
#ifndef _SSDMODEL_SSD_PARAM_H
#define _SSDMODEL_SSD_PARAM_H  

#include <libparam/libparam.h>
#ifdef __cplusplus
extern"C"{
#endif
struct dm_disk_if;

/* prototype for ssdmodel_ssd param loader function */
struct ssd *ssdmodel_ssd_loadparams(struct lp_block *b, int *num);

typedef enum {
   SSDMODEL_SSD_SCHEDULER,
   SSDMODEL_SSD_MAX_QUEUE_LENGTH,
   SSDMODEL_SSD_BLOCK_COUNT,
   SSDMODEL_SSD_BUS_TRANSACTION_LATENCY,
   SSDMODEL_SSD_BULK_SECTOR_TRANSFER_TIME,
   SSDMODEL_SSD_NEVER_DISCONNECT,
   SSDMODEL_SSD_PRINT_STATS,
   SSDMODEL_SSD_COMMAND_OVERHEAD,
   SSDMODEL_SSD_TIMING_MODEL,
   SSDMODEL_SSD_FLASH_CHIP_ELEMENTS,
   SSDMODEL_SSD_PAGE_SIZE,
   SSDMODEL_SSD_PAGES_PER_BLOCK,
   SSDMODEL_SSD_BLOCKS_PER_ELEMENT,
   SSDMODEL_SSD_ELEMENT_STRIDE_PAGES,
   SSDMODEL_SSD_CHIP_XFER_LATENCY,
   SSDMODEL_SSD_CHANNEL_SWITCH_DELAY,
   SSDMODEL_SSD_CHANNEL,
   SSDMODEL_SSD_PAGE_READ_LATENCY,
   SSDMODEL_SSD_PAGE_WRITE_LATENCY,
   SSDMODEL_SSD_PAGE_WRITE_MAX_LATENCY,
   SSDMODEL_SSD_BLOCK_ERASE_LATENCY,
   SSDMODEL_SSD_WRITE_POLICY,
   SSDMODEL_SSD_RESERVE_PAGES_PERCENTAGE,
   SSDMODEL_SSD_MINIMUM_FREE_BLOCKS_PERCENTAGE,
   SSDMODEL_SSD_CLEANING_POLICY,
   SSDMODEL_SSD_PLANES_PER_PACKAGE,
   SSDMODEL_SSD_BLOCKS_PER_PLANE,
   SSDMODEL_SSD_PLANE_BLOCK_MAPPING,
   SSDMODEL_SSD_COPY_BACK,
   SSDMODEL_SSD_NUMBER_OF_PARALLEL_UNITS,
   SSDMODEL_SSD_ELEMENTS_PER_GANG,
   SSDMODEL_SSD_CLEANING_IN_BACKGROUND,
   SSDMODEL_SSD_GANG_SHARE,
   SSDMODEL_SSD_ALLOCATION_POOL_LOGIC,
   SSDMODEL_SSD_FLASH_INPUT_VOLTAGE,
   SSDMODEL_SSD_PAGE_READ_CURRENT,
   SSDMODEL_SSD_PAGE_WRITE_CURRENT,
   SSDMODEL_SSD_PAGE_ERASE_CURRENT,
   SSDMODEL_SSD_FLASH_IDLE_CURRENT,
   SSDMODEL_SSD_FLASH_BUS_CURRENT,
   SSDMODEL_SSD_CPU_NORMAL_MODE_POWER,
   SSDMODEL_SSD_CPU_IDLE_MODE_POWER,
   SSDMODEL_SSD_CPU_SLOW_MODE_POWER,
   SSDMODEL_SSD_SSD_BUS_CURRENT,
   SSDMODEL_SSD_BUFFER_CACHE_POLICY,
   SSDMODEL_SSD_DRAM_ACTIVE_CURRENT,
   SSDMODEL_SSD_DRAM_IDLE_CURRENT,
   SSDMODEL_SSD_DRAM_INPUT_VOLTAGE,
   SSDMODEL_SSD_DRAM_ACTIVE_LATENCY,
   SSDMODEL_SSD_BUFFER_CACHE_SIZE,
   SSDMODEL_SSD_LEAKAGE_POWER
} ssdmodel_ssd_param_t;

#define SSDMODEL_SSD_MAX_PARAM		SSDMODEL_SSD_LEAKAGE_POWER
extern void * SSDMODEL_SSD_loaders[];
extern lp_paramdep_t SSDMODEL_SSD_deps[];


static struct lp_varspec ssdmodel_ssd_params [] = {
   {"Scheduler", BLOCK, 1 },
   {"Max queue length", I, 1 },
   {"Block count", I, 0 },
   {"Bus transaction latency", D, 0 },
   {"Bulk sector transfer time", D, 1 },
   {"Never disconnect", I, 1 },
   {"Print stats", I, 1 },
   {"Command overhead", D, 1 },
   {"Timing model", I, 1 },
   {"Flash chip elements", I, 1 },
   {"Page size", I, 1 },
   {"Pages per block", I, 1 },
   {"Blocks per element", I, 1 },
   {"Element stride pages", I, 1 },
   {"Chip xfer latency", D, 1 },
   {"Channel switch delay", D, 1 },
   {"Channel", I, 1 },
   {"Page read latency", D, 1 },
   {"Page write latency", D, 1 },
   {"Page write max latency", D, 1 },
   {"Block erase latency", D, 1 },
   {"Write policy", I, 1 },
   {"Reserve pages percentage", I, 1 },
   {"Minimum free blocks percentage", I, 1 },
   {"Cleaning policy", I, 1 },
   {"Planes per package", I, 1 },
   {"Blocks per plane", I, 1 },
   {"Plane block mapping", I, 1 },
   {"Copy back", I, 1 },
   {"Number of parallel units", I, 1 },
   {"Elements per gang", I, 1 },
   {"Cleaning in background", I, 1 },
   {"Gang share", I, 1 },
   {"Allocation pool logic", I, 1 },
   {"Flash input voltage", D, 1 },
   {"Page read current", D, 1 },
   {"Page write current", D, 1 },
   {"Page erase current", D, 1 },
   {"Flash idle current", D, 1 },
   {"Flash bus current", D, 1 },
   {"CPU normal mode power", D, 1 },
   {"CPU idle mode power", D, 1 },
   {"CPU slow mode power", D, 1 },
   {"SSD bus current", D, 1 },
   {"Buffer cache policy", I, 1 },
   {"DRAM active current", D, 1 },
   {"DRAM idle current", D, 1 },
   {"DRAM input voltage", D, 1 },
   {"DRAM active latency", D, 1 },
   {"Buffer cache size", I, 1 },
   {"LEAKAGE power", D, 1 },
   {0,0,0}
};
#define SSDMODEL_SSD_MAX 51
static struct lp_mod ssdmodel_ssd_mod = { "ssdmodel_ssd", ssdmodel_ssd_params, SSDMODEL_SSD_MAX, (lp_modloader_t)ssdmodel_ssd_loadparams,  0, 0, SSDMODEL_SSD_loaders, SSDMODEL_SSD_deps };


#ifdef __cplusplus
}
#endif
#endif // _SSDMODEL_SSD_PARAM_H
