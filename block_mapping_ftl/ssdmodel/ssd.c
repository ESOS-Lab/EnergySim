// DiskSim SSD support
// 2008 Microsoft Corporation. All Rights Reserved

#include "ssd.h"
#include "ftl.h"
#include "ssd_gang.h"
#include "modules/ssdmodel_ssd_param.h"

#include "disksim_stat.h"


static void ssd_request_complete(ioreq_event *curr);
static void ssd_media_access_request(ioreq_event *curr);

struct ssd *getssd (int devno)
{
   struct ssd *s;
   ASSERT1((devno >= 0) && (devno < MAXDEVICES), "devno", devno);

   s = disksim->ssdinfo->ssds[devno];
   return (disksim->ssdinfo->ssds[devno]);
}

void ssd_assert_current_activity(ssd_t *currdisk, ioreq_event *curr)
{
    assert(currdisk->channel_activity != NULL &&
        currdisk->channel_activity->devno == curr->devno &&
        currdisk->channel_activity->blkno == curr->blkno &&
        currdisk->channel_activity->bcount == curr->bcount);
}

/*
 * ssd_send_event_up_path()
 *
 * Acquires the bus (if not already acquired), then uses bus_delay to
 * send the event up the path.
 *
 * If the bus is already owned by this device or can be acquired
 * immediately (interleaved bus), the event is sent immediately.
 * Otherwise, ssd_bus_ownership_grant will later send the event.
 */
static void ssd_send_event_up_path (ioreq_event *curr, double delay)
{
   ssd_t *currdisk;
   int busno;
   int slotno;

   // fprintf (outputfile, "ssd_send_event_up_path - devno %d, type %d, cause %d, blkno %d\n", curr->devno, curr->type, curr->cause, curr->blkno);

   currdisk = getssd (curr->devno);

   ssd_assert_current_activity(currdisk, curr);

   busno = ssd_get_busno(curr);
   slotno = currdisk->slotno[0];

   /* Put new request at head of buswait queue */
   curr->next = currdisk->buswait;
   currdisk->buswait = curr;

   curr->tempint1 = busno;
   curr->time = delay;
   if (currdisk->busowned == -1) {

      // fprintf (outputfile, "Must get ownership of the bus first\n");

      if (curr->next) {
         //fprintf(stderr,"Multiple bus requestors detected in ssd_send_event_up_path\n");
         /* This should be ok -- counting on the bus module to sequence 'em */
      }
      if (bus_ownership_get(busno, slotno, curr) == FALSE) {
         /* Remember when we started waiting (only place this is written) */
         currdisk->stat.requestedbus = simtime;
      } else {
         currdisk->busowned = busno;
         bus_delay(busno, DEVICE, curr->devno, delay, curr); /* Never for SCSI */
      }
   } else if (currdisk->busowned == busno) {

      //fprintf (outputfile, "Already own bus - so send it on up\n");
      bus_delay(busno, DEVICE, curr->devno, delay, curr);
   } else {
      fprintf(stderr, "Wrong bus owned for transfer desired\n");
      exit(1);
   }
}

/* The idea here is that only one request can "possess" the channel back to the
   controller at a time. All others are enqueued on queue of pending activities.
   "Completions" ... those operations that need only be signaled as done to the
   controller ... are given on this queue.  The "channel_activity" field indicates
   whether any operation currently possesses the channel.

   It is our hope that new requests cannot enter the system when the channel is
   possessed by another operation.  This would not model reality!!  However, this
   code (and that in ssd_request_arrive) will handle this case "properly" by enqueuing
   the incoming request.  */

static void ssd_check_channel_activity (ssd_t *currdisk)
{
   while (1) {
       ioreq_event *curr = currdisk->completion_queue;
       currdisk->channel_activity = curr;
       if (curr != NULL) {
           currdisk->completion_queue = curr->next;

           if (curr->flags & READ) {
               /* transfer data up the line: curr->bcount, which is still set to */
               /* original requested value, indicates how many blks to transfer. */
               curr->type = DEVICE_DATA_TRANSFER_COMPLETE;
               ssd_send_event_up_path(curr, (double) 0.0);
           } else {
               ssd_request_complete (curr);
           }
           
       } else {
           curr = ioqueue_get_next_request(currdisk->queue);
           currdisk->channel_activity = curr;
           if (curr != NULL) {
               if (curr->flags & READ) {
                   ssd_media_access_request(curr);
                   continue;
               } else {
                   curr->cause = RECONNECT;
                   curr->type = IO_INTERRUPT_ARRIVE;
                   currdisk->reconnect_reason = IO_INTERRUPT_ARRIVE;
                   ssd_send_event_up_path (curr, currdisk->bus_transaction_latency);
               }
           }
       }
       break;
   }
}

