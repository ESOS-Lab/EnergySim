// DiskSim SSD support
// 2008 Microsoft Corporation. All Rights Reserved

#ifndef _DISKSIM_SSD_CLEAN_H
#define _DISKSIM_SSD_CLEAN_H

#include "ssd.h"


// CAMERA READY
#define SSD_RATELIMIT_WINDOW						0.80

//vp - threshold for cleaning
#if 1

#define LOW_WATERMARK_PER_ELEMENT(s)    ((s)->params.blocks_per_element * ((1.0*(s)->params.min_freeblks_percent)/100.0))
#define HIGH_WATERMARK_PER_ELEMENT(s)   (LOW_WATERMARK_PER_ELEMENT(s) + 1)

//#define LOW_WATERMARK_PER_PLANE(s)        ((s)->params.blocks_per_plane * ((1.0*(s)->params.min_freeblks_percent)/100.0) + 1)
#define LOW_WATERMARK_PER_PLANE(s)      ((s)->params.blocks_per_plane * ((1.0*(s)->params.min_freeblks_percent)/100.0))
#define HIGH_WATERMARK_PER_PLANE(s)     (LOW_WATERMARK_PER_PLANE(s) + 1)

#else

#define LOW_WATERMARK_PER_ELEMENT(s)    (800)
#define HIGH_WATERMARK_PER_ELEMENT(s)   (1000)

#define LOW_WATERMARK_PER_PLANE(s)      (125)
#define HIGH_WATERMARK_PER_PLANE(s)     (125)

#endif


typedef struct _usage_table {
    int len;
    int temp;
    int *block;
} usage_table;


double ssd_clean_block_partially(int plane_num, int elem_num, ssd_t *s);
double ssd_clean_element_no_copyback(int elem_num, ssd_t *s);
int ssd_free_bits(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s);
void ssd_assert_plane_freebits(int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s);

double _ssd_clean_block_fully(int blk, int plane_num, int elem_num, ssd_element_metadata *metadata, ssd_t *s);

int ssd_next_plane_in_parunit(int plane_num, int parunit_num, int elem_num, ssd_t *s);
int ssd_start_cleaning_parunit(int parunit_num, int elem_num, ssd_t *s);
int ssd_start_cleaning(int plane_num, int elem_num, ssd_t *s);
int ssd_stop_cleaning(int plane_num, int elem_num, ssd_t *s);

#endif

