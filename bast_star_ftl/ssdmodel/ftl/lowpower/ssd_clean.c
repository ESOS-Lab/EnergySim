// DiskSim SSD support
// 2008 Microsoft Corporation. All Rights Reserved

#include "ssd.h"
#include "ftl.h"
#include "ssd_clean.h"
#include "ssd_utils.h"
//@20090831-Micky::added
#include "ssd_power.h"
//--

/*
 * return true if two blocks belong to the same plane.
 */
static int ssd_same_plane_blocks
(ssd_t *s, ssd_element_metadata *metadata, int from_blk, int to_blk)
{
    return (metadata->block_usage[from_blk].plane_num == metadata->block_usage[to_blk].plane_num);
}

/*
 * we clean a block only after it is fully used.
 */
int ssd_can_clean_block(ssd_t *s, ssd_element_metadata *metadata, int blk)
{
    int bitpos = ssd_block_to_bitpos(s, blk);
    return ((ssd_bit_on(metadata->free_blocks, bitpos)) && (metadata->block_usage[blk].state == SSD_BLOCK_SEALED));
}

/*
 * calculates the cost of reading and writing a block of data across planes.
 */
static double ssd_crossover_cost
(ssd_t *s, ssd_element_metadata *metadata, ssd_power_element_stat *power_stat, int from_blk, int to_blk)
{
    if (ssd_same_plane_blocks(s, metadata, from_blk, to_blk)) {
        return 0;
    } else {
        double xfer_cost;

        // we need to read and write back across the pins
        xfer_cost = ssd_data_transfer_cost(s, s->params.page_size);
        ssd_power_flash_calculate(SSD_POWER_FLASH_BUS_DATA_TRANSFER, 2*xfer_cost, power_stat, s);

        return (2 * xfer_cost);
    }
}


/*
 * update the remaining life time and time of last erasure
 * for this block.
 * we did some operations since the last update to
 * simtime variable and the time took for these operations are
 * in the 'cost' variable. so the time of last erasure is
 * cost + the simtime
 */
void ssd_update_block_lifetime(double time, int blk, ssd_element_metadata *metadata)
{
    metadata->block_usage[blk].rem_lifetime --;
    metadata->block_usage[blk].time_of_last_erasure = time;

    if (metadata->block_usage[blk].rem_lifetime < 0) {
        fprintf(stderr, "Error: Negative lifetime %d (block is being erased after it's dead)\n",
            metadata->block_usage[blk].rem_lifetime);
        ASSERT(0);
        exit(1);
    }
}

/*
 * updates the status of erased blocks
 */
void ssd_update_free_block_status(int blk, int plane_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int bitpos;

    // clear the bit corresponding to this block in the
    // free blocks list for future use
    bitpos = ssd_block_to_bitpos(s, blk);
    ssd_clear_bit(metadata->free_blocks, bitpos);
    metadata->block_usage[blk].state = SSD_BLOCK_CLEAN;
    metadata->block_usage[blk].bsn = 0;
    metadata->tot_free_blocks ++;
    metadata->plane_meta[plane_num].free_blocks ++;
    ssd_assert_free_blocks(s, metadata);

    // there must be no valid pages in the erased block
    ASSERT(metadata->block_usage[blk].num_valid == 0);
    ssd_assert_valid_pages(plane_num, metadata, s);
}

//#define CAMERA_READY

/*
 * returns one if the block must be rate limited
 * because of its reduced block life else returns 0, if the
 * block can be continued with cleaning.
 * CAMERA READY: adding rate limiting according to the new
 * definition
 */
#ifdef CAMERA_READY
int ssd_rate_limit(int block_life, double avg_lifetime)
{
    double percent_rem = (block_life * 1.0) / avg_lifetime;
    double temp = (percent_rem - (SSD_LIFETIME_THRESHOLD_X-SSD_RATELIMIT_WINDOW)) / (SSD_LIFETIME_THRESHOLD_X - percent_rem);
    double rand_no = DISKSIM_drand48();

    // i can use this block
    if (rand_no < temp) {
        return 0;
    } else {
        //i cannot use this block
        //if (rand_no >= temp)
        return 1;
    }
}
#else
int ssd_rate_limit(int block_life, double avg_lifetime)
{
    double percent_rem = (block_life * 1.0) / avg_lifetime;
    double temp = percent_rem / SSD_LIFETIME_THRESHOLD_X;
    double rand_no = DISKSIM_drand48();

    // i can use this block
    if (rand_no < temp) {
        return 0;
    } else {
        //i cannot use this block
        //if (rand_no >= temp)
        return 1;
    }
}
#endif

/*
 * computes the average lifetime of all the blocks in a plane.
 */
double ssd_compute_avg_lifetime_in_plane(int plane_num, int elem_num, ssd_t *s)
{
    int i;
    int bitpos;
    double tot_lifetime = 0;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    bitpos = plane_num * s->params.blocks_per_plane;
    for (i = bitpos; i < bitpos + (int)s->params.blocks_per_plane; i ++) {
        int block = ssd_bitpos_to_block(i, s);
        ASSERT(metadata->block_usage[block].plane_num == plane_num);
        tot_lifetime += metadata->block_usage[block].rem_lifetime;
    }

    return (tot_lifetime / s->params.blocks_per_plane);
}


/*
 * computes the average lifetime of all the blocks in the ssd element.
 */
