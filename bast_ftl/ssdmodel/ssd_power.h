

#ifndef DISKSIM_SSD_POWER_H
#define DISKSIM_SSD_POWER_H

typedef enum {
	SSD_POWER_FLASH_READ,
	SSD_POWER_FLASH_WRITE,
	SSD_POWER_FLASH_ERASE,
	SSD_POWER_FLASH_BUS_DATA_TRANSFER,
	SSD_POWER_BUS_DATA_TRANSFER,
	SSD_POWER_CPU_ACTIVE,
	
} ssd_power_type_t;

void ssd_power_flash_calculate(ssd_power_type_t type, double time, ssd_power_element_stat *power_stat, ssd_t *s);
void ssd_power_ssd_calculate(ssd_power_type_t type, double time, ssd_t *s);
void power_update(ssd_t *s, double cost);
void print_power_start(ssd_t *s);
void print_power_end(ssd_t *s);

#endif