/*
 * send completion up the line
 */
static void ssd_request_complete(ioreq_event *curr)
{
   ssd_t *currdisk;
   ioreq_event *x;

   // fprintf (outputfile, "Entering ssd_request_complete: %12.6f\n", simtime);

   currdisk = getssd (curr->devno);
   ssd_assert_current_activity(currdisk, curr);

   if ((x = ioqueue_physical_access_done(currdisk->queue,curr)) == NULL) {
      fprintf(stderr, "ssd_request_complete:  ioreq_event not found by ioqueue_physical_access_done call\n");
      exit(1);
   }

   fprintf(outputfile6, "%.6f %d %d\n", disksim->lastphystime, curr->blkno, curr->flags); 

   /* send completion interrupt */
   curr->type = IO_INTERRUPT_ARRIVE;
   curr->cause = COMPLETION;
   ssd_send_event_up_path(curr, currdisk->bus_transaction_latency);
}

static void ssd_bustransfer_complete (ioreq_event *curr)
{
   // fprintf (outputfile, "Entering ssd_bustransfer_complete for disk %d: %12.6f\n", curr->devno, simtime);

   if (curr->flags & READ) {
      ssd_request_complete (curr);
   } else {
      ssd_t *currdisk = getssd (curr->devno);
      ssd_assert_current_activity(currdisk, curr);

      ssd_media_access_request (curr);
      ssd_check_channel_activity (currdisk);
   }
}



int ssd_already_present(ssd_req **reqs, int total, ioreq_event *req)
{
    int i;
    int found = 0;

    for (i = 0; i < total; i ++) {
        if ((req->blkno == reqs[i]->org_req->blkno) &&
            (req->flags == reqs[i]->org_req->flags) &&
			(req->busno == reqs[i]->org_req->busno)) {
            found = 1;
            break;
        }
    }

    return found;
}

int _ssd_invoke_element_cleaning(int elem_num, ssd_t *s)
{
    int clean_invoked = ssd_clean_element(s, elem_num);
    return clean_invoked;
}

static int ssd_invoke_element_cleaning(int elem_num, ssd_t *s)
{
    int cleaning_invoked = 0;
    ssd_element *elem = &s->elements[elem_num];

    // element must be free
    ASSERT(elem->media_busy == FALSE);

    cleaning_invoked = _ssd_invoke_element_cleaning(elem_num, s);

	/* 보류
    // cleaning was invoked on this element. we can start
    // the next operation on this elem only after the cleaning
    // gets over.
    if (max_cost > 0) {
        ioreq_event *tmp;

        elem->media_busy = 1;
        cleaning_invoked = 1;

        // we use the 'blkno' field to store the element number
        tmp = (ioreq_event *)getfromextraq();
        tmp->devno = s->devno;
        tmp->time = simtime + max_cost;
        tmp->blkno = elem_num;
        tmp->ssd_elem_num = elem_num;
        tmp->type = SSD_CLEAN_ELEMENT;
        tmp->flags = SSD_CLEAN_ELEMENT;
        tmp->busno = -1;
        tmp->bcount = -1;
        stat_update (&s->stat.acctimestats, max_cost);
        addtointq ((event *)tmp);

        // stat
        elem->stat.tot_clean_time += max_cost;
		ssd_dpower(s, max_cost);
    }
	*/

    return cleaning_invoked;
}

void ssd_activate_elem(ssd_t *currdisk, int elem_num)
{
    ioreq_event *req;
    ssd_req **read_reqs;
    ssd_req **write_reqs;
	ssd_req **erase_reqs;
    int i;
    int read_total = 0;
    int write_total = 0;
	int erase_total = 0;
	int gc_write = 0;
	int gc_read = 0;
    double schtime = 0;
	double write_schtime = 0;
    int max_reqs;
    int tot_reqs_issued;
    double max_time_taken = 0;


    ssd_element *elem = &currdisk->elements[elem_num];

    // if the media is busy, we can't do anything, so return
    if (elem->media_busy == TRUE) {
        return;
    }

    ASSERT(ioqueue_get_reqoutstanding(elem->queue) == 0);

	//don't need 매 갱신 시 새 블록 할당
    // we can invoke cleaning in the background whether there
    // is request waiting or not
  //  if (currdisk->params.cleaning_in_background) {
  //      // if cleaning was invoked, wait until
  //      // it is over ...
		//if (!elem->metadata.clean_in_progress){
		//	if (ssd_invoke_element_cleaning(elem_num, currdisk)) {
		//		return;
		//	}
		//}
  //  }

    ASSERT(elem->metadata.reqs_waiting == ioqueue_get_number_in_queue(elem->queue));

    if (elem->metadata.reqs_waiting > 0) {

		//don't need
        // invoke cleaning in foreground when there are requests waiting
   //     if (!currdisk->params.cleaning_in_background) {
   //         // if cleaning was invoked, wait until
   //         // it is over ...
			//if (!elem->metadata.clean_in_progress){
			//	if (ssd_invoke_element_cleaning(elem_num, currdisk)) {
			//		return;
			//	}
			//}
   //     }

        // how many reqs can we issue at once
        if (currdisk->params.copy_back == SSD_COPY_BACK_DISABLE) {
            max_reqs = 1;
        } else {
            if (currdisk->params.num_parunits == 1) {
                max_reqs = 1;
            } else {
                max_reqs = MAX_REQS_ELEM_QUEUE;
            }
        }

        // ideally, we should issue one req per plane, overlapping them all.
        // in order to simplify the overlapping strategy, let's issue
        // requests of the same type together.

        read_reqs = (ssd_req **) malloc(max_reqs * sizeof(ssd_req *));
        write_reqs = (ssd_req **) malloc(max_reqs * sizeof(ssd_req *));
		erase_reqs = (ssd_req **) malloc(max_reqs * sizeof(ssd_req *));

        // collect the requests
        while ((req = ioqueue_get_next_request(elem->queue)) != NULL) {
            int found = 0;

            elem->metadata.reqs_waiting --;

            // see if we already have the same request in the list.
            // this usually doesn't happen -- but on synthetic traces
            // this weird case can occur.
            if (req->flags & READ) {
                found = ssd_already_present(read_reqs, read_total, req);
            } else {
                found = ssd_already_present(write_reqs, write_total, req);
            }

			fprintf(outputfile7, "%10.6f %d %d %d C\n", simtime, elem_num, req->blkno, req->flags); 

            if (!found) {
                // this is a valid request
                ssd_req *r = malloc(sizeof(ssd_req));
                r->blk = req->blkno;
                r->count = req->bcount;
				//we separate read or write
				if (req->flags & READ) {
					r->is_read = 1;
					r->is_write = 0;
					r->is_gc = 0;
				}else if(req->flags == GC_READ){
					r->is_read = 1;
					r->is_write = 0;
					r->is_gc = 1;
				} else if (!req->flags) {
					r->is_read = 0;
					r->is_write = 1;
					r->is_gc = 0;
				}else if( req->flags == GC_WRITE ){
					r->is_read = 0;
					r->is_write = 1;
					r->is_gc = 1;
				} else {
					r->is_read = 0;
					r->is_write = 0;
					r->is_gc = 1;
				}
                r->org_req = req;
                r->plane_num = -1; // we don't know to which plane this req will be directed at

				if (r->is_read) {
                    read_reqs[read_total] = r;
                    read_total ++;
				} else if (r->is_write) {
                    write_reqs[write_total] = r;
                    write_total ++;
				} else {
					erase_reqs[erase_total] = r;
					erase_total ++;
				}

                // if we have more reqs than we can handle, quit
                if ((read_total >= max_reqs) ||
                    (write_total >= max_reqs)||(erase_total >= max_reqs)) {
                    break;
				}
            } else {
                // throw this request -- it doesn't make sense
                stat_update (&currdisk->stat.acctimestats, 0);
                req->time = simtime;
                req->ssd_elem_num = elem_num;
                req->type = DEVICE_ACCESS_COMPLETE;
                addtointq ((event *)req);
            }
        }

        if (read_total > 0) {
            // first issue all the read requests (it doesn't matter what we
            // issue first). i chose read because reads are mostly synchronous.
            // find the time taken to serve these requests.
            ssd_compute_access_time(currdisk, elem_num, read_reqs, read_total);

            // add an event for each request completion
            for (i = 0; i < read_total; i ++) {
              elem->media_busy = TRUE;

              // find the maximum time taken by a request
              if (schtime < read_reqs[i]->schtime) {
                  schtime = read_reqs[i]->schtime;
              }

              stat_update (&currdisk->stat.acctimestats, read_reqs[i]->acctime);
			  if(read_reqs[i]->org_req->flags == READ) {
				  read_reqs[i]->org_req->time = simtime + read_reqs[i]->schtime;
				  read_reqs[i]->org_req->ssd_elem_num = elem_num;
				  read_reqs[i]->org_req->type = DEVICE_ACCESS_COMPLETE;

				  //printf("R: blk %d elem %d acctime %f simtime %f\n", read_reqs[i]->blk,
				  //  elem_num, read_reqs[i]->acctime, read_reqs[i]->org_req->time);
				  
				  addtointq ((event *)read_reqs[i]->org_req);
			  } else {
				  read_reqs[i]->org_req->time = simtime + read_reqs[i]->schtime;
				  read_reqs[i]->org_req->type = SSD_CLEAN_ELEMENT;
				  gc_read ++;
				  addtointq ((event *)read_reqs[i]->org_req);
			  }
              free(read_reqs[i]);
            }
        }

        free(read_reqs);

        max_time_taken = schtime;

        if (write_total > 0) {
            // next issue the write requests
            ssd_compute_access_time(currdisk, elem_num, write_reqs, write_total);

            // add an event for each request completion.
            // note that we can issue the writes only after all the reads above are
            // over. so, include the maximum read time when creating the event.
            for (i = 0; i < write_total; i ++) {
              elem->media_busy = TRUE;

              stat_update (&currdisk->stat.acctimestats, write_reqs[i]->acctime);
              //printf("blk %d elem %d acc time %f\n", write_reqs[i]->blk, elem_num, write_reqs[i]->acctime);

              if (max_time_taken < (schtime+write_reqs[i]->schtime)) {
                  max_time_taken = (schtime+write_reqs[i]->schtime);
              }

			  if (write_schtime < write_reqs[i]->schtime) {
				  write_schtime = write_reqs[i]->schtime;
			  }

			  if(!write_reqs[i]->org_req->flags) {
				  write_reqs[i]->org_req->time = simtime + schtime + write_reqs[i]->schtime;
				  write_reqs[i]->org_req->ssd_elem_num = elem_num;
				  write_reqs[i]->org_req->type = DEVICE_ACCESS_COMPLETE;
				  //printf("W: blk %d elem %d acctime %f simtime %f\n", write_reqs[i]->blk,
				  //  elem_num, write_reqs[i]->acctime, write_reqs[i]->org_req->time);
				  
				  addtointq ((event *)write_reqs[i]->org_req);
			  } else {
				  write_reqs[i]->org_req->time = simtime + schtime + write_reqs[i]->schtime;
				  write_reqs[i]->org_req->type = SSD_CLEAN_ELEMENT;
				  gc_write++;
				  addtointq ((event *)write_reqs[i]->org_req);
			  }
              free(write_reqs[i]);
            }
        }

        free(write_reqs);

		if (erase_total > 0) {
            // next issue the erase requests
            ssd_compute_access_time(currdisk, elem_num, erase_reqs, erase_total);

            // add an event for each request completion.
            // note that we can issue the writes only after all the reads above are
            // over. so, include the maximum read time when creating the event.
            for (i = 0; i < erase_total; i ++) {
              elem->media_busy = TRUE;

              stat_update (&currdisk->stat.acctimestats, erase_reqs[i]->acctime);
              //printf("blk %d elem %d acc time %f\n", write_reqs[i]->blk, elem_num, write_reqs[i]->acctime);

              if (max_time_taken < (schtime+write_schtime+erase_reqs[i]->schtime)) {
                  max_time_taken = (schtime+write_schtime+erase_reqs[i]->schtime);
              }

			  erase_reqs[i]->org_req->time = simtime + schtime + write_schtime + erase_reqs[i]->schtime;
			  erase_reqs[i]->org_req->type = SSD_CLEAN_ELEMENT;
			  addtointq ((event *)erase_reqs[i]->org_req);

              free(erase_reqs[i]);
            }
		}
		free(erase_reqs);

        // statistics
        tot_reqs_issued = read_total + write_total + erase_total;
        ASSERT(tot_reqs_issued > 0);
		currdisk->elements[elem_num].stat.tot_read_reqs += read_total - gc_read;
		currdisk->elements[elem_num].stat.tot_write_reqs += write_total - gc_write;
        currdisk->elements[elem_num].stat.tot_reqs_issued += tot_reqs_issued;
        currdisk->elements[elem_num].stat.tot_time_taken += max_time_taken;
        ssd_dpower(currdisk, max_time_taken);
    }
}