double ssd_compute_avg_lifetime_in_element(int elem_num, ssd_t *s)
{
    double tot_lifetime = 0;
    int i;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    for (i = 0; i < s->params.blocks_per_element; i ++) {
        tot_lifetime += metadata->block_usage[i].rem_lifetime;
    }

    return (tot_lifetime / s->params.blocks_per_element);
}

double ssd_compute_avg_lifetime(int plane_num, int elem_num, ssd_t *s)
{
	double avr_lifetime = 0.0;
    if (plane_num == -1) {
		avr_lifetime = ssd_compute_avg_lifetime_in_element(elem_num, s);
        return avr_lifetime;
    } else {
		avr_lifetime = ssd_compute_avg_lifetime_in_plane(plane_num, elem_num, s);
        return avr_lifetime;
    }
}

#ifdef CAMERA_READY
// if we care about wear-leveling, then we must rate limit overly cleaned blocks.
// return 1 if it is ok to clean this block.
// CAMERA READY: making changes for the camera ready.
// if the block life is less than lifetime threshold x and within
// a rate limit window, then it is rate limited with probability
// that linearly increases from 0 from 1 as its remaining life time
// drops from (SSD_LIFETIME_THRESHOLD_X * avg_lifetime) to
// (SSD_LIFETIME_THRESHOLD_X - SSD_RATELIMIT_WINDOW) * avg_lifetime.
// if it is below (SSD_LIFETIME_THRESHOLD_X - SSD_RATELIMIT_WINDOW) * avg_lifetime
// then the probability is 0.
int ssd_pick_wear_aware(int blk, int block_life, double avg_lifetime, ssd_t *s)
{
    ASSERT(s->params.cleaning_policy == DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE);

    // see if this block's remaining lifetime is within
    // a certain threshold of the average remaining lifetime
    // of all blocks in this element
    if (block_life < (SSD_LIFETIME_THRESHOLD_X * avg_lifetime)) {
        if ((block_life > (SSD_LIFETIME_THRESHOLD_X - SSD_RATELIMIT_WINDOW) * avg_lifetime)) {
            // we have to rate limit this block as it has exceeded
            // its cleaning limits
            //printf("Rate limiting block %d (block life %d avg life %f\n",
            //  blk, block_life, avg_lifetime);

            if (ssd_rate_limit(block_life, avg_lifetime)) {
                // skip this block and go to the next one
                return 0;
            }
        } else {
            // this block's lifetime is less than (SSD_LIFETIME_THRESHOLD_X - SSD_RATELIMIT_WINDOW)
            // of the average life time.
            // skip this block and go to the next one
            return 0;
        }
    }

    return 1;
}
#else
// if we care about wear-leveling, then we must rate limit overly cleaned blocks.
// return 1 if it is ok to clean this block
int ssd_pick_wear_aware(int blk, int block_life, double avg_lifetime, ssd_t *s)
{
    ASSERT(s->params.cleaning_policy == DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE);

    // see if this block's remaining lifetime is within
    // a certain threshold of the average remaining lifetime
    // of all blocks in this element
    if (block_life < (SSD_LIFETIME_THRESHOLD_X * avg_lifetime)) {

        // we have to rate limit this block as it has exceeded
        // its cleaning limits
        //printf("Rate limiting block %d (block life %d avg life %f\n",
        //  blk, block_life, avg_lifetime);

        if (ssd_rate_limit(block_life, avg_lifetime)) {
            // skip this block and go to the next one
            return 0;
        }
    }

    return 1;
}
#endif

static int _ssd_pick_block_to_clean
(int blk, int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int block_life;

    if (plane_num != -1) {
        if (metadata->block_usage[blk].plane_num != plane_num) {
            return 0;
        }
    }

    block_life = metadata->block_usage[blk].rem_lifetime;

    // if the block is already dead, skip it
    if (block_life == 0) {
        return 0;
    }

    // clean only those blocks that are sealed.
    /*if (!ssd_can_clean_block(s, metadata, blk)) {
        return 0;
    }*/

    return 1;
}

/*
 * migrate data from a cold block to "to_blk"
 */
