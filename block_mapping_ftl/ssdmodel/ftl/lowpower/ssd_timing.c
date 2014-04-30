// DiskSim SSD support
// 2008 Microsoft Corporation. All Rights Reserved

#include "ssd.h"
#include "ssd_timing.h"
#include "ssd_clean.h"
#include "ssd_gang.h"
#include "ssd_utils.h"
#include "modules/ssdmodel_ssd_param.h"
//@20090831-Micky::added
#include "ssd_power.h"
//--

struct my_timing_t {
    ssd_timing_params    *params;
    int next_write_page[SSD_MAX_ELEMENTS];
};

int ssd_choose_element(void *user_params, int blkno)
{
    struct my_timing_t *tt = (struct my_timing_t *) user_params;
    return (blkno/(tt->params->element_stride_pages*tt->params->page_size)) % tt->params->nelements;
}

int ssd_choose_aligned_count(int page_size, int blkno, int count)
{
    int res = page_size - (blkno % page_size);
    if (res > count)
        res = count;
    return res;
}

double ssd_read_policy_simple(int count, ssd_t *s, ssd_power_element_stat *power_stat)
{
    double cost = 0;
    double cost2 = 0;

    cost = s->params.page_read_latency;
    ssd_power_flash_calculate(SSD_POWER_FLASH_READ, cost, power_stat, s);
    
    cost2 = ssd_data_transfer_cost(s, count);
    ssd_power_flash_calculate(SSD_POWER_FLASH_BUS_DATA_TRANSFER, cost2, power_stat, s);

    return cost + cost2;
}

//////////////////////////////////////////////////////////////////////////////
//                OSR algorithm for writing and cleaning flash
//////////////////////////////////////////////////////////////////////////////

/*
 * returns a count of the number of free bits in a plane.
 */
int ssd_free_bits(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int i;
    int freecount = 0;
    int start = plane_num * s->params.blocks_per_plane;
    for (i = start; i < start + (int)s->params.blocks_per_plane; i ++) {
        if (!ssd_bit_on(metadata->free_blocks, i)) {
            freecount ++;
        }
    }

    return freecount;
}

/*
 * makes sure that the number of free blocks is equal to the number
 * of free bits in the bitmap block.
 */
void ssd_assert_plane_freebits(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int f1;

    f1 = ssd_free_bits(plane_num, elem_num, metadata, s);
    ASSERT(f1 == metadata->plane_meta[plane_num].free_blocks);
}

/*
 * just to make sure that the free blocks maintained across various
 * metadata structures are the same.
 */
void ssd_assert_free_blocks(ssd_t *s, ssd_element_metadata *metadata)
{
    int tmp = 0;
    int i;

    //assertion
    for (i = 0; i < s->params.planes_per_pkg; i ++) {
        tmp += metadata->plane_meta[i].free_blocks;
    }
    ASSERT(metadata->tot_free_blocks == tmp);
}

/*
 * make sure that the following invariant holds always
 */
void ssd_assert_page_version(int prev_page, int active_page, ssd_element_metadata *metadata, ssd_t *s)
{
    int prev_block = prev_page / s->params.pages_per_block;
    int active_block = active_page / s->params.pages_per_block;

    if (prev_block != active_block) {
        ASSERT(metadata->block_usage[prev_block].bsn < metadata->block_usage[active_block].bsn);
    } else {
        ASSERT(prev_page < active_page);
    }
}

void ssd_assert_valid_pages(int plane_num, ssd_element_metadata *metadata, ssd_t *s)
{
#if SSD_ASSERT_ALL
    int i;
    int block_valid_pages = 0;
    int start_bitpos = plane_num * s->params.blocks_per_plane;

    for (i = start_bitpos; i < (start_bitpos + (int)s->params.blocks_per_plane); i ++) {
        int block = ssd_bitpos_to_block(i, s);
        block_valid_pages += metadata->block_usage[block].num_valid;
    }

    ASSERT(block_valid_pages == metadata->plane_meta[plane_num].valid_pages);
#else
    return;
#endif
}

double ssd_data_transfer_cost(ssd_t *s, int sectors_count)
{
    return (sectors_count * (SSD_BYTES_PER_SECTOR) * s->params.chip_xfer_latency);
}

int ssd_last_page_in_block(int page_num, ssd_t *s)
{
    //return (((page_num + 1) % s->params.pages_per_block) == 0);
	return 1;
}

