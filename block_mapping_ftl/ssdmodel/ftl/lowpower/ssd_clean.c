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

//* writes a page to the current active page. if there is no active page,
// * allocate one and then move.

void block_read_que_create(int bcount, int block_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
	ioreq_event *tmp = (ioreq_event *)getfromextraq();
    ssd_element *elem = &s->elements[elem_num];

#ifdef TDEBUG
	if(block_num == -1){
		printf("error\n");
	}
#endif
	// create a new sub-request for the element
	tmp->devno = s->devno;
	tmp->busno = -1;
	tmp->flags = GC_READ;
	tmp->blkno = block_num;
	tmp->bcount = bcount;//bcount == number of valid page
	tmp->ssd_elem_num = elem_num;
	//ASSERT(tmp->bcount == currdisk->params.page_size);

	elem->metadata.reqs_waiting ++;
	
	// add the request to the corresponding element's queue
	ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	//ssd_activate_elem(s, elem_num);
}

void block_write_que_create(int bcount, int block_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
	ioreq_event *tmp = (ioreq_event *)getfromextraq();
	ssd_element *elem = &s->elements[elem_num];
	
	if(block_num == -1){
		printf("error\n");
	}
	// create a new sub-request for the element
    tmp->devno = s->devno;
    tmp->busno = -1;
    tmp->flags = GC_WRITE;
    tmp->blkno = block_num;
    tmp->bcount = bcount;//bcount == number of valid page
	tmp->ssd_elem_num = elem_num;
    //ASSERT(tmp->bcount == currdisk->params.page_size);

	elem->metadata.reqs_waiting ++;

    // add the request to the corresponding element's queue
	ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	//ssd_activate_elem(s, elem_num);
}

void block_erase_que_create(int blk, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
	ioreq_event *tmp = (ioreq_event *)getfromextraq();
    ssd_element *elem = &s->elements[elem_num];

	if(blk == -1)
		printf("break erase queue\n");
	// create a new sub-request for the element
    tmp->devno = s->devno;
    tmp->busno = -1;
    tmp->flags = GC_ERASE;
    tmp->blkno = blk; //blk = block number
    tmp->bcount = 1; 
	tmp->ssd_elem_num = elem_num;
    //ASSERT(tmp->bcount == currdisk->params.page_size);

	elem->metadata.reqs_waiting ++;

    // add the request to the corresponding element's queue
	ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	//ssd_activate_elem(s, elem_num);
	//Number of clean++;
	s->elements[elem_num].stat.num_clean++;
}

/*
 * reads the data out of a page and writes it back the active page.
 */
void ssd_clean_one_block
(int bcount, int block_num, int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    double cost = 0;
    double xfer_cost = 0;
	ssd_element *elem = &s->elements[elem_num];
    ssd_power_element_stat *power_stat = &(s->elements[elem_num].power_stat);

	//read queue create
	block_read_que_create(bcount, block_num, elem_num, metadata, s);

	//write queue create
	block_write_que_create(bcount, block_num, elem_num, metadata, s);

    // stat -- we move 'valid_pages' out of this block
    s->elements[elem_num].stat.pages_moved += bcount;
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
    if (plane_num == -1) {
        return ssd_compute_avg_lifetime_in_element(elem_num, s);
    } else {
        return ssd_compute_avg_lifetime_in_plane(plane_num, elem_num, s);
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
    if (!ssd_can_clean_block(s, metadata, blk)) {
        return 0;
    }

	if (metadata->block_usage[blk].num_valid > 0){
		return 0;
	}

    return 1;
}

/*
 * migrate data from a cold block to "to_blk"
 */
int ssd_migrate_cold_data(int to_blk, int plane_num, int elem_num, ssd_t *s)
{
    int i;
    int from_blk = -1;
    double oldest_erase_time = simtime;
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
    _ssd_clean_block_fully(to_blk, metadata->block_usage[to_blk].plane_num, elem_num, metadata, s);

#if SSD_ASSERT_ALL
    if (plane_num != -1) {
        f2 = ssd_free_bits(plane_num, elem_num, metadata, s);
        ASSERT(f2 == metadata->plane_meta[metadata->block_usage[to_blk].plane_num].free_blocks);
    }
#endif

    // then, migrate the cold data to the worn out block.
    // for which, we first read all the valid data
    //cost += metadata->block_usage[from_blk].num_valid * s->params.page_read_latency;
    //ssd_power_flash_calculate(SSD_POWER_FLASH_READ, metadata->block_usage[from_blk].num_valid * s->params.page_read_latency, power_stat, s);

    // include the write cost
    //cost += metadata->block_usage[from_blk].num_valid * s->params.page_write_latency;
    //ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, metadata->block_usage[from_blk].num_valid * s->params.page_write_latency, power_stat, s);
    
    // if the src and dest blocks are on different planes
    // include the transfer cost also
	//for(i = 0; i < metadata->block_usage[from_blk].num_valid ; i ++) {
		//cost += ssd_crossover_cost(s, metadata, power_stat, from_blk, to_blk);
	//}

    // the cost of erasing the cold block (represented by from_blk)
    // will be added later ...

    // finally, update the metadata
    metadata->block_usage[to_blk].bsn = metadata->block_usage[from_blk].bsn;
    metadata->block_usage[to_blk].num_valid = metadata->block_usage[from_blk].num_valid;
    metadata->block_usage[from_blk].num_valid = 0;

    for (i = 0; i < s->params.pages_per_block; i ++) {
        int lbn = metadata->block_usage[from_blk].page[i];
        if (lbn != -1) {
            if(metadata->lba_table[lbn] == from_blk);
				metadata->lba_table[lbn] = to_blk;
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

    // stat
    metadata->tot_migrations ++;
    metadata->tot_pgs_migrated += metadata->block_usage[to_blk].num_valid;
    //metadata->mig_cost += cost;

    return from_blk;
}

// return 1 if it is ok to clean this block
int ssd_pick_wear_aware_with_migration
(int blk, int block_life, double avg_lifetime, int plane_num, int elem_num, ssd_t *s)
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
        from_block = ssd_migrate_cold_data(blk, plane_num, elem_num, s);
        printf("Migrating frm blk %d to blk %d (blk life %d avg life %f\n",
          from_block, blk, block_life, avg_lifetime);
    }

    return from_block;
}

/*
 * a greedy solution, where we find the block in a plane with the least
 * num of valid pages and return it.
 */
static int ssd_pick_block_to_clean2(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    double avg_lifetime = 1;
    int i;
    int size;
    int block = -1;
    listnode *greedy_list;

    // find the average life time of all the blocks in this element
    avg_lifetime = ssd_compute_avg_lifetime(plane_num, elem_num, s);

    // we create a list of greedily selected blocks
    ll_create(&greedy_list);
    for (i = 0; i < s->params.blocks_per_element; i ++) {
        if (_ssd_pick_block_to_clean(i, plane_num, elem_num, metadata, s)) {
			ASSERT(i == metadata->block_usage[i].block_num);
			ll_insert_at_head(greedy_list, (void*)&metadata->block_usage[i]);
			block = i;
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
            mig_blk = ssd_pick_wear_aware_with_migration(bm->block_num, bm->rem_lifetime, avg_lifetime, bm->plane_num, elem_num, s);
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

static int ssd_pick_block_to_clean1(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int i;
    int block = -1;
    int min_valid = s->params.pages_per_block - 1; // one page goes for the summary info

    for (i = 0; i < s->params.blocks_per_element; i ++) {
        if (_ssd_pick_block_to_clean(i, plane_num, elem_num, metadata, s)) {
            if (metadata->block_usage[i].num_valid < min_valid) {
                min_valid = metadata->block_usage[i].num_valid;
                block = i;
            }
        }
    }

    if (block != -1) {
        return block;
    } else {
        fprintf(stderr, "Error: we cannot find a block to clean in plane %d\n", plane_num);
        ASSERT(0);
        exit(1);
    }
}

static int ssd_pick_block_to_clean(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    return ssd_pick_block_to_clean2(plane_num, elem_num, metadata, s);
}

/*
 * this routine cleans one block by reading all its valid
 * pages and writing them to the current active block. the
 * cleaned block is also erased.
 */
void _ssd_clean_block_fully(int blk, int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    plane_metadata *pm = &metadata->plane_meta[plane_num];
	ssd_power_element_stat *power_stat = &(s->elements[elem_num].power_stat);

    ASSERT((pm->clean_in_progress == 0) && (pm->clean_in_block = -1));
    pm->clean_in_block = blk;
    pm->clean_in_progress = 1;
	s->elements[elem_num].metadata.clean_in_progress = 1;

	if(blk == -1)
		printf("Break\n");
	block_erase_que_create(blk, elem_num, metadata, s);
    // stat
    pm->num_cleans ++;
    s->elements[elem_num].stat.num_clean ++;
}

static void ssd_clean_block_fully(int plane_num, int elem_num, ssd_t *s)
{
    int blk;
    ssd_element_metadata *metadata = &s->elements[elem_num].metadata;
    plane_metadata *pm = &metadata->plane_meta[plane_num];

    ASSERT((pm->clean_in_progress == 0) && (pm->clean_in_block = -1));

    blk = ssd_pick_block_to_clean(plane_num, elem_num, metadata, s);
    ASSERT(metadata->block_usage[blk].plane_num == plane_num);

    _ssd_clean_block_fully(blk, plane_num, elem_num, metadata, s);
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

/*
 * pick a random block with at least 1 empty page slot and clean it
 */
static double ssd_clean_blocks_random(int plane_num, int elem_num, ssd_t *s)
{
    double cost = 0;
#if 1
    printf("ssd_clean_blocks_random: not yet fixed\n");
    exit(1);
#else
    long blk = 0;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    do {
        // get a random number to select a block
        blk = DISKSIM_lrand48() % s->params.blocks_per_element;

        // if this is plane specific cleaning, then skip all the
        // blocks that don't belong to this plane.
        if ((plane_num != -1) && (metadata->block_usage[blk].plane_num != plane_num)) {
            continue;
        }

        // clean only those blocks that are used.
        if (ssd_can_clean_block(s, metadata, blk)) {

            int valid_pages = metadata->block_usage[blk].num_valid;

            // if all the pages in the block are valid, continue to
            // select another random block
            if (valid_pages == s->params.pages_per_block) {
                continue;
            } else {
                // invoke cleaning until we reach the high watermark
                cost += _ssd_clean_block_fully(blk, elem_num, metadata, s);

                if (ssd_stop_cleaning(plane_num, elem_num, s)) {
                    // we're done with creating enough free blocks. so quit.
                    break;
                }
            }
        } else { // block is already free. so continue.
            continue;
        }
    } while (1);
#endif

    return cost;
}

#define GREEDY_IN_COPYBACK 0

/*
 * first we create a hash table of blocks according to their
 * usage. then we select blocks with the least usage and clean
 * them.
 */
static int ssd_clean_blocks_greedy(int plane_num, int elem_num, ssd_t *s)
{
    double avg_lifetime;
    int i;
    usage_table *table;
	usage_table *entry;
	int clean_invoked = 0;
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

	// get the bucket of blocks with 'i' valid pages
	entry = &(table[0]);

	// free all the blocks with 'i' valid pages
	for (i = 0; i < entry->len; i ++) {
		int blk = entry->block[i];
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
						// skip this block and go to the next on
						continue;
					}
				}
			}

			// okies, finally here we're with the block to be cleaned.
			// invoke cleaning until we reach the high watermark.
			_ssd_clean_block_fully(blk, metadata->block_usage[blk].plane_num, elem_num, metadata, s);
			clean_invoked = 1;

			if (ssd_stop_cleaning(plane_num, elem_num, s)) {
				// no more cleaning is required -- so quit.
				break;
			}
		}
	}

    // release the table
    ssd_release_usage_table(table, s);

    // see if we were able to generate enough free blocks
    if (!ssd_stop_cleaning(plane_num, elem_num, s)) {
        printf("Yuck! we couldn't generate enough free pages in plane %d elem %d ssd %d\n",
            plane_num, elem_num, s->devno);
    }

    return clean_invoked;
}

int ssd_clean_element_no_copyback(int elem_num, ssd_t *s)
{
    int clean_invoked = 0;

    if (!ssd_start_cleaning(-1, elem_num, s)) {
        return clean_invoked;
    }

    switch(s->params.cleaning_policy) {
        case DISKSIM_SSD_CLEANING_POLICY_RANDOM:
            clean_invoked = ssd_clean_blocks_random(-1, elem_num, s);
            break;

        case DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AGNOSTIC:
        case DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE:
            clean_invoked = ssd_clean_blocks_greedy(-1, elem_num, s);
            break;

        default:
            fprintf(stderr, "Error: invalid cleaning policy %d\n",
                s->params.cleaning_policy);
            exit(1);
    }

    return clean_invoked;
}

void ssd_clean_plane_copyback(int plane_num, int elem_num, ssd_t *s)
{
    double cost = 0;

    ASSERT(plane_num != -1);

    switch(s->params.cleaning_policy) {
        case DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AGNOSTIC:
        case DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE:
            ssd_clean_block_fully(plane_num, elem_num, s);
            break;

        case DISKSIM_SSD_CLEANING_POLICY_RANDOM:
        default:
            fprintf(stderr, "Error: invalid cleaning policy %d\n",
                s->params.cleaning_policy);
            exit(1);
    }
}

/*
 * 1. find a plane to clean in each of the parallel unit
 * 2. invoke copyback cleaning on all such planes simultaneously
 */
int ssd_clean_element_copyback(int elem_num, ssd_t *s)
{
    // this means that some (or all) planes require cleaning
    int clean_req = 0;
    int i;
    int plane_to_clean[SSD_MAX_PARUNITS_PER_ELEM];
    ssd_element_metadata *metadata = &s->elements[elem_num].metadata;
    int tot_cleans = 0;

    for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
        if ((plane_to_clean[i] = ssd_start_cleaning_parunit(i, elem_num, s)) != -1) {
            clean_req = 1;
        }
    }

    if (clean_req) {
        for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
            double cleaning_cost = 0;
            int plane_num = plane_to_clean[i];

            if (plane_num == -1) {
                // don't force cleaning
                continue;
            }

            if (metadata->plane_meta[plane_num].clean_in_progress) {
                metadata->plane_meta[plane_num].clean_in_progress = 0;
                metadata->plane_meta[plane_num].clean_in_block = -1;
            }

            //metadata->active_block = metadata->plane_meta[plane_num].active_page / (s->params.pages_per_block-1);
            ssd_clean_plane_copyback(plane_num, elem_num, s);

            tot_cleans ++;

        }
    }

    return clean_req;
}

int ssd_clean_element(ssd_t *s, int elem_num)
{
    int clean_invoked = 0;
    if (s->params.copy_back == SSD_COPY_BACK_DISABLE) {
        clean_invoked = ssd_clean_element_no_copyback(elem_num, s);
    } else {
        clean_invoked = ssd_clean_element_copyback(elem_num, s);
    }
    return clean_invoked;
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