int ssd_migrate_cold_data(int to_blk, double *mcost, int plane_num, int elem_num, ssd_t *s)
{
    int i;
    int from_blk = -1;
    double oldest_erase_time = simtime;
    double cost = 0;
    int bitpos;
    ssd_power_element_stat *power_stat = &(s->elements[elem_num].power_stat);


#if SSD_ASSERT_ALL
    int f1;
    int f2;
#endif

    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    // first select the coldest of all blocks.
    // one way to select is to find the one that has the oldest
    // erasure time.
    if (plane_num == -1) {
        for (i = 0; i < s->params.blocks_per_element; i ++) {
            if (metadata->block_usage[i].num_valid > 0) {
                if (metadata->block_usage[i].time_of_last_erasure < oldest_erase_time) {
                    oldest_erase_time = metadata->block_usage[i].time_of_last_erasure;
                    from_blk = i;
                }
            }
        }
    } else {

#if SSD_ASSERT_ALL
        f1 = ssd_free_bits(plane_num, elem_num, metadata, s);
        ASSERT(f1 == metadata->plane_meta[metadata->block_usage[to_blk].plane_num].free_blocks);
#endif

        bitpos = plane_num * s->params.blocks_per_plane;
        for (i = bitpos; i < bitpos + (int)s->params.blocks_per_plane; i ++) {
            int block = ssd_bitpos_to_block(i, s);
            ASSERT(metadata->block_usage[block].plane_num == plane_num);

            if (metadata->block_usage[block].num_valid > 0) {
                if (metadata->block_usage[block].time_of_last_erasure < oldest_erase_time) {
                    oldest_erase_time = metadata->block_usage[block].time_of_last_erasure;
                    from_blk = block;
                }
            }
        }
    }

    ASSERT(from_blk != -1);
    if (plane_num != -1) {
        ASSERT(metadata->block_usage[from_blk].plane_num == metadata->block_usage[to_blk].plane_num);
    }

    // next, clean the block to which we'll transfer the
    // cold data
    cost += _ssd_clean_block_fully(to_blk, metadata->block_usage[to_blk].plane_num, elem_num, metadata, s);

#if SSD_ASSERT_ALL
    if (plane_num != -1) {
        f2 = ssd_free_bits(plane_num, elem_num, metadata, s);
        ASSERT(f2 == metadata->plane_meta[metadata->block_usage[to_blk].plane_num].free_blocks);
    }
#endif

    // then, migrate the cold data to the worn out block.
    // for which, we first read all the valid data
    cost += metadata->block_usage[from_blk].num_valid * s->params.page_read_latency;
    ssd_power_flash_calculate(SSD_POWER_FLASH_READ, metadata->block_usage[from_blk].num_valid * s->params.page_read_latency, power_stat, s);

    // include the write cost
    cost += metadata->block_usage[from_blk].num_valid * s->params.page_write_latency;
    ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, metadata->block_usage[from_blk].num_valid * s->params.page_write_latency, power_stat, s);
    
    // if the src and dest blocks are on different planes
    // include the transfer cost also
    cost += ssd_crossover_cost(s, metadata, power_stat, from_blk, to_blk);

    // the cost of erasing the cold block (represented by from_blk)
    // will be added later ...

    // finally, update the metadata
    metadata->block_usage[to_blk].bsn = metadata->block_usage[from_blk].bsn;
    metadata->block_usage[to_blk].num_valid = metadata->block_usage[from_blk].num_valid;
    metadata->block_usage[from_blk].num_valid = 0;

    for (i = 0; i < s->params.pages_per_block; i ++) {
        int lbn = metadata->block_usage[from_blk].page[i];
        if (lbn != -1) {
            ASSERT(metadata->lba_table[lbn] == (from_blk * s->params.pages_per_block + i));
            metadata->lba_table[lbn] = to_blk * s->params.pages_per_block + i;
        }
        metadata->block_usage[to_blk].page[i] = metadata->block_usage[from_blk].page[i];
    }
    metadata->block_usage[to_blk].state = metadata->block_usage[from_blk].state;

    bitpos = ssd_block_to_bitpos(s, to_blk);
    ssd_set_bit(metadata->free_blocks, bitpos);
    metadata->tot_free_blocks --;
    metadata->plane_meta[metadata->block_usage[to_blk].plane_num].free_blocks --;

#if SSD_ASSERT_ALL
    if (plane_num != -1) {
        f2 = ssd_free_bits(plane_num, elem_num, metadata, s);
        ASSERT(f2 == metadata->plane_meta[metadata->block_usage[to_blk].plane_num].free_blocks);
    }
#endif

    ssd_assert_free_blocks(s, metadata);
    ASSERT(metadata->block_usage[from_blk].num_valid == 0);

#if ASSERT_FREEBITS
    if (plane_num != -1) {
        f2 = ssd_free_bits(plane_num, elem_num, metadata, s);
        ASSERT(f1 == f2);
    }
#endif

    *mcost = cost;

    // stat
    metadata->tot_migrations ++;
    metadata->tot_pgs_migrated += metadata->block_usage[to_blk].num_valid;
    metadata->mig_cost += cost;

    return from_blk;
}

// return 1 if it is ok to clean this block
int ssd_pick_wear_aware_with_migration
(int blk, int block_life, double avg_lifetime, double *cost, int plane_num, int elem_num, ssd_t *s)
{
    int from_block = blk;
    double retirement_age;
    ASSERT(s->params.cleaning_policy == DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE);

    retirement_age = SSD_LIFETIME_THRESHOLD_Y * avg_lifetime;


    // see if this block's remaining lifetime is less than
    // the retirement threshold. if so, migrate cold data
    // into it.
    if (block_life < retirement_age) {

        // let us migrate some cold data into this block
        from_block = ssd_migrate_cold_data(blk, cost, plane_num, elem_num, s);
        //printf("Migrating frm blk %d to blk %d (blk life %d avg life %f\n",
        //  from_block, blk, block_life, avg_lifetime);
    }

    return from_block;
}

/*
 * a greedy solution, where we find the block in a plane with the least
 * num of valid pages and return it.
 */