/*
 * returns the logical page number within an element given a block number as
 * issued by the file system
 */
int ssd_logical_blockno(int blkno, ssd_t *s)
{
    int apn, abn;
    int lbn;

    // absolute page number is the block number as written by the above layer
    apn = blkno/s->params.page_size;
    abn = apn/(s->params.pages_per_block-1);
    
    // find the logical page number within the ssd element. we maintain the
    // mapping between the logical page number and the actual physical page
    // number. an alternative is that we could maintain the mapping between
    // apn we calculated above and the physical page number. but the range
    // of apn is several times bigger and so we chose to go with the mapping
    // b/w lbn --> physical page number
	lbn = ((abn - (abn % (s->params.element_stride_pages * s->params.nelements)))/
                      s->params.nelements);

    return lbn;
}

int ssd_bitpos_to_block(int bitpos, ssd_t *s)
{
    int block = -1;

    switch(s->params.plane_block_mapping) {
        case PLANE_BLOCKS_CONCAT:
            block = bitpos;
            break;

        case PLANE_BLOCKS_PAIRWISE_STRIPE:
        {
            int plane_pair = bitpos / (s->params.blocks_per_plane*2);
            int stripe_no = (bitpos/2) % s->params.blocks_per_plane;
            int offset = bitpos % 2;

            block = plane_pair * (s->params.blocks_per_plane*2)  + \
                                                        (stripe_no * 2) + \
                                                        offset;
        }
        break;

        case PLANE_BLOCKS_FULL_STRIPE:
        {
            int stripe_no = bitpos % s->params.blocks_per_plane;
            int col_no = bitpos / s->params.blocks_per_plane;
            block = (stripe_no * s->params.planes_per_pkg) + col_no;
        }
        break;

        default:
            fprintf(stderr, "Error: unknown plane_block_mapping %d\n", s->params.plane_block_mapping);
            exit(1);
    }

    return block;
}

int ssd_block_to_bitpos(ssd_t *currdisk, int block)
{
    int bitpos = -1;

    // mark the bit position as allocated.
    // we need to first find the bit that corresponds to the block
    switch(currdisk->params.plane_block_mapping) {
        case PLANE_BLOCKS_CONCAT:
            bitpos = block;
        break;

        case PLANE_BLOCKS_PAIRWISE_STRIPE:
        {
            int block_index = block % (2*currdisk->params.blocks_per_plane);
            int pairs_to_skip = block / (2*currdisk->params.blocks_per_plane);
            int planes_to_skip = block % 2;
            int blocks_to_skip = block_index / 2;

            bitpos = pairs_to_skip * (2*currdisk->params.blocks_per_plane) + \
                        planes_to_skip * currdisk->params.blocks_per_plane + \
                        blocks_to_skip;
        }
        break;

        case PLANE_BLOCKS_FULL_STRIPE:
        {
            int planes_to_skip = block % currdisk->params.planes_per_pkg;
            int blocks_to_skip = block / currdisk->params.planes_per_pkg;
            bitpos = planes_to_skip * currdisk->params.blocks_per_plane + blocks_to_skip;
        }
        break;

        default:
            fprintf(stderr, "Error: unknown plane_block_mapping %d\n",
                currdisk->params.plane_block_mapping);
            exit(1);
    }

    ASSERT((bitpos >= 0) && (bitpos < currdisk->params.blocks_per_element));

    return bitpos;
}

int ssd_bitpos_to_plane(int bitpos, ssd_t *s)
{
    return (bitpos / s->params.blocks_per_plane);
}

/*
 * element stat update 
 */
void ssd_elem_stat_upate(ssd_t *s, ssd_element_metadata *metadata, int active_block, int prev_block)
{
	if(prev_block == -1) {
		int active_plane = metadata->block_usage[active_block].plane_num;
		//if this is new write increment the usage count on the active block
		metadata->block_usage[active_block].num_valid++;
		metadata->plane_meta[active_plane].valid_pages++;
	}
	else {
		//we init prev_block stat 
		//we don't touch page table
		int prev_plane = metadata->block_usage[prev_block].plane_num;
		metadata->plane_meta[prev_plane].valid_pages -= metadata->block_usage[prev_block].num_valid;
		metadata->block_usage[prev_block].num_valid = 0;
		metadata->block_usage[prev_block].state = SSD_BLOCK_SEALED;
	}   
}

double _ssd_write_page_new(ssd_t *s, ssd_element_metadata *metadata, int lbn, ssd_power_element_stat *power_stat, int offset)
{
    double cost;
	unsigned int write_block = metadata->lba_table[lbn];
    unsigned int write_plane = metadata->block_usage[write_block].plane_num;
	double min, max;
	//metadata update
	metadata->block_usage[write_block].page[offset] = lbn;
	ssd_elem_stat_upate(s, metadata, write_block, -1);

    //add cost
	//가정:write latency는 minimum 소수 첫째짜리까지의 값이다
	/*min = s->params.page_write_latency * 10;
	max = s->params.page_write_max_latency * 10;

	cost = (rand()%(int)(max-min+1)+min)/10;*/
    cost = s->params.page_write_latency;
    ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, cost, power_stat, s);

    // some sanity checking
    if (metadata->block_usage[write_block].num_valid >= s->params.pages_per_block) {
        fprintf(stderr, "Error: len %d of block %d is greater than or equal to pages per block %d\n",
            metadata->block_usage[write_block].num_valid, write_block, s->params.pages_per_block);
        exit(1);
    }

    return cost;
}

/*
 * writes a single page into the active block of a ssd element.
 * this code assumes that there is a valid active page where the write can go
 * without invoking new cleaning.
 */
double _ssd_write_block_osr(ssd_t *s, ssd_element_metadata *metadata, int lbn, ssd_power_element_stat *power_stat, int blkno)
{
    double cost = 0;
    unsigned int active_block = metadata->active_block;
    unsigned int active_plane = metadata->block_usage[active_block].plane_num;

	int prev_num_valid = 0;
	int i = 0;
	int elem_num = ssd_choose_element(s->user_params, blkno);
	unsigned int prev_block = metadata->lba_table[lbn];
	unsigned int prev_plane = metadata->block_usage[prev_block].plane_num;
	int apn = blkno/s->params.page_size;
	int offset = apn/s->params.nelements%(s->params.pages_per_block-1);

	//we write request page in new block
	//first, update lbn
	metadata->lba_table[lbn] = active_block;
	cost = _ssd_write_page_new(s, metadata, lbn, power_stat, offset);

	//stat update
	metadata->block_usage[prev_block].page[offset] = -1;
	ssd_elem_stat_upate(s, metadata, active_block, prev_block);

	//Move One Page
	for(i = 0 ; i < s->params.pages_per_block ; i++) {
		/*int r_blkno = blkno;
		r_blkno = blkno - (offset * s->params.page_size * s->params.nelements);*/
		if((metadata->block_usage[prev_block].page[i] != -1) && (i != offset)) {
			//r_blkno += (i * s->params.page_size * s->params.nelements);
			prev_num_valid++;
			//clean prev_block's page table
			metadata->block_usage[prev_block].page[i] = -1;
			//s->elements[elem_num].stat.pages_moved ++;
			//metadata update
			metadata->block_usage[active_block].page[i] = lbn;
			ssd_elem_stat_upate(s, metadata, active_block, -1);
		}
	}

	if(prev_num_valid != 0){
		ssd_clean_one_block(prev_num_valid, prev_block, prev_plane, elem_num, metadata, s);
	}else{
		//prev_block erase
		block_erase_que_create(prev_block, elem_num, metadata, s);
	}

	//activate NAND chip after all gc event creating
	//ssd_activate_elem(s, elem_num);

    // some sanity checking
    if (metadata->block_usage[active_block].num_valid >= s->params.pages_per_block) {
        fprintf(stderr, "Error: len %d of block %d is greater than or equal to pages per block %d\n",
            metadata->block_usage[active_block].num_valid, active_block, s->params.pages_per_block);
        exit(1);
    }

    return cost;
}

//block_erase_opeeration
double _ssd_erase_block_osr(ssd_t *s, ssd_element_metadata *metadata, int block, int plane_num, ssd_power_element_stat *power_stat)
{
	double cost =0.0;

	if ((metadata->block_usage[block].num_valid == 0)&&(metadata->block_usage[block].state == SSD_BLOCK_SEALED)) {
		// add the cost of erasing the block
		//add the power consumption of the erase
		plane_metadata *pm = &metadata->plane_meta[plane_num];

		cost = s->params.block_erase_latency;
		ssd_power_flash_calculate(SSD_POWER_FLASH_ERASE, cost, power_stat, s);

        ssd_update_free_block_status(block, plane_num, metadata, s);
		ssd_update_block_lifetime(simtime+s->params.block_erase_latency, block, metadata);
		pm->clean_in_progress = 0;
        pm->clean_in_block = -1;
    }

	return cost;
}

/*
 * description: this routine finds the next free block inside a ssd
 * element/plane and returns it for writing. this routine can be called
 * during cleaning and therefore, must be guaranteed to find a free block
 * without invoking any further cleaning (otherwise there will be a
 * circular dep). we can ensure this by invoking the cleaning algorithm
 * when the num of free blocks is high enough to help in cleaning.
 */
void _ssd_alloc_active_block(int plane_num, int elem_num, ssd_t *s)
{
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);
    unsigned char *free_blocks = metadata->free_blocks;
    int active_block = -1;
    int prev_pos;
    int bitpos;

    if (plane_num != -1) {
        prev_pos = metadata->plane_meta[plane_num].block_alloc_pos;
    } else {
        prev_pos = metadata->block_alloc_pos;
    }

    // find a free bit
    bitpos = ssd_find_zero_bit(free_blocks, s->params.blocks_per_element, prev_pos);
    ASSERT((bitpos >= 0) && (bitpos < s->params.blocks_per_element));

    // check if we found the free bit in the plane we wanted to
    if (plane_num != -1) {
        if (ssd_bitpos_to_plane(bitpos, s) != plane_num) {

            //printf("Error: We wanted a free block in plane %d but found another in %d\n",
            //  plane_num, ssd_bitpos_to_plane(bitpos, s));
            //printf("So, starting the search again for plane %d\n", plane_num);

            // start from the beginning
            metadata->plane_meta[plane_num].block_alloc_pos = plane_num * s->params.blocks_per_plane;
            prev_pos = metadata->plane_meta[plane_num].block_alloc_pos;
            bitpos = ssd_find_zero_bit(free_blocks, s->params.blocks_per_element, prev_pos);
            ASSERT((bitpos >= 0) && (bitpos < s->params.blocks_per_element));

            if (ssd_bitpos_to_plane(bitpos, s) != plane_num) {
                printf("Error: this plane %d is full\n", plane_num);
                printf("this case is not yet handled\n");
                exit(1);
            }
        }

        metadata->plane_meta[plane_num].block_alloc_pos = \
            (plane_num * s->params.blocks_per_plane) + ((bitpos+1) % s->params.blocks_per_plane);
    } else {
        metadata->block_alloc_pos = (bitpos+1) % s->params.blocks_per_element;
    }

    // find the block num
    active_block = ssd_bitpos_to_block(bitpos, s);

    if (active_block != -1) {
        plane_metadata *pm;

        // make sure we're doing the right thing
        ASSERT(metadata->block_usage[active_block].plane_num == ssd_bitpos_to_plane(bitpos, s));
        ASSERT(metadata->block_usage[active_block].bsn == 0);
        ASSERT(metadata->block_usage[active_block].num_valid == 0);
        ASSERT(metadata->block_usage[active_block].state == SSD_BLOCK_CLEAN);

        if (plane_num == -1) {
            plane_num = metadata->block_usage[active_block].plane_num;
        } else {
            ASSERT(plane_num == metadata->block_usage[active_block].plane_num);
        }

        pm = &metadata->plane_meta[plane_num];

        // reduce the total number of free blocks
        metadata->tot_free_blocks --;
        pm->free_blocks --;

        //assertion
        ssd_assert_free_blocks(s, metadata);

        // allocate the block
        ssd_set_bit(free_blocks, bitpos);
        metadata->block_usage[active_block].state = SSD_BLOCK_INUSE;
        metadata->block_usage[active_block].bsn = metadata->bsn ++;

        // start from the first page of the active block
        pm->active_page = active_block * s->params.pages_per_block;
        metadata->active_block = active_block;

        //ssd_assert_plane_freebits(plane_num, elem_num, metadata, s);
    } else {
        fprintf(stderr, "Error: cannot find a free block in ssd element %d\n", elem_num);
        exit(-1);
    }
}

listnode **ssd_pick_parunits(ssd_req **reqs, int total, int elem_num, ssd_element_metadata *metadata, ssd_t *s)
{
    int i;
    int lbn;
    int prev_page;
    int prev_block;
    int plane_num;
    int parunit_num;
    listnode **parunits;
    int filled = 0;

    // parunits is an array of linked list structures, where
    // each entry in the array corresponds to one parallel unit
    parunits = (listnode **)malloc(SSD_PARUNITS_PER_ELEM(s) * sizeof(listnode *));
    for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
        ll_create(&parunits[i]);
    }

    // first, fill in the reads
    for (i = 0; i < total; i ++) {
		if (reqs[i]->is_gc) {
			prev_block = reqs[i]->blk;
			plane_num = metadata->block_usage[prev_block].plane_num;
            parunit_num = metadata->plane_meta[plane_num].parunit_num;
            reqs[i]->plane_num = plane_num;
			ll_insert_at_tail(parunits[parunit_num], (void*)reqs[i]);
            filled ++;
		}else if (reqs[i]->is_read) {
            // get the logical page number corresponding to this blkno
            lbn = ssd_logical_blockno(reqs[i]->blk, s);
            /*prev_page = metadata->lba_table[lbn];
            ASSERT(prev_page != -1);
            prev_block = SSD_PAGE_TO_BLOCK(prev_page, s);*/
			prev_block = metadata->lba_table[lbn];
			ASSERT(prev_block != -1);
            plane_num = metadata->block_usage[prev_block].plane_num;
            parunit_num = metadata->plane_meta[plane_num].parunit_num;
            reqs[i]->plane_num = plane_num;
            ll_insert_at_tail(parunits[parunit_num], (void*)reqs[i]);
            filled ++;
        }
    }

    // if all the reqs are reads, return
    if (filled == total) {
        return parunits;
    }

    for (i = 0; i < total; i ++) {
		if(reqs[i]->is_gc){
			prev_block = reqs[i]->blk;
			plane_num = metadata->block_usage[prev_block].plane_num;
            parunit_num = metadata->plane_meta[plane_num].parunit_num;
            reqs[i]->plane_num = plane_num;
			ll_insert_at_tail(parunits[parunit_num], (void*)reqs[i]);
            filled ++;
		}else if (reqs[i]->is_write) {        // we need to find planes for the writes
            int j;
            int prev_bsn = -1;
            int min_valid;

            plane_num = -1;
            lbn = ssd_logical_blockno(reqs[i]->blk, s);
            prev_block = metadata->lba_table[lbn];
			prev_page = prev_block * s->params.pages_per_block;
            ASSERT(prev_page != -1);
            prev_bsn = metadata->block_usage[prev_block].bsn;

            if (s->params.alloc_pool_logic == SSD_ALLOC_POOL_PLANE) {
                plane_num = metadata->block_usage[prev_block].plane_num;
            } else {
                // find a plane with the max no of free blocks
                j = metadata->plane_to_write;
                do {
                    int active_block;
                    int active_bsn;
                    plane_metadata *pm = &metadata->plane_meta[j];

                    active_block = SSD_PAGE_TO_BLOCK(pm->active_page, s);
                    active_bsn = metadata->block_usage[active_block].bsn;

                    // see if we can write to this block
                    if ((active_bsn > prev_bsn) ||
                        ((active_bsn == prev_bsn) && (pm->active_block > (unsigned int)prev_block))) {
                        int free_pages_in_act_blk;
                        int k;
                        int p;
                        int tmp;
                        int size;
                        int additive = 0;

                        p = metadata->plane_meta[j].parunit_num;
                        size = ll_get_size(parunits[p]);

                        // check if this plane has been already selected for writing
                        // in this parallel unit
                        for (k = 0; k < size; k ++) {
                            ssd_req *r;
                            listnode *n = ll_get_nth_node(parunits[p], k);
                            ASSERT(n != NULL);

                            r = ((ssd_req *)n->data);

                            // is this plane has been already selected for writing?
                            if ((r->plane_num == j) &&
                                (!r->is_read)) {
                                additive ++;
                            }
                        }

                        // select a plane with the most no of free pages
                        free_pages_in_act_blk = s->params.pages_per_block - ((pm->active_page%s->params.pages_per_block) + additive);
                        tmp = pm->free_blocks * s->params.pages_per_block + free_pages_in_act_blk;

                        if (plane_num == -1) {
                            // this is the first plane that satisfies the above criterion
                            min_valid = tmp;
                            plane_num = j;
                        } else {
                            if (min_valid < tmp) {
                                min_valid = tmp;
                                plane_num = j;
                            }
                        }
                    }

                    // check out the next plane
                    j = (j+1) % s->params.planes_per_pkg;
                } while (j != metadata->plane_to_write);
            }

            if (plane_num != -1) {
                // start searching from the next plane
                metadata->plane_to_write = (plane_num+1) % s->params.planes_per_pkg;

                reqs[i]->plane_num = plane_num;
                parunit_num = metadata->plane_meta[plane_num].parunit_num;
                ll_insert_at_tail(parunits[parunit_num], (void *)reqs[i]);
                filled ++;
            } else {
                fprintf(stderr, "Error: cannot find a plane to write\n");
                exit(1);
            }
        }
    }

	// last, fill in the erases
    for (i = 0; i < total; i ++) {
		if (reqs[i]->org_req->flags == GC_ERASE) {
            //get plane number
			//erase queue's blk = block number
			plane_num = metadata->block_usage[reqs[i]->blk].plane_num;
            parunit_num = metadata->plane_meta[plane_num].parunit_num;
            reqs[i]->plane_num = plane_num;
            ll_insert_at_tail(parunits[parunit_num], (void*)reqs[i]);
            filled ++;
        }
    }

    ASSERT(filled == total);

    return parunits;
}

