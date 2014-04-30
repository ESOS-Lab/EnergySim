
// DiskSim SSD support

#ifndef DISKSIM_FTL_H
#define DISKSIM_FTL_H


//vp - cleaning & wear leveling policies
#define DISKSIM_SSD_CLEANING_POLICY_RANDOM                      1
#define DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AGNOSTIC        2
#define DISKSIM_SSD_CLEANING_POLICY_GREEDY_WEAR_AWARE           3

//vp - maximum no of erasures a block can sustain
#define SSD_MAX_ERASURES							1000000
//#define SSD_MAX_ERASURES							50
#define SSD_LIFETIME_THRESHOLD_X					0.80
#define SSD_LIFETIME_THRESHOLD_Y					0.85



void * ssd_new_timing_t(ssd_timing_params *params);
int ssd_choose_element(void *user_params, int blkno);

// Modules implementing this interface choose an alignment boundary for requests.
// They enforce this boundary by returning counts less than requested from choose_aligned_count
// Typically the alignment just past the last sector in a request is zero mod 8 or 16,
// and in no cases will a returned count cross a stride or block boundary.
//
// The results of compute_delay are not meaningful if a count is supplied that was not
// dictated by an earlier call to choose_aligned_count.
// get a timing object ... params pointer is valid for lifetime of element
int ssd_choose_aligned_count(int page_size, int blkno, int count);
void ssd_compute_access_time(ssd_t *s, int elem_num, ssd_req **reqs, int total);

// for Cleaning
double ssd_clean_element(ssd_t *s, int elem_num);
double ssd_compute_avg_lifetime(int plane_num, int elem_num, ssd_t *s);

#endif   /* DISKSIM_FTL_H */