static int ssd_pick_block_to_clean2(int plane_num, int elem_num, double *mcost, ssd_element_metadata *metadata, ssd_t *s)
{
    double avg_lifetime = 1;
    int i;
    int size;
    int block = -1;
    int min_valid = s->params.pages_per_block - 1; // one page goes for the summary info
    listnode *greedy_list;

    *mcost = 0;

    // find the average life time of all the blocks in this element
    avg_lifetime = ssd_compute_avg_lifetime(plane_num, elem_num, s);

    // we create a list of greedily selected blocks
    ll_create(&greedy_list);
    for (i = 0; i < s->params.blocks_per_element; i ++) {
        if (_ssd_pick_block_to_clean(i, plane_num, elem_num, metadata, s)) {

            // greedily select the block
            if (metadata->block_usage[i].num_valid <= min_valid) {
                ASSERT(i == metadata->block_usage[i].block_num);
                ll_insert_at_head(greedy_list, (void*)&metadata->block_usage[i]);
                min_valid = metadata->block_usage[i].num_valid;
                block = i;
            }
        }
    }

    ASSERT(block != -1);
    block = -1;

    // from the greedily picked blocks, select one after rate
    // limiting the overly used blocks
    size = ll_get_size(greedy_list);

    //printf("plane %d elem %d size %d avg lifetime %f\n",
    //  plane_num, elem_num, size, avg_lifetime);

    for (i = 0; i < size; i ++) {
        block_metadata *bm;
        int mig_blk;

        listnode *n = ll_get_nth_node(greedy_list, i);
        bm = ((block_metadata *)n->data);

        if (i == 0) {
            ASSERT(min_valid == bm->num_valid);
        }

        // this is the last of the greedily picked blocks.
        if (i == size -1) {
            // select it!
            block = bm->block_num;
            break;
        }

        if (s->params.cleaning_policy == DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AGNOSTIC) {
            block = bm->block_num;
            break;
        } else {
#if MIGRATE
            // migration
            mig_blk = ssd_pick_wear_aware_with_migration(bm->block_num, bm->rem_lifetime, avg_lifetime, mcost, bm->plane_num, elem_num, s);
            if (mig_blk != bm->block_num) {
                // data has been migrated and we have a new
                // block to use
                block = mig_blk;
                break;
            }
#endif

            // pick this block giving consideration to its life time
            if (ssd_pick_wear_aware(bm->block_num, bm->rem_lifetime, avg_lifetime, s)) {
                block = bm->block_num;
                break;
            }
        }
    }

    ll_release(greedy_list);

    ASSERT(block != -1);
    return block;
}

static int ssd_pick_block_to_clean(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
	int block;
	int log_block;
	int lbn;
	int min_valid = s->params.pages_per_block * 2;
	int i,j;
	double avg_lifetime = 1;
	unsigned int reserved_blocks_per_plane = (s->params.reserve_blocks * s->params.blocks_per_plane) / 100;
    unsigned int usable_blocks_per_plane = s->params.blocks_per_plane - reserved_blocks_per_plane;
    unsigned int reserved_blocks = reserved_blocks_per_plane * s->params.planes_per_pkg;
    unsigned int usable_blocks = usable_blocks_per_plane * s->params.planes_per_pkg;
	int num_valid;

	avg_lifetime = ssd_compute_avg_lifetime(plane_num, elem_num, s);

	for(i = 0; i < usable_blocks ; i++) {
		block = metadata->lba_table[i];
		if(block == -1){
			continue;
		}
		if( metadata->block_usage[block].log_index == -1){
			continue;
		}

		log_block = metadata->log_data[metadata->block_usage[block].log_index].bsn;
		num_valid = metadata->block_usage[block].num_valid + metadata->block_usage[log_block].num_valid;
		if(num_valid < min_valid) {
			/*min_valid = metadata->block_usage[block].num_valid;
			if(_ssd_pick_block_to_clean(block, plane_num, elem_num, metadata, s)){
				int mig_blk;
				block_metadata bm;
				bm = metadata->block_usage[block];
				mig_blk = ssd_pick_wear_aware_with_migration(bm.block_num, bm.rem_lifetime, avg_lifetime, mcost, bm.plane_num, elem_num, s);
				if (ssd_pick_wear_aware(bm.block_num, bm.rem_lifetime, avg_lifetime, s)) {
					lbn = i;
					break;
				}
			}*/
			lbn = i;
		}
	}

    return lbn;
}

double _ssd_clean_block_fully(int blk, int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    double cost = 0;
    plane_metadata *pm = &metadata->plane_meta[plane_num];
	ssd_power_element_stat *power_stat = &(s->elements[elem_num].power_stat);

    ASSERT((pm->clean_in_progress == 0) && (pm->clean_in_block = -1));
    pm->clean_in_block = blk;
    pm->clean_in_progress = 1;

    // stat
    pm->num_cleans ++;
    s->elements[elem_num].stat.num_clean ++;

	cost = s->params.block_erase_latency;
    //Micky:add the power consumption of the erase
	ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, s->params.block_erase_latency, power_stat, s);

	ssd_update_free_block_status(blk, plane_num, metadata, s);
	ssd_update_block_lifetime(simtime+cost, blk, metadata);
	pm->clean_in_progress = 0;
	pm->clean_in_block = -1;

    return cost;
}

static usage_table *ssd_build_usage_table(int elem_num, ssd_t *s)
{
    int i;
    usage_table *table;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    //////////////////////////////////////////////////////////////////////////////
    // allocate the hash table. it has (pages_per_block + 1) entries,
    // one entry for values from 0 to pages_per_block.
    table = (usage_table *) malloc(sizeof(usage_table) * (s->params.pages_per_block + 1));
    memset(table, 0, sizeof(usage_table) * (s->params.pages_per_block + 1));

    // find out how many blocks have a particular no of valid pages
    for (i = 0; i < s->params.blocks_per_element; i ++) {
        int usage = metadata->block_usage[i].num_valid;
        table[usage].len ++;
    }

    // allocate space to store the block numbers
    for (i = 0; i <= s->params.pages_per_block; i ++) {
        table[i].block = (int*)malloc(sizeof(int)*table[i].len);
    }

    /////////////////////////////////////////////////////////////////////////////
    // fill in the block numbers in their appropriate 'usage' buckets
    for (i = 0; i < s->params.blocks_per_element; i ++) {
        usage_table *entry;
        int usage = metadata->block_usage[i].num_valid;

        entry = &(table[usage]);
        entry->block[entry->temp ++] = i;
    }

    return table;
}

static void ssd_release_usage_table(usage_table *table, ssd_t *s)
{
    int i;

    for (i = 0; i <= s->params.pages_per_block; i ++) {
        free(table[i].block);
    }

    // release the table
    free(table);
}

static usage_table *ssd_build_log_usage_table(int elem_num, ssd_t *s)
{
    int i;
    usage_table *table;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);
	int reserved_blocks_per_plane = (s->params.reserve_blocks * s->params.blocks_per_plane) / 100;
	int reserved_blocks = reserved_blocks_per_plane * s->params.planes_per_pkg;  

    //////////////////////////////////////////////////////////////////////////////
    // allocate the hash table. it has (pages_per_block + 1) entries,
    // one entry for values from 0 to pages_per_block.
    table = (usage_table *) malloc(sizeof(usage_table) * (s->params.pages_per_block + 1));
    memset(table, 0, sizeof(usage_table) * (s->params.pages_per_block + 1));

    // find out how many blocks have a particular no of valid pages
    for (i = 0; i < reserved_blocks; i ++) {
		int usage;
		int block;
		if(metadata->log_data[i].bsn != -1){
			block = metadata->log_data[i].bsn;
			usage = metadata->block_usage[block].num_valid;
			table[usage].len ++;
		}
    }

    // allocate space to store the block numbers
    for (i = 0; i <= s->params.pages_per_block; i ++) {
        table[i].block = (int*)malloc(sizeof(int)*table[i].len);
    }

    /////////////////////////////////////////////////////////////////////////////
    // fill in the block numbers in their appropriate 'usage' buckets
    for (i = 0; i < reserved_blocks; i ++) {
        usage_table *entry;
        int usage;
		int block;
		if(metadata->log_data[i].bsn != -1){
			block = metadata->log_data[i].bsn;
			usage = metadata->block_usage[block].num_valid;
			entry = &(table[usage]);
			entry->block[entry->temp ++] = block;
		}
    }

    return table;
}

static void ssd_release_log_usage_table(usage_table *table, ssd_t *s)
{
    int i;

    for (i = 0; i <= s->params.pages_per_block; i ++) {
        free(table[i].block);
    }

    // release the table
    free(table);
}

#define GREEDY_IN_COPYBACK 0

/*
 * first we create a hash table of blocks according to their
 * usage. then we select blocks with the least usage and clean
 * them.
 */
static double ssd_clean_blocks_greedy(int plane_num, int elem_num, ssd_t *s)
{
    double cost = 0;
    double avg_lifetime;
    int i;
    usage_table *table;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    /////////////////////////////////////////////////////////////////////////////
    // build the histogram
    table = ssd_build_usage_table(elem_num, s);

    //////////////////////////////////////////////////////////////////////////////
    // find the average life time of all the blocks in this element
    avg_lifetime = ssd_compute_avg_lifetime(plane_num, elem_num, s);

    /////////////////////////////////////////////////////////////////////////////
    // we now have a hash table of blocks, where the key of each
    // bucket is the usage count and each bucket has all the blocks with
    // the same usage count (i.e., the same num of valid pages).
    for (i = 0; i <= s->params.pages_per_block; i ++) {
        int j;
        usage_table *entry;

        // get the bucket of blocks with 'i' valid pages
        entry = &(table[i]);

        // free all the blocks with 'i' valid pages
        for (j = 0; j < entry->len; j ++) {
            int blk = entry->block[j];
            int block_life = metadata->block_usage[blk].rem_lifetime;

            // if this is plane specific cleaning, then skip all the
            // blocks that don't belong to this plane.
            if ((plane_num != -1) && (metadata->block_usage[blk].plane_num != plane_num)) {
                continue;
            }

            // if the block is already dead, skip it
            if (block_life == 0) {
                continue;
            }

            // clean only those blocks that are sealed.
            if (ssd_can_clean_block(s, metadata, blk)) {

                // if we care about wear-leveling, then we must rate limit overly cleaned blocks
                if (s->params.cleaning_policy == DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE) {

                    // see if this block's remaining lifetime is within
                    // a certain threshold of the average remaining lifetime
                    // of all blocks in this element
                    if (block_life < (SSD_LIFETIME_THRESHOLD_X * avg_lifetime)) {
                        // we have to rate limit this block as it has exceeded
                        // its cleaning limits
                        printf("Rate limiting block %d (block life %d avg life %f\n",
                            blk, block_life, avg_lifetime);

                        if (ssd_rate_limit(block_life, avg_lifetime)) {
                            // skip this block and go to the next one
                            continue;
                        }
                    }
                }

                // okies, finally here we're with the block to be cleaned.
                // invoke cleaning until we reach the high watermark.
                cost += _ssd_clean_block_fully(blk, metadata->block_usage[blk].plane_num, elem_num, metadata, s);

                if (ssd_stop_cleaning(plane_num, elem_num, s)) {
                    // no more cleaning is required -- so quit.
                    break;
                }
            }
        }

        if (ssd_stop_cleaning(plane_num, elem_num, s)) {
            // no more cleaning is required -- so quit.
            break;
        }
    }

    // release the table
    ssd_release_usage_table(table, s);

    // see if we were able to generate enough free blocks
    if (!ssd_stop_cleaning(plane_num, elem_num, s)) {
        printf("Yuck! we couldn't generate enough free pages in plane %d elem %d ssd %d\n",
            plane_num, elem_num, s->devno);
    }

    return cost;
}

