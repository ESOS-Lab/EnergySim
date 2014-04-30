
// DiskSim SSD support

#ifndef DISKSIM_SSD_EXTERN_H
#define DISKSIM_SSD_EXTERN_H

void    ssd_initialize (void);


void    ssd_printstats (void);
void    ssd_printsetstats (int *set, int setsize, char *sourcestr);
void    ssd_resetstats (void);
void    ssd_cleanstats (void);

extern struct ssd *getssd(int devno);

#endif   /* DISKSIM_SSD_EXTERN_H */