void ssd_activate(ioreq_event *curr)
{
	ssd_t *currdisk;
	int elem_num;

	currdisk = getssd(curr->devno);
	elem_num = curr->ssd_elem_num;

	// release this event
	addtoextraq((event *) curr);

	ssd_activate_elem(currdisk, elem_num);
}

static void ssd_media_access_request_element (ioreq_event *curr)
{
   ssd_t *currdisk = getssd(curr->devno);
   int blkno = curr->blkno;
   int count = curr->bcount;
   //added by tiel
   int i = 0;
   int cx[SSD_MAX_ELEMENTS] = {0,};
   //double max_threshold = currdisk->params.nelements * currdisk->params.page_size;

   /* **** CAREFUL ... HIJACKING tempint2 and tempptr2 fields here **** */
   curr->tempint2 = count;
   //while (count != 0) {

   /* IO Size가 # of Flashs * page size 보다 크면 불필요한 block update가 발생함
      이를 없애기 위한 code가 필요 */
   while (count > 0) {

       // find the element (package) to direct the request
	   ioreq_event *tmp;
       int elem_num = ssd_choose_element(currdisk->user_params, blkno);
       ssd_element *elem = &currdisk->elements[elem_num];

	   //mapping table check
	   ssd_element_metadata *metadata = &(currdisk->elements[elem_num].metadata);
	   int lbn = ssd_logical_blockno(blkno, currdisk);
	   int block = metadata->lba_table[lbn];
	   int apn = blkno/currdisk->params.page_size;
	   int offset = apn/currdisk->params.nelements%(currdisk->params.pages_per_block-1);

	   if((metadata->block_usage[block].page[offset] != -1)&&(curr->flags == WRITE)) {
		   if(cx[elem_num] == 1){
			   curr->tempint2 -= ssd_choose_aligned_count(currdisk->params.page_size, blkno, count);
			   count -= ssd_choose_aligned_count(currdisk->params.page_size, blkno, count);
			   continue;
		   }else{
			   cx[elem_num] = 1;
		   }
	   }
       // create a new sub-request for the element
       tmp = (ioreq_event *)getfromextraq();
       tmp->devno = curr->devno;
       tmp->busno = curr->busno;
       tmp->flags = curr->flags;
       tmp->blkno = blkno;
       tmp->bcount = ssd_choose_aligned_count(currdisk->params.page_size, blkno, count);
       //ASSERT(tmp->bcount == currdisk->params.page_size);

	   /*if(curr->bcount > max_threshold)
		   tmp->tempint1 = 1;*/
	   if(curr->flags & READ){
		   fprintf(outputfile5, "%.6f %d %d %d D\n", simtime, 0, elem_num, blkno); 
	   }else {
		   fprintf(outputfile4, "%.6f %d %d %d D\n", simtime, 0, elem_num, blkno); 
	   }

       tmp->tempptr2 = curr;
       blkno += tmp->bcount;
       count -= tmp->bcount;

       elem->metadata.reqs_waiting ++;

       // add the request to the corresponding element's queue
       ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
       //ssd_activate_elem(currdisk, elem_num);

	    // added by tiel
	   // activate request create simtime, type, elem_num
	   {
		   /*ioreq_event *temp = (ioreq_event *)getfromextraq();
		   temp->type = SSD_ACTIVATE_ELEM;
		   temp->time = simtime + (i * currdisk->params.channel_switch_delay);*/
		   int ch_num;
		   double wtime, ctime;
		   ioreq_event *temp = (ioreq_event *)getfromextraq();
		   temp->type = SSD_ACTIVATE_ELEM;
		   //Insert Channel/Way delay
		   //Channel Number = Chip number % Number of Channel
		   ch_num = elem_num % currdisk->params.nchannel;
		   wtime = currdisk->CH[ch_num].arrival_time + ssd_data_transfer_cost(currdisk,tmp->bcount);
		   ctime = simtime + (i * currdisk->params.channel_switch_delay);
		   if(currdisk->params.nchannel == currdisk->params.nelements){
			   temp->time = ctime;
			   currdisk->CH[ch_num].ccount++;
		   }else if(simtime > wtime || currdisk->CH[ch_num].flag == -1){
			   //channel data setting
			   currdisk->CH[ch_num].arrival_time = ctime;
			   currdisk->CH[ch_num].flag = curr->flags;
			   temp->time = ctime;
			   currdisk->CH[ch_num].ccount++;
		   }else if(currdisk->CH[ch_num].flag ==READ){
			   if(wtime > ctime){
				   if(curr->flags == READ){
					   temp->time = wtime;
				   }else{
					   temp->time = wtime + currdisk->params.page_read_latency;
				   }
				   currdisk->CH[ch_num].wcount++;
			   }else{
				   temp->time = ctime;
				   currdisk->CH[ch_num].ccount++;
			   }
			   currdisk->CH[ch_num].arrival_time = temp->time;
			   currdisk->CH[ch_num].flag = curr->flags;
		   }else if(currdisk->CH[ch_num].flag == WRITE){
			   if(wtime > ctime){
					   temp->time = wtime;
					   currdisk->CH[ch_num].wcount++;
			   }else{
				   temp->time = ctime;
				   currdisk->CH[ch_num].ccount++;
			   }
			   currdisk->CH[ch_num].arrival_time = temp->time;
			   currdisk->CH[ch_num].flag = curr->flags;
		   }
		   temp->ssd_elem_num = elem_num;
		   addtointq ((event *)temp);
		   i ++;
	   }
   }
}

