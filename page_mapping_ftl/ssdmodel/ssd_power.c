
#include "ssd.h"
#include "ssd_power.h"

void ssd_power_flash_calculate(ssd_power_type_t type, double time, ssd_power_element_stat *power_stat, ssd_t *s)
{
	double energy_value = 0.0;

	switch(type)
	{
	case SSD_POWER_FLASH_READ:
		energy_value = s->params.flash_input_voltage * s->params.page_read_current * time;
		power_stat->num_reads++;
		power_stat->read_power_consumed += energy_value;
	break;

	case SSD_POWER_FLASH_WRITE:
		energy_value = s->params.flash_input_voltage * s->params.page_write_current * time;
		power_stat->num_writes++;
		power_stat->write_power_consumed += energy_value;
	break;

	case SSD_POWER_FLASH_ERASE:
		energy_value = s->params.flash_input_voltage * s->params.page_erase_current * time;
		power_stat->num_erase++;
		power_stat->erase_power_consumed += energy_value;
	break;

	case SSD_POWER_FLASH_BUS_DATA_TRANSFER:
		energy_value = s->params.flash_input_voltage * s->params.flash_bus_current * time;
		power_stat->bus_power_consumed += energy_value;
	break;

	default:
	break;
	}
}

void ssd_power_ssd_calculate(ssd_power_type_t type, double time, ssd_t *s)
{
	ssd_power_ssd_stat *ssd_power_stat = &(s->ssd_power_stat);
	double energy_value = 0.0;

	switch(type)
	{
	case SSD_POWER_BUS_DATA_TRANSFER:
		energy_value = 5 * s->params.ssd_bus_current * time;
		ssd_power_stat->ssd_bus_power_consumed += energy_value;
		ssd_power_stat->ssd_bus_time_consumed += time;
	break;

	default:
	break;
	}
}
// version 1.0 by tiel
void power_update(ssd_t *s, double cost)
{
	int i, busy;
	double total_energy = 0.0;
	double cpu_active_energy = 0.0;
	double cpu_idle_energy = 0.0;
	double cpu_active_time = 0.0;
	double cpu_idle_time = 0.0;
	double ram_active_energy = 0.0;
	double ram_idle_energy = 0.0;
	double leakage_energy = 0.0;
	double time = 0.0;
	double idle_current_elem = 0.0;
	double idle_current;

	busy = 0;
	for (i = 0; i < s->params.nelements; i ++) {
		double element_total_energy = 0.0;
		double element_idle_time = 0.0;
		double element_idle_energy = 0.0;
		double element_active_energy = 0.0;
		ssd_power_element_stat *stat = &(s->elements[i].power_stat);

		// get idle energy
		//element_idle_time = s->section + s->current_cost - warmuptime - stat->acc_time;
		//element_idle_time = s->acc_time - stat->acc_time;
		if((simtime+cost) > (s->section + s->prev_cost)){
			element_idle_time = simtime + cost - stat->acc_time;
		}else{
			element_idle_time = s->section + s->prev_cost - stat->acc_time;
		}
		element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

		// get active energy
		element_active_energy += stat->read_power_consumed + stat->write_power_consumed + 
								stat->erase_power_consumed + stat->bus_power_consumed; 
		// get total element energy
		element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
								stat->erase_power_consumed + stat->bus_power_consumed + element_idle_energy;

		total_energy += element_total_energy;
		if(s->elements[i].media_busy == 1) {
			idle_current_elem += s->params.page_write_current;
			busy++;
		} else {
			idle_current_elem += s->params.flash_idle_current;
		}
	}
	// get CPU energy
	cpu_idle_time = simtime + cost - s->acc_time;
	cpu_active_energy = s->params.cpu_normal_mode_power * s->acc_time;
	cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;

	total_energy += cpu_active_energy;
	total_energy += cpu_idle_energy;

	//ram energy
	//ram_active_energy = s->params.dram_active_current * s->params.dram_input_voltage * cpu_active_time;
	ram_active_energy = 0.0;
	ram_idle_energy = s->params.dram_idle_current * s->params.dram_input_voltage * simtime;

	total_energy += ram_active_energy;
	total_energy += ram_idle_energy;
	
	// get BUS energy
	total_energy += s->ssd_power_stat.ssd_bus_power_consumed;

	//get Leakage energy
	leakage_energy = s->params.leakage_power * simtime;
	total_energy += leakage_energy;

	s->power_section.time = simtime;
	s->power_section.cost = cost;
	time = simtime - s->section - s->prev_cost;
	//if(simtime > 50430.0)
	//	printf("break");
	if( time >= 0){
		double power, energy;
		energy = total_energy - s->prev_energy;
		s->power_section.energy = energy * cost / (cost+time);
		s->prev_energy = total_energy;
		power = energy / (cost + time);
		s->power_section.power = power * 1000;
	}else {
		double energy = total_energy - s->prev_energy;
		s->prev_energy += energy;
		if( (time + cost) < 0){
			double piece_energy, power;
			piece_energy = s->power_section.energy;
			piece_energy = piece_energy * (cost/s->prev_cost);
			s->power_section.energy = piece_energy + energy;
			power = s->power_section.energy / cost;
			s->power_section.power = power * 1000;
		}else{
			double piece_energy, duplicate_cost, power;
			duplicate_cost = 0 - time;
			piece_energy = s->power_section.energy * (duplicate_cost/s->prev_cost);
			s->power_section.energy = piece_energy + energy;
			power = s->power_section.energy / cost;
			s->power_section.power = power * 1000;
		}
	}
	idle_current = (s->params.cpu_normal_mode_power + s->params.leakage_power)/5 + s->params.dram_idle_current;
	s->power_section.current = (idle_current + idle_current_elem) * 1000;
}
//*/
/*
//version 2.0 by tiel 
//don't calculate previous idle current
//just calculate current energy
void power_update(ssd_t *s, double cost)
{
	int i, busy;
	double total_energy = 0.0;
	double cpu_active_energy = 0.0;
	double cpu_idle_energy = 0.0;
	double cpu_active_time = 0.0;
	double cpu_idle_time = 0.0;
	double ram_active_energy = 0.0;
	double ram_idle_energy = 0.0;
	double leakage_energy = 0.0;
	double time = 0.0;
	double idle_current_elem = 0.0;
	double idle_current;

	busy = 0;
	for (i = 0; i < s->params.nelements; i ++) {
		double element_total_energy = 0.0;
		double element_idle_time = 0.0;
		double element_idle_energy = 0.0;
		double element_active_energy = 0.0;
		ssd_power_element_stat *stat = &(s->elements[i].power_stat);

		// get idle energy
		//element_idle_time = s->section + s->current_cost - warmuptime - stat->acc_time;
		//element_idle_time = s->acc_time - stat->acc_time;
		element_idle_time = simtime + cost - stat->acc_time;
		element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

		// get active energy
		element_active_energy += stat->read_power_consumed + stat->write_power_consumed + 
								stat->erase_power_consumed + stat->bus_power_consumed; 
		// get total element energy
		element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
								stat->erase_power_consumed + stat->bus_power_consumed + element_idle_energy;

		total_energy += element_total_energy;
		if(s->elements[i].media_busy == 1) {
			idle_current_elem += s->params.page_write_current;
			busy++;
		} else {
			idle_current_elem += s->params.flash_idle_current;
		}
	}
	// get CPU energy
	cpu_idle_time = simtime - s->acc_time;
	cpu_active_energy = s->params.cpu_normal_mode_power * s->acc_time;
	cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;

	total_energy += cpu_active_energy;
	total_energy += cpu_idle_energy;

	//ram energy
	ram_active_energy = s->params.dram_active_current * s->params.dram_input_voltage * cpu_active_time;
	ram_idle_energy = s->params.dram_idle_current * s->params.dram_input_voltage * cpu_idle_time;

	total_energy += ram_active_energy;
	total_energy += ram_idle_energy;
	
	// get BUS energy
	total_energy += s->ssd_power_stat.ssd_bus_power_consumed;

	//get Leakage energy
	leakage_energy = s->params.leakage_power * simtime;
	total_energy += leakage_energy;

	s->power_section.time = simtime + cost;
	s->power_section.energy = total_energy - s->prev_energy;

	if( s->section > (s->prev_time + s->prev_cost)){
		time = (s->section + s->current_cost) - (s->prev_time + s->prev_cost);
		s->power_section.power = s->ower_section.energy / time * 1000;
	}
	else {
		double ram;
		double cpu;
		double leakage;
		double a_time;
		double chip;
		double energy;

		time = s->current_cost;
		a_time = time - (s->section + s->current_cost - s->prev_time - s->prev_cost);

		ram = s->params.dram_idle_current * s->params.dram_input_voltage * a_time;
		cpu = s->params.cpu_normal_mode_power * a_time;
		leakage = s->params.leakage_power * a_time;
		chip = s->params.flash_input_voltage * s->params.flash_idle_current * a_time * s->params.nelements;

		energy = s->power_section.energy + ram + cpu + leakage + chip;

		s->power_section.power = (energy / time) * 1000 ;
	}
	idle_current = (s->params.cpu_normal_mode_power + s->params.leakage_power)/5 + s->params.dram_idle_current;
	s->power_section.current = idle_current + idle_current_elem;
	s->power_section.cost = time;
	s->power_section.time = simtime;
}
*/
void print_power_start(ssd_t *s)
{
	ssd_power_section tmp = {0.0,0.0,0.0,0.0,0.0};
	double time = 0.0;
	double idle_power_ram, idle_current_ctr, idle_current_elem;
	int i;

	idle_power_ram = s->params.dram_idle_current * s->params.dram_input_voltage;
	idle_current_elem = s->params.flash_idle_current * s->params.nelements;
	idle_current_ctr  = (s->params.cpu_idle_mode_power + s->params.leakage_power)/5 + s->params.dram_idle_current;

	tmp.energy = ((s->params.cpu_idle_mode_power + s->params.leakage_power) + idle_power_ram) * 0.5;
	tmp.power = ((s->params.cpu_idle_mode_power + s->params.leakage_power) + idle_power_ram) * 1000;
	tmp.current = (idle_current_ctr + idle_current_elem) * 1000;
	tmp.cost = 0.5;

	if(s->end_list == NULL) {
		s->end_list = &(s->power_section);
		
		tmp.energy = (s->params.cpu_idle_mode_power + s->params.leakage_power) + idle_power_ram;
		
		fprintf(outputfile2, "#SSD Power Distribution \n");
		fprintf(outputfile2, "#time(mSec),Current(mA),Power(mW),Cost(mSec),TOTAL_P(mJ),\n"); 
		fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", time, tmp.current, tmp.power, tmp.cost, tmp.energy);

		if(simtime>0.1){
			time = simtime - 0.1;
			fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", time, tmp.current, tmp.power, tmp.cost, tmp.energy);
			fflush (outputfile2);
		}
		fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", simtime, s->power_section.current, s->power_section.power, s->power_section.cost, s->	power_section.energy); 
	}else {
		time = simtime - (s->section + s->prev_cost);
		if(time > 0.2) {
			/*i = 1;
			while( time > 1) {
				tmp.time = s->section + s->prev_cost + (i*0.5);		
				fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", tmp.time, tmp.current, tmp.power, tmp.cost, tmp.energy); 
				time -= 0.5;
				i++;
			}*/
			tmp.time = s->section + s->prev_cost + 0.1;		
			fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", tmp.time, tmp.current, tmp.power, tmp.cost, tmp.energy); 
			tmp.time = simtime - 0.1;		
			fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", tmp.time, tmp.current, tmp.power, tmp.cost, tmp.energy); 
		}
		fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", simtime, s->power_section.current, s->power_section.power, s->power_section.cost, s->power_section.energy); 
		fflush (outputfile2);
	}
}