static double ssd_issue_overlapped_ios(ssd_req **reqs, int total, int elem_num, ssd_t *s)
{
    double max_cost = 0;
    double parunit_op_cost[SSD_MAX_PARUNITS_PER_ELEM];
    double parunit_tot_cost[SSD_MAX_PARUNITS_PER_ELEM];
    ssd_element_metadata *metadata;
    ssd_power_element_stat *power_stat;
    
    int lbn;
    int i;
    int read_cycle = 0;
    listnode **parunits;

    // all the requests must be of the same type
    for (i = 1; i < total; i ++) {
        ASSERT(reqs[i]->is_read == reqs[0]->is_read);
    }

    // is this a set of read requests?
    if (reqs[0]->is_read) {
        read_cycle = 1;
    }

    memset(parunit_tot_cost, 0, sizeof(double)*SSD_MAX_PARUNITS_PER_ELEM);

    // find the planes to which the reqs are to be issued
    metadata = &(s->elements[elem_num].metadata);
    power_stat = &(s->elements[elem_num].power_stat);
    parunits = ssd_pick_parunits(reqs, total, elem_num, metadata, s);

    // repeat until we've served all the requests
    while (1) {
		double read_xfer_cost = 0.0;
		double write_xfer_cost = 0.0;
        double max_op_cost = 0;
        int active_parunits = 0;
        int op_count = 0;

        // do we still have any request to service?
        for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
            if (ll_get_size(parunits[i]) > 0) {
                active_parunits ++;
            }
        }

        // no more requests -- get out
        if (active_parunits == 0) {
            break;
        }

        // clear this arrays for storing costs
        memset(parunit_op_cost, 0, sizeof(double)*SSD_MAX_PARUNITS_PER_ELEM);

        // begin a round of serving. we serve one request per
        // parallel unit. if an unit has more than one request
        // in the list, they have to be serialized.
        max_cost = 0;
        for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
            int size;

            size = ll_get_size(parunits[i]);
            if (size > 0) {
                // this parallel unit has a request to serve
                ssd_req *r;
                listnode *n = ll_get_nth_node(parunits[i], 0);

                op_count ++;
                ASSERT(op_count <= active_parunits);

                // get the request
                r = (ssd_req *)n->data;
				if(!r->is_gc){
					lbn = ssd_logical_blockno(r->blk, s);

					if (r->is_read) {
						parunit_op_cost[i] = s->params.page_read_latency;
						//Micky
						ssd_power_flash_calculate(SSD_POWER_FLASH_READ, s->params.page_read_latency, power_stat, s);
						read_xfer_cost += ssd_data_transfer_cost(s,r->count);
					} else {
						int plane_num = r->plane_num;
						int block = metadata->lba_table[lbn];
						int apn = r->blk/s->params.page_size;
						int offset = apn/s->params.nelements%(s->params.pages_per_block-1);

						// issue the write to the current active page.
						// we need to transfer the data across the serial pins for write.
						metadata->active_block = metadata->plane_meta[plane_num].active_page/s->params.pages_per_block;
						//printf("elem %d plane %d ", elem_num, plane_num);
						
						if((metadata->block_usage[block].page[offset] == -1)) {
							parunit_op_cost[i] = _ssd_write_page_new(s, metadata, lbn, power_stat, offset);
						}
						else {
							parunit_op_cost[i] = 0.0;
							parunit_op_cost[i] = _ssd_write_block_osr(s, metadata, lbn, power_stat, r->blk);
							_ssd_alloc_active_block(plane_num, elem_num, s);
						}
						write_xfer_cost += ssd_data_transfer_cost(s,r->count);
					} 
				}else {
					if(r->is_read){
						int plane_num = r->plane_num;
						parunit_op_cost[i] = s->params.page_read_latency * r->count;
						ssd_power_flash_calculate(SSD_POWER_FLASH_READ, parunit_op_cost[i], power_stat, s);
						power_stat->num_reads += r->count - 1 ;
						//read_xfer_cost += ssd_data_transfer_cost(s,s->params.page_size)*r->count;
					}else if(r->is_write){
						int plane_num = r->plane_num;
						parunit_op_cost[i] = s->params.page_write_latency * r->count;
						ssd_power_flash_calculate(SSD_POWER_FLASH_WRITE, parunit_op_cost[i], power_stat, s);
						power_stat->num_writes += r->count - 1 ;
						parunit_op_cost[i] += _ssd_erase_block_osr(s, metadata, r->blk, plane_num, power_stat);
						//write_xfer_cost += ssd_data_transfer_cost(s,s->params.page_size)*r->count;
					}else{
						int plane_num = r->plane_num;
						parunit_op_cost[i] = _ssd_erase_block_osr(s, metadata, r->blk, plane_num, power_stat);
					}
				}

                ASSERT(r->count <= s->params.page_size);

                // calc the cost: the access time should be something like this  (interleaving 고려)
                // for read
                if (read_cycle) {
                    if (SSD_PARUNITS_PER_ELEM(s) > 4) {
                        printf("modify acc time here ...\n");
                        ASSERT(0);
                    }
                    if (op_count == 1) {
						r->acctime = parunit_op_cost[i] + read_xfer_cost;
                        r->schtime = parunit_tot_cost[i] + r->acctime;
                    } else {
						r->acctime = ssd_data_transfer_cost(s,r->count);
                        r->schtime = parunit_tot_cost[i] + read_xfer_cost + parunit_op_cost[i];
                    }
				} else if (r->is_write){
                    // for write
                    r->acctime = parunit_op_cost[i] + ssd_data_transfer_cost(s,r->count);
                    r->schtime = parunit_tot_cost[i] + write_xfer_cost + parunit_op_cost[i];
				} else {
					// for erase
					r->acctime = parunit_op_cost[i];
                    r->schtime = parunit_tot_cost[i] + r->acctime;
				}


                // find the maximum cost for this round of operations
                if (max_cost < r->schtime) {
                    max_cost = r->schtime;
                }

                // release the node from the linked list
                ll_release_node(parunits[i], n);
            }
        }

		//add bus power
		ssd_power_flash_calculate(SSD_POWER_FLASH_BUS_DATA_TRANSFER, read_xfer_cost, power_stat, s);
		ssd_power_flash_calculate(SSD_POWER_FLASH_BUS_DATA_TRANSFER, write_xfer_cost, power_stat, s);

        // we can start the next round of operations only after all
        // the operations in the first round are over because we're
        // limited by the one set of pins to all the parunits
        for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
            parunit_tot_cost[i] = max_cost;
        }
    }

    for (i = 0; i < SSD_PARUNITS_PER_ELEM(s); i ++) {
        ll_release(parunits[i]);
    }
    free(parunits);

	power_stat->acc_time += max_cost;

    return max_cost;
}