/*
 * 1. find a plane to clean in each of the parallel unit
 * 2. invoke copyback cleaning on all such planes simultaneously
 */

double ssd_fullmerge(ssd_t *s, ssd_element_metadata *metadata, ssd_power_element_stat *power_stat, int lbn, int elem_num)
{
	int prev_block = metadata->lba_table[lbn];
	int log_index = metadata->block_usage[prev_block].log_index;
	int log_block = metadata->log_data[log_index].bsn;
	int num_valid = 0;
	int num_valid_d = metadata->block_usage[prev_block].num_valid;
	int num_valid_u = metadata->block_usage[log_block].num_valid;
	int prev_plane_num = metadata->block_usage[prev_block].plane_num;
	int log_plane_num = metadata->block_usage[log_block].plane_num;
	int plane_num;

	int active_block;
	int i;
	double cost = 0.0;
	double r_cost, w_cost, xfer_cost;

	//set active_block
	metadata->active_block = metadata->plane_meta[prev_plane_num].active_block;
	active_block = metadata->active_block;
	_ssd_alloc_active_block(prev_plane_num, elem_num, s);
	plane_num = metadata->block_usage[active_block].plane_num;
	metadata->plane_meta[prev_plane_num].clean_in_block = prev_block;
	metadata->plane_meta[prev_plane_num].clean_in_progress = 1;
	metadata->plane_meta[log_plane_num].clean_in_block = prev_block;
	metadata->plane_meta[log_plane_num].clean_in_progress = 1;

	//page state copy & init page state
	for( i = 0 ; i < s->params.pages_per_block ; i++) {
		if((metadata->block_usage[prev_block].page[i] == 1) || (metadata->log_data[log_index].page[i] != -1)){
			metadata->block_usage[active_block].page[i] = 1;
		}
		metadata->block_usage[prev_block].page[i] = -1;
		metadata->log_data[log_index].page[i] = -1;
		metadata->block_usage[log_block].page[i] = -1;
	}
	metadata->lba_table[lbn] = active_block;

	//update stat
	//update log_table
	metadata->block_usage[prev_block].log_index = -1;
	metadata->log_data[log_index].bsn = -1;
	metadata->log_data[log_index].data_block = -1;
	metadata->log_pos = log_index;
	metadata->num_log--;
	//update block usage
	metadata->block_usage[prev_block].num_valid = 0;
	metadata->block_usage[log_block].num_valid = 0;
	//update plane data
	metadata->plane_meta[prev_plane_num].valid_pages -= num_valid_d;
	metadata->plane_meta[log_plane_num].valid_pages -= num_valid_u;
	num_valid += num_valid_d;
	num_valid += num_valid_u;
	if(num_valid > s->params.pages_per_block) {
		fprintf(outputfile3, "Error number of pages : valid_page %d, Real_page %d\n", num_valid, s->params.pages_per_block);
		fprintf(outputfile3, "Error elem_num %d, lbn %d, original block %d, log block %d\n", elem_num, lbn, prev_block, log_block);
		exit(-1);
	}
	metadata->block_usage[active_block].num_valid = num_valid;
	metadata->plane_meta[plane_num].valid_pages += num_valid;


	//data tranfer cost
	//read
	r_cost = s->params.page_read_latency * num_valid;
	cost += r_cost;
	ssd_power_flash_calculate(SSD_POWER_FLASH_READ, r_cost, power_stat, s);
	cost += s->params.page_read_latency;
	ssd_power_flash_calculate(SSD_POWER_FLASH_READ, s->params.page_read_latency, power_stat, s);
	s->spare_read++;

	//write
	w_cost = s->params.page_write_latency * num_valid;
	cost += w_cost;
	ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, w_cost, power_stat, s);

	//transfer cost
	for( i = 0 ; i < num_valid ; i++) {
		double xfer_cost;
		xfer_cost = ssd_crossover_cost(s, metadata, power_stat, prev_block, active_block);
		cost += xfer_cost;
		s->elements[elem_num].stat.tot_xfer_cost += xfer_cost;
	}

	//erase two block(D)
	cost += s->params.block_erase_latency;
	ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, s->params.block_erase_latency, power_stat, s);
	ssd_update_free_block_status(prev_block, prev_plane_num, metadata, s);
	ssd_update_block_lifetime(simtime+cost, prev_block, metadata);
	metadata->plane_meta[prev_plane_num].num_cleans++;
	metadata->plane_meta[prev_plane_num].clean_in_block = 0;
	metadata->plane_meta[prev_plane_num].clean_in_progress = -1;

	//erase two block(U)
	cost += s->params.block_erase_latency;
	ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, s->params.block_erase_latency, power_stat, s);
	ssd_update_free_block_status(log_block, log_plane_num, metadata, s);
	ssd_update_block_lifetime(simtime+cost, log_block, metadata);
	metadata->plane_meta[log_plane_num].num_cleans++;
	metadata->plane_meta[log_plane_num].clean_in_block = 0;
	metadata->plane_meta[log_plane_num].clean_in_progress = -1;

	//erase stat update
	s->elements[elem_num].stat.pages_moved += num_valid;
	s->elements[elem_num].stat.num_clean += 2;
	s->elements[elem_num].stat.num_fullmerge++;

	return cost;
}