static void ssd_media_access_request (ioreq_event *curr)
{
    ssd_t *currdisk = getssd(curr->devno);

    switch(currdisk->params.alloc_pool_logic) {
        case SSD_ALLOC_POOL_PLANE:
        case SSD_ALLOC_POOL_CHIP:
            ssd_media_access_request_element(curr);
        break;

        case SSD_ALLOC_POOL_GANG:
#if SYNC_GANG
            ssd_media_access_request_gang_sync(curr);
#else
            ssd_media_access_request_gang(curr);
#endif
        break;

        default:
            printf("Unknown alloc pool logic %d\n", currdisk->params.alloc_pool_logic);
            ASSERT(0);
    }
}

static void ssd_reconnect_done (ioreq_event *curr)
{
   ssd_t *currdisk;

   // fprintf (outputfile, "Entering ssd_reconnect_done for disk %d: %12.6f\n", curr->devno, simtime);

   currdisk = getssd (curr->devno);
   ssd_assert_current_activity(currdisk, curr);

   if (curr->flags & READ) {
		addtoextraq((event *) curr);
		ssd_check_channel_activity (currdisk);
   } else {
      if (currdisk->reconnect_reason == DEVICE_ACCESS_COMPLETE) {
         ssd_request_complete (curr);

      } else {
         /* data transfer: curr->bcount, which is still set to original */
         /* requested value, indicates how many blks to transfer.       */
         curr->type = DEVICE_DATA_TRANSFER_COMPLETE;
         ssd_send_event_up_path(curr, (double) 0.0);
      }
   }
}

static void ssd_request_arrive (ioreq_event *curr)
{
   ssd_t *currdisk;

   // fprintf (outputfile, "Entering ssd_request_arrive: %12.6f\n", simtime);
   // fprintf (outputfile, "ssd = %d, blkno = %d, bcount = %d, read = %d\n",curr->devno, curr->blkno, curr->bcount, (READ & curr->flags));

   currdisk = getssd(curr->devno);

   // verify that request is valid.
   if ((curr->blkno < 0) || (curr->bcount <= 0) ||
       ((curr->blkno + curr->bcount) > currdisk->numblocks)) {
      fprintf(stderr, "Invalid set of blocks requested from ssd - blkno %d, bcount %d, numblocks %d\n", curr->blkno, curr->bcount, currdisk->numblocks);
      exit(1);
   }

   /* create a new request, set it up for initial interrupt */
   ioqueue_add_new_request(currdisk->queue, curr);
   if (currdisk->channel_activity == NULL) {

      curr = ioqueue_get_next_request(currdisk->queue);
      currdisk->busowned = ssd_get_busno(curr);
      currdisk->channel_activity = curr;
      currdisk->reconnect_reason = IO_INTERRUPT_ARRIVE;

      if (curr->flags & READ) {
          ssd_media_access_request (curr);
          ssd_check_channel_activity(currdisk);
      } else {
         curr->cause = READY_TO_TRANSFER;
         curr->type = IO_INTERRUPT_ARRIVE;
         ssd_send_event_up_path(curr, currdisk->bus_transaction_latency);
      }
   }
}

/*
 * cleaning in an element is over.
 */
static void ssd_clean_element_complete(ioreq_event *curr)
{
   ssd_t *currdisk;
   int elem_num;
   ssd_element  *elem;
   ioreq_event *x;
   int lba;

   currdisk = getssd (curr->devno);
   elem_num = curr->ssd_elem_num;
   elem = &currdisk->elements[elem_num];

   /*
   if (curr->flags != GC_ERASE) {
	   lba = ssd_logical_blockno(curr->blkno, currdisk);
   }

   if(curr->flags == GC_READ){
	   fprintf(outputfile5, "%10.6f %d %d %d\n", simtime, lba, elem_num, curr->blkno); 
   }
   else if (curr->flags == GC_WRITE){
	   fprintf(outputfile4, "%10.6f %d %d %d\n", simtime, lba, elem_num, curr->blkno); 
   }
   */

   if ((x = ioqueue_physical_access_done(elem->queue,curr)) == NULL) {
      fprintf(stderr, "ssd_access_complete:  ioreq_event not found by ioqueue_physical_access_done call\n");
      exit(1);
   }

   ssd_dpower(currdisk, 0);

   // all the reqs are over
   if (ioqueue_get_reqoutstanding(elem->queue) == 0) {
		elem->media_busy = FALSE;
   }

   // release this event
   addtoextraq((event *) curr);

   // activate the gang to serve the next set of requests
   ssd_activate_elem(currdisk, elem_num);
}

void ssd_complete_parent(ioreq_event *curr, ssd_t *currdisk)
{
    ioreq_event *parent;

    /* **** CAREFUL ... HIJACKING tempint2 and tempptr2 fields here **** */
    parent = curr->tempptr2;
	if(parent->blkno == 22662568)
		printf("break complete\n");
    parent->tempint2 -= curr->bcount;

    if (parent->tempint2 == 0) {
      ioreq_event *prev;

      assert(parent != currdisk->channel_activity);
      prev = currdisk->completion_queue;
      if (prev == NULL) {
         currdisk->completion_queue = parent;
         parent->next = prev;
      } else {
         while (prev->next != NULL)
            prev = prev->next;
            parent->next = prev->next;
            prev->next = parent;
      }
      if (currdisk->channel_activity == NULL) {
         ssd_check_channel_activity (currdisk);
      }
    }
}