static double ssd_write_one_active_page(int blkno, int count, int elem_num, ssd_t *s)
{
    double cost = 0;
    int cleaning_invoked = 0;
    ssd_element_metadata *metadata;
    ssd_power_element_stat *power_stat;
    int lbn;
	int offset;
	int block;
	int apn;

    metadata = &(s->elements[elem_num].metadata);
    power_stat = &(s->elements[elem_num].power_stat);

    // get the logical page number corresponding to this blkno
    lbn = ssd_logical_blockno(blkno, s);
	apn = blkno/s->params.page_size;
	offset = apn/s->params.nelements%(s->params.pages_per_block-1);
	
    // see if there are any free pages left inside the active block.
    // as per the osr design, the last page is used as a summary page.
    // so if the active_page is already pointing to the summary page,
    // then we need to find another free block to act as active block.
    if (ssd_last_page_in_block(metadata->active_block, s)) {

        // do we need to create more free blocks for future writes?
        if (ssd_start_cleaning(-1, elem_num, s)) {

            printf ("We should not clean here ...\n");
            ASSERT(0);

            // if we're cleaning in the background, this should
            // not get executed
            if (s->params.cleaning_in_background) {
                exit(1);
            }

            cleaning_invoked = 1;
            cost += ssd_clean_element_no_copyback(elem_num, s);
        }

        // if we had invoked the cleaning, we must again check if we
        // need an active block before allocating one. this check is
        // needed because the above cleaning procedure might have
        // allocated new active blocks during the process of cleaning,
        // which might still have free pages for writing.
        if (!cleaning_invoked ||
            ssd_last_page_in_block(metadata->active_block, s)) {
            _ssd_alloc_active_block(-1, elem_num, s);
        }
    }

	// issue the write to the current active page
	block = metadata->lba_table[lbn];
	if(metadata->block_usage[block].page[offset] == -1) {
		cost += _ssd_write_page_new(s, metadata, lbn, power_stat, offset);
	}
	else {
		cost += _ssd_write_block_osr(s, metadata, lbn, power_stat, blkno);
	}
    cost += ssd_data_transfer_cost(s, count);
    ssd_power_flash_calculate(SSD_POWER_FLASH_BUS_DATA_TRANSFER, ssd_data_transfer_cost(s,s->params.page_size), power_stat, s);

    return cost;
}