double ssd_switch(ssd_t *s, ssd_element_metadata *metadata, ssd_power_element_stat *power_stat, int lbn, int elem_num)
{
	int d_block = metadata->lba_table[lbn];
	int log_index = metadata->block_usage[d_block].log_index;
	int log_block = metadata->log_data[log_index].bsn;
	int num_valid = 0;
	int num_valid_d = metadata->block_usage[d_block].num_valid;
	int num_valid_u = metadata->block_usage[log_block].num_valid;
	int d_plane_num = metadata->block_usage[d_block].plane_num;
	int log_plane_num = metadata->block_usage[log_block].plane_num;

	int i;
	double cost = 0.0;

	metadata->plane_meta[d_plane_num].clean_in_block = d_block;
	metadata->plane_meta[d_plane_num].clean_in_progress = 1;

	//switch
	for( i = 0 ; i < s->params.pages_per_block ; i++) {
		metadata->block_usage[d_block].page[i] = -1;
		metadata->log_data[log_index].page[i] = -1;
	}
	metadata->lba_table[lbn] = log_block;

	//update stat
	metadata->block_usage[d_block].log_index = -1;
	metadata->log_data[log_index].bsn = -1;
	metadata->log_data[log_index].data_block = -1;
	metadata->num_log--;
	metadata->log_pos = log_index;
	//update block usage
	metadata->block_usage[d_block].num_valid = 0;
	//update plane data
	metadata->plane_meta[d_plane_num].valid_pages -= num_valid_d;

	//erase two block(D)
	cost += s->params.block_erase_latency;
	ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, s->params.block_erase_latency, power_stat, s);
	ssd_update_free_block_status(d_block, d_plane_num, metadata, s);
	ssd_update_block_lifetime(simtime+cost, d_block, metadata);
	metadata->plane_meta[d_plane_num].clean_in_block = 0;
	metadata->plane_meta[d_plane_num].clean_in_progress = -1;
	metadata->plane_meta[d_plane_num].num_cleans++;
	s->elements[elem_num].stat.num_clean++;
	s->elements[elem_num].stat.num_switch++;

	return cost;
}


double ssd_merge(ssd_t *s, int elem_num, int lbn)
{
	ssd_power_element_stat *power_stat;
	ssd_element_metadata *metadata;
	int block;
	int log_index;
	int log_block;
	int offset;
	int type;
	int i;
	double cost = 0.0;


	metadata = &(s->elements[elem_num].metadata);
	power_stat = &(s->elements[elem_num].power_stat);

	block = metadata->lba_table[lbn];
	log_index = metadata->block_usage[block].log_index;
	log_block = metadata->log_data[log_index].bsn;

	//fullmerge or switch or replacement
	//if d-block is full used, merge is fullmerge
	//if d-block is full used and all log-block page is sequential, merge is switch
	offset = metadata->log_data[log_index].page[0];
	for( i = 1 ; i < s->params.pages_per_block ; i++){
		if ( offset == -1) {
			type = FULL_MERGE;
			break;
		}
		else if (offset > metadata->log_data[log_index].page[i]) {
			type = FULL_MERGE;
			break;
		}
		else {
			type = SWITCH;
		}
		offset = metadata->log_data[log_index].page[i];
	}

	switch(type){
		case FULL_MERGE:
			cost = ssd_fullmerge(s, metadata, power_stat, lbn, elem_num);
			break;
		case SWITCH:
			cost = ssd_switch(s, metadata, power_stat, lbn, elem_num);
			break;
		default:
			printf("Unkwon case\n");
			break;
	}
	//cost = ssd_fullmerge(s, metadata, power_stat, lbn, elem_num);

	return cost;
}