static void ssd_access_complete_element(ioreq_event *curr)
{
   ssd_t *currdisk;
   int elem_num;
   ssd_element  *elem;
   ioreq_event *x;
   int lba;

   currdisk = getssd (curr->devno);
   elem_num = ssd_choose_element(currdisk->user_params, curr->blkno);
   ASSERT(elem_num == curr->ssd_elem_num);
   elem = &currdisk->elements[elem_num];

   lba = ssd_logical_blockno(curr->blkno, currdisk);

   if(curr->flags & READ){
	   fprintf(outputfile5, "%.6f %d %d %d C\n", simtime, lba, elem_num, curr->blkno); 
   }
   else {
	   fprintf(outputfile4, "%.6f %d %d %d C\n", simtime, lba, elem_num, curr->blkno); 
   }

   if(curr->blkno ==2041)
	   printf("break access\n");

   if ((x = ioqueue_physical_access_done(elem->queue,curr)) == NULL) {
      fprintf(stderr, "ssd_access_complete:  ioreq_event not found by ioqueue_physical_access_done call\n");
      exit(1);
   }

   ssd_dpower(currdisk, 0);

   // all the reqs are over
   if (ioqueue_get_reqoutstanding(elem->queue) == 0) {
		elem->media_busy = FALSE;
   }

   ssd_complete_parent(curr, currdisk);
   ssd_activate_elem(currdisk, elem_num);
}

static void ssd_access_complete(ioreq_event *curr)
{
    ssd_t *currdisk = getssd (curr->devno);;

    switch(currdisk->params.alloc_pool_logic) {
        case SSD_ALLOC_POOL_PLANE:
        case SSD_ALLOC_POOL_CHIP:
            ssd_access_complete_element(curr);
        break;

        case SSD_ALLOC_POOL_GANG:
#if SYNC_GANG
            ssd_access_complete_gang_sync(curr);
#else
            ssd_access_complete_gang(curr);
#endif
        break;

        default:
            printf("Unknown alloc pool logic %d\n", currdisk->params.alloc_pool_logic);
            ASSERT(0);
    }
}

/* intermediate disconnect done */
static void ssd_disconnect_done (ioreq_event *curr)
{
   ssd_t *currdisk;

   currdisk = getssd (curr->devno);
   ssd_assert_current_activity(currdisk, curr);

   // fprintf (outputfile, "Entering ssd_disconnect for disk %d: %12.6f\n", currdisk->devno, simtime);

   addtoextraq((event *) curr);

   if (currdisk->busowned != -1) {
      bus_ownership_release(currdisk->busowned);
      currdisk->busowned = -1;
   }
   ssd_check_channel_activity (currdisk);
}

/* completion disconnect done */
static void ssd_completion_done (ioreq_event *curr)
{
   ssd_t *currdisk = getssd (curr->devno);
   ssd_assert_current_activity(currdisk, curr);

   // fprintf (outputfile, "Entering ssd_completion for disk %d: %12.6f\n", currdisk->devno, simtime);

   addtoextraq((event *) curr);

   if (currdisk->busowned != -1) {
      bus_ownership_release(currdisk->busowned);
      currdisk->busowned = -1;
   }

   ssd_check_channel_activity (currdisk);
}

static void ssd_interrupt_complete (ioreq_event *curr)
{
   switch (curr->cause) {

      case RECONNECT:
         ssd_reconnect_done(curr);
		 break;

      case DISCONNECT:
		 ssd_disconnect_done(curr);
		 break;

      case COMPLETION:
		 ssd_completion_done(curr);
		 break;

      default:
         ddbg_assert2(0, "bad event type");
   }
}

void ssd_process_event(ioreq_event *curr)
{
   ssd_t *currdisk;

   currdisk = getssd (curr->devno);

   switch (curr->type) {

// disksim IO event
      case DEVICE_OVERHEAD_COMPLETE:
         ssd_request_arrive(curr);
         break;

      case DEVICE_ACCESS_COMPLETE:
         ssd_access_complete (curr);
         break;

      case DEVICE_DATA_TRANSFER_COMPLETE:
         ssd_bustransfer_complete(curr);
         break;

      case IO_INTERRUPT_COMPLETE:
         ssd_interrupt_complete(curr);
         break;

// SSD IO event
		 //added by tiel
	  case SSD_ACTIVATE_ELEM:
          ssd_activate(curr);
          break;

      case SSD_CLEAN_GANG:
          ssd_clean_gang_complete(curr);
          break;

      case SSD_CLEAN_ELEMENT:
          ssd_clean_element_complete(curr);
          break;

        default:
         fprintf(stderr, "Unrecognized event type!\n");
         exit(1);
   }
}