/*
 * computes the time taken to read/write data in a flash element
 * using just one active page.
 */
static void ssd_compute_access_time_one_active_page
(ssd_req **reqs, int total, int elem_num, ssd_t *s)
{
    // we assume that requests have been broken down into page sized chunks
    double cost;
    int blkno;
    int count;
    int is_read;
    ssd_power_element_stat *power_stat = &(s->elements[elem_num].power_stat);

    // we can serve only one request at a time
    ASSERT(total == 1);

    blkno = reqs[0]->blk;
    count = reqs[0]->count;
    is_read = reqs[0]->is_read;

    if (is_read) {
        cost = ssd_read_policy_simple(count, s, power_stat);
        
    } else {
        cost = ssd_write_one_active_page(blkno, count, elem_num, s);
    }

    reqs[0]->acctime = cost;
    reqs[0]->schtime = cost;
}

/*
 * with multiple planes, we can support either single active page or multiple
 * active pages.
 *
 * one-active-page-per-element: we can just have one active page per
 * element and move this active page to different blocks whenever a block
 * gets full. this is the basic osr design.
 *
 * one-active-page-per-plane: since each ssd element consists of multiple
 * parallel planes, a more dynamic design would have one active page
 * per plane. but this slightly complicates the design due to page versions.
 *
 * page versions are identified using a block sequence number (bsn) and page
 * number. bsn is a monotonically increasing num stored in the first page of
 * each block. so, if a same logical page 'p' is stored on two different physical
 * pages P and P', we find the latest version by comparing <BSN(P), P>
 * and <BSN(P'), P'>. in the simple, one-active-page-per-element design
 * whenever a block gets full and the active page is moved to a different
 * block, the bsn is increased. this way, we can always ensure that the latest
 * copy of a page will have its <BSN, P'> higher than its previous versions
 * (because BSN is monotonically increasing and physical pages in a block are
 * written only in increasing order). however, with multiple active pages say,
 * P0 to Pi with their corresponding bsns BSN0 to BSNi, we have to make sure
 * that the invariant is still maintained.
 */
void ssd_compute_access_time(ssd_t *s, int elem_num, ssd_req **reqs, int total)
{
    if (total == 0) {
        return;
    }

    if (s->params.copy_back == SSD_COPY_BACK_DISABLE) {
        ssd_compute_access_time_one_active_page(reqs, total, elem_num, s);
    } else {
        ssd_issue_overlapped_ios(reqs, total, elem_num, s);
    }
}

void * ssd_new_timing_t(ssd_timing_params *params)
{
    int i;
    struct my_timing_t *tt = malloc(sizeof(struct my_timing_t));
    tt->params = params;
    for (i=0; i<SSD_MAX_ELEMENTS; i++)
        tt->next_write_page[i] = -1;
    return tt;
}