double ssd_replacement(ssd_t *s, int elem_num, int lbn)
{
	ssd_element_metadata *metadata;
	ssd_power_element_stat *power_stat;
	int block;
	int log_index;
	int prev_log_block;
	int prev_plane_num;
	int log_block;
	int plane_num;
	int num_valid;
	double cost = 0.0;
	double r_cost, w_cost, xfer_cost;
	int i,j;

	metadata = &(s->elements[elem_num].metadata);
	power_stat = &(s->elements[elem_num].power_stat);

	block = metadata->lba_table[lbn];
	log_index = metadata->block_usage[block].log_index;
	prev_log_block = metadata->log_data[log_index].bsn;
	prev_plane_num = metadata->block_usage[prev_log_block].plane_num;
	num_valid = metadata->block_usage[prev_log_block].num_valid;

	//alloc new logblock and erase old logblock
	log_block = metadata->plane_meta[prev_plane_num].active_block;
	_ssd_alloc_active_block(prev_plane_num, elem_num, s);
	plane_num = metadata->block_usage[log_block].plane_num;
	metadata->log_data[log_index].bsn = log_block; 
	//move page old to new 
	j = 0;
	for( i = 0 ; i < s->params.pages_per_block ; i++) {
		if( metadata->log_data[log_index].page[i] != -1) {
			metadata->log_data[log_index].page[i] = j;
			metadata->block_usage[log_block].page[j] = 1;
			metadata->block_usage[log_block].num_valid++;
			j++;
		}
		metadata->block_usage[prev_log_block].page[i] = -1;
	}
	metadata->block_usage[prev_log_block].num_valid = 0;

	//plane metadata update
	metadata->plane_meta[prev_plane_num].valid_pages -= num_valid;
	metadata->plane_meta[plane_num].valid_pages += num_valid;

	//cost
	//read
	r_cost = s->params.page_read_latency * num_valid;
	cost += r_cost;
	ssd_power_flash_calculate(SSD_POWER_FLASH_READ, r_cost, power_stat, s);

	//write
	w_cost = s->params.page_write_latency * num_valid;
	cost += w_cost;
	ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, w_cost, power_stat, s);

	//transfer cost
	for( i = 0 ; i < num_valid ; i++) {
		double xfer_cost;
		xfer_cost = ssd_crossover_cost(s, metadata, power_stat, prev_log_block, log_block);
		cost += xfer_cost;
		s->elements[elem_num].stat.tot_xfer_cost += xfer_cost;
	}

	//erase U block
	cost += s->params.block_erase_latency;
	ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, s->params.block_erase_latency, power_stat, s);
	ssd_update_free_block_status(prev_log_block, prev_plane_num, metadata, s);
	ssd_update_block_lifetime(simtime+cost, prev_log_block, metadata);
	s->elements[elem_num].stat.pages_moved += num_valid;
	s->elements[elem_num].stat.num_clean ++;
	s->elements[elem_num].stat.num_replacement++;
	metadata->plane_meta[prev_plane_num].num_cleans++;

	return cost;

}

double ssd_clean_element(ssd_t *s, int elem_num)
{
	ssd_element_metadata *metadata;
    double cost = 0;
	int lbn;
	int reserved = s->params.reserve_blocks * s->params.blocks_per_element * 0.01;
	int low_thresold = reserved * 0.9;

	metadata = &(s->elements[elem_num].metadata);
	/*if (!ssd_start_cleaning(-1, elem_num, s)) {
		if(metadata->num_log >= reserved){
			cost = ssd_merge(s, elem_num, lbn);
		}
        return cost;
    }*/
	while (metadata->num_log > low_thresold) {
		lbn = ssd_pick_block_to_clean(-1, elem_num, metadata, s);

		cost += ssd_merge(s, elem_num, lbn);
        //return cost;
    }

    return cost;
}

double ssd_clean_logblock(ssd_t *s, int elem_num, int lbn)
{
    double cost = 0;
	ssd_element_metadata *metadata;
	int block;
	int check_page_use = 0;
	int i;

	metadata = &(s->elements[elem_num].metadata);
	block = metadata->lba_table[lbn];
	for( i = 0 ; i < (s->params.pages_per_block -1) ; i++){
		if(metadata->block_usage[block].page[i] != -1)
			check_page_use++;
	}

	/*if(check_page_use == (s->params.pages_per_block -1))
		cost = ssd_merge(s, elem_num, lbn);
	else
		cost = ssd_replacement(s, elem_num, lbn);*/
	cost = ssd_merge(s, elem_num, lbn);
    
    return cost;
}

int ssd_next_plane_in_parunit(int plane_num, int parunit_num, int elem_num, ssd_t *s)
{
    return (parunit_num*SSD_PLANES_PER_PARUNIT(s) + (plane_num+1)%SSD_PLANES_PER_PARUNIT(s));
}

int ssd_start_cleaning_parunit(int parunit_num, int elem_num, ssd_t *s)
{
    int i;
    int start;

    start = s->elements[elem_num].metadata.parunits[parunit_num].plane_to_clean;
    i = start;
    do {
        ASSERT(parunit_num == s->elements[elem_num].metadata.plane_meta[i].parunit_num);
        if (ssd_start_cleaning(i, elem_num, s)) {
            s->elements[elem_num].metadata.parunits[parunit_num].plane_to_clean = \
                ssd_next_plane_in_parunit(i, parunit_num, elem_num, s);
            return i;
        }

        i = ssd_next_plane_in_parunit(i, parunit_num, elem_num, s);
    } while (i != start);

    return -1;
}

/*
 * invoke cleaning when the number of free blocks drop below a
 * certain threshold (in a plane or an element).
 */
int ssd_start_cleaning(int plane_num, int elem_num, ssd_t *s)
{
    if (plane_num == -1) {
        unsigned int low = (unsigned int)LOW_WATERMARK_PER_ELEMENT(s);
        return (s->elements[elem_num].metadata.tot_free_blocks <= low);
    } else {
        int low = (int)LOW_WATERMARK_PER_PLANE(s);
        return (s->elements[elem_num].metadata.plane_meta[plane_num].free_blocks <= low);
    }
}

/*
 * stop cleaning when sufficient number of free blocks
 * are generated (in a plane or an element).
 */
int ssd_stop_cleaning(int plane_num, int elem_num, ssd_t *s)
{
    if (plane_num == -1) {
        unsigned int high = (unsigned int)HIGH_WATERMARK_PER_ELEMENT(s);
        return (s->elements[elem_num].metadata.tot_free_blocks > high);
    } else {
        int high = (int)HIGH_WATERMARK_PER_PLANE(s);
        return (s->elements[elem_num].metadata.plane_meta[plane_num].free_blocks > high);
    }
}