void print_power_end(ssd_t *s)
{
	int i, busy, waiting;
	double idle_current_elem = 0.0;
	double idle_current;

	busy = 0;
	waiting = 0;
	for (i = 0; i < s->params.nelements; i ++) {
		ssd_power_element_stat *stat = &(s->elements[i].power_stat);
		waiting += s->elements[i].metadata.reqs_waiting;

		if(s->elements[i].media_busy == 1) {
			idle_current_elem += s->params.page_write_current;
			busy++;
		} else {
			idle_current_elem += s->params.flash_idle_current;
		}
	}

	idle_current = (s->params.cpu_normal_mode_power + s->params.leakage_power)/5 + s->params.dram_idle_current;
	s->power_section.current = (idle_current + idle_current_elem) * 1000;

	fprintf(outputfile2, "%6.4f,%6.4f,\n", simtime, s->power_section.current); 
	fflush (outputfile2);

	/*if((busy ==1) && (waiting == 0)){
		double time;
		double current;
		time = simtime + 0.1;

		idle_current = (s->params.cpu_idle_mode_power + s->params.leakage_power)/5 + s->params.dram_idle_current;
		current = (s->params.flash_idle_current * s->params.nelements + idle_current) * 1000;

		fprintf(outputfile2, "%6.4f,%6.4f,%6.4f,%6.4f,%6.4f,\n", time, current, s->power_section.power, s->power_section.cost, s->power_section.energy); 
		fflush (outputfile2);
	}*/
}

void ssd_dpower(ssd_t *s, double cost) 
{
	if(cost == 0) {
		print_power_end(s);
	}else {
		double p_time = s->section + s->prev_cost;
		double c_time = simtime + cost;

		if( simtime > p_time){
			s->acc_time += cost;
		}else if (c_time > p_time){
			s->acc_time += (c_time - p_time);	
		}

		power_update(s, cost);
		print_power_start(s);
		
		//s->prev_time = s->section;
		if(c_time > p_time){
			s->prev_cost = cost;
		}else{
			s->prev_cost = s->prev_cost - (simtime - s->section);
		}
		//s->current_cost = cost;
		//s->prev_energy += s->power_section.energy;
		s->section = simtime;
	}
	//else{
	//	/*if((cost > s->current_cost) && (simtime == s->section))
	//	{
	//		s->current_cost = cost;
	//		s->power_section.time = s->section + cost;
	//	}*/
	//	power_update(s, cost);
	//}
}