
#include "ssd.h"
#include "ftl.h"

#ifndef sprintf_s
#define sprintf_s3(x,y,z) sprintf(x,z)
#define sprintf_s4(x,y,z,w) sprintf(x,z,w)
#define sprintf_s5(x,y,z,w,s) sprintf(x,z,w,s)
#else
#define sprintf_s3(x,y,z) sprintf_s(x,z)
#define sprintf_s4(x,y,z,w) sprintf_s(x,z,w)
#define sprintf_s5(x,y,z,w,s) sprintf_s(x,z,w,s)
#endif

static void ssd_acctime_printstats (int *set, int setsize, char *prefix)
{
   int i;
   statgen * statset[MAXDEVICES];

   if (device_printacctimestats) {
      for (i=0; i<setsize; i++) {
         ssd_t *currdisk = getssd (set[i]);
         statset[i] = &currdisk->stat.acctimestats;
      }
      stat_print_set(statset, setsize, prefix);
   }
}

static void ssd_other_printstats (int *set, int setsize, char *prefix)
{
   int i;
   int numbuswaits = 0;
   double waitingforbus = 0.0;

   for (i=0; i<setsize; i++) {
      ssd_t *currdisk = getssd (set[i]);
      numbuswaits += currdisk->stat.numbuswaits;
      waitingforbus += currdisk->stat.waitingforbus;
   }

   fprintf(outputfile, "%sTotal bus wait time: %f\n", prefix, waitingforbus);
   fprintf(outputfile, "%sNumber of bus waits: %d\n", prefix, numbuswaits);
}

static void ssd_print_block_lifetime_distribution(int elem_num, ssd_t *s, int ssdno, double avg_lifetime, char *sourcestr)
{
    const int bucket_size = 20;
    int no_buckets = (100/bucket_size + 1);
    int i;
    int *hist;
    int dead_blocks = 0;
    int n;
    double variance_sqr;
    double variance;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);

    // allocate the buckets
    hist = (int *) malloc(no_buckets * sizeof(int));
    memset(hist, 0, no_buckets * sizeof(int));

    // to calc the variance
    n = s->params.blocks_per_element;
    variance_sqr = 0;

    for (i = 0; i < s->params.blocks_per_element; i ++) {
        int bucket;
        int rem_lifetime = metadata->block_usage[i].rem_lifetime;
        double perc = (rem_lifetime * 100.0) / avg_lifetime;

        // find out how many blocks have completely been erased.
        if (metadata->block_usage[i].rem_lifetime == 0) {
            dead_blocks ++;
        }

        if (perc >= 100) {
            // this can happen if a particular block was not
            // cleaned at all and so its remaining life time
            // is greater than the average life time. put these
            // blocks in the last bucket.
            bucket = no_buckets - 1;
        } else {
            bucket = (int) perc / bucket_size;
        }

        hist[bucket] ++;

        // calculate the variance
        variance_sqr = variance_sqr + ((rem_lifetime - avg_lifetime) * (rem_lifetime - avg_lifetime));
    }


    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    fprintf(outputfile, "Block Lifetime Distribution\n");

    // print the bucket size
    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    for (i = bucket_size; i <= 100; i += bucket_size) {
        fprintf(outputfile, "< %d\t", i);
    }
    fprintf(outputfile, ">= 100\t\n");

    // print the histogram bar lengths
    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    for (i = bucket_size; i <= 100; i += bucket_size) {
        fprintf(outputfile, "%d\t", hist[i/bucket_size - 1]);
    }
    fprintf(outputfile, "%d\t\n", hist[no_buckets - 1]);

    variance = variance_sqr/(n - 1);
    fprintf(outputfile, "%s #%d elem #%d   Average of life time:\t%f\n",
        sourcestr, ssdno, elem_num, avg_lifetime);
    fprintf(outputfile, "%s #%d elem #%d   Variance of life time:\t%f\n",
        sourcestr, ssdno, elem_num, variance);
    fprintf(outputfile, "%s #%d elem #%d   Total dead blocks:\t%d\n",
        sourcestr, ssdno, elem_num, dead_blocks);
}

//prints the cleaning algo statistics
static void ssd_printcleanstats(int *set, int setsize, char *sourcestr)
{
    int i;
    int tot_ssd = 0;
    int elts_count = 0;
	int t_read = 0;
	int t_write = 0;
	int t_erase = 0;
	int t_pmove = 0;
    double iops = 0;

    fprintf(outputfile, "\n\nSSD CLEANING STATISTICS\n");
    fprintf(outputfile, "---------------------------------------------\n\n");
    for (i = 0; i < setsize; i ++) {
        int j;
        int tot_elts = 0;
        ssd_t *s = getssd(set[i]);

            elts_count += s->params.nelements;

            for (j = 0; j < s->params.nelements; j ++) {
                int plane_num;
                double avg_lifetime;
                double elem_iops = 0;
                double elem_clean_iops = 0;

                ssd_element_stat *stat = &(s->elements[j].stat);

                avg_lifetime = ssd_compute_avg_lifetime(-1, j, s);

				t_read += s->elements[j].stat.tot_read_reqs;
				t_write += s->elements[j].stat.tot_write_reqs;
				t_erase += s->elements[j].stat.num_clean;
				t_pmove += s->elements[j].stat.pages_moved;

                fprintf(outputfile, "%s #%d elem #%d   Total reqs issued:\t%d\n",
                    sourcestr, set[i], j, s->elements[j].stat.tot_reqs_issued);
				fprintf(outputfile, "%s #%d elem #%d   Total Read reqs issued:\t%d\n",
					sourcestr, set[i], j, s->elements[j].stat.tot_read_reqs);
				fprintf(outputfile, "%s #%d elem #%d   Total Write reqs issued:\t%d\n",
					sourcestr, set[i], j, s->elements[j].stat.tot_write_reqs);
                fprintf(outputfile, "%s #%d elem #%d   Total time taken:\t%f\n",
                    sourcestr, set[i], j, s->elements[j].stat.tot_time_taken);
                if (s->elements[j].stat.tot_time_taken > 0) {
                    elem_iops = ((s->elements[j].stat.tot_reqs_issued*1000.0)/s->elements[j].stat.tot_time_taken);
                    fprintf(outputfile, "%s #%d elem #%d   IOPS:\t%f\n",
                        sourcestr, set[i], j, elem_iops);
                }

                fprintf(outputfile, "%s #%d elem #%d   Total cleaning reqs issued:\t%d\n",
                    sourcestr, set[i], j, s->elements[j].stat.num_clean);
                fprintf(outputfile, "%s #%d elem #%d   Total cleaning time taken:\t%f\n",
                    sourcestr, set[i], j, s->elements[j].stat.tot_clean_time);
                fprintf(outputfile, "%s #%d elem #%d   Total migrations:\t%d\n",
                    sourcestr, set[i], j, s->elements[j].metadata.tot_migrations);
                fprintf(outputfile, "%s #%d elem #%d   Total pages migrated:\t%d\n",
                    sourcestr, set[i], j, s->elements[j].metadata.tot_pgs_migrated);
                fprintf(outputfile, "%s #%d elem #%d   Total migrations cost:\t%f\n",
                    sourcestr, set[i], j, s->elements[j].metadata.mig_cost);


                if (s->elements[j].stat.tot_clean_time > 0) {
                    elem_clean_iops = ((s->elements[j].stat.num_clean*1000.0)/s->elements[j].stat.tot_clean_time);
                    fprintf(outputfile, "%s #%d elem #%d   clean IOPS:\t%f\n",
                        sourcestr, set[i], j, elem_clean_iops);
                }

                fprintf(outputfile, "%s #%d elem #%d   Overall IOPS:\t%f\n",
                    sourcestr, set[i], j, ((s->elements[j].stat.num_clean+s->elements[j].stat.tot_reqs_issued)*1000.0)/(s->elements[j].stat.tot_clean_time+s->elements[j].stat.tot_time_taken));

                iops += elem_iops;

                fprintf(outputfile, "%s #%d elem #%d   Number of free blocks:\t%d\n",
                    sourcestr, set[i], j, s->elements[j].metadata.tot_free_blocks);
                fprintf(outputfile, "%s #%d elem #%d   Number of cleans:\t%d\n",
                    sourcestr, set[i], j, stat->num_clean);
                fprintf(outputfile, "%s #%d elem #%d   Pages moved:\t%d\n",
                    sourcestr, set[i], j, stat->pages_moved);
                fprintf(outputfile, "%s #%d elem #%d   Total xfer time:\t%f\n",
                    sourcestr, set[i], j, stat->tot_xfer_cost);
                if (stat->tot_xfer_cost > 0) {
                    fprintf(outputfile, "%s #%d elem #%d   Xfer time per page:\t%f\n",
                        sourcestr, set[i], j, stat->tot_xfer_cost/(1.0*stat->pages_moved));
                } else {
                    fprintf(outputfile, "%s #%d elem #%d   Xfer time per page:\t0\n",
                        sourcestr, set[i], j);
                }
                fprintf(outputfile, "%s #%d elem #%d   Average lifetime:\t%f\n",
                    sourcestr, set[i], j, avg_lifetime);
                fprintf(outputfile, "%s #%d elem #%d   Plane Level Statistics\n",
                    sourcestr, set[i], j);
                fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, set[i], j);
                for (plane_num = 0; plane_num < s->params.planes_per_pkg; plane_num ++) {
                    fprintf(outputfile, "%d:(%d)  ",
                        plane_num, s->elements[j].metadata.plane_meta[plane_num].num_cleans);
                }
                fprintf(outputfile, "\n");


                ssd_print_block_lifetime_distribution(j, s, set[i], avg_lifetime, sourcestr);
                fprintf(outputfile, "\n");

                tot_elts += stat->pages_moved;

            //fprintf(outputfile, "%s SSD %d average # of pages moved per element %d\n",
            //  sourcestr, set[i], tot_elts / s->params.nelements);

            tot_ssd += tot_elts;
            fprintf(outputfile, "\n");
        }
    }

    if (elts_count > 0) {
        fprintf(outputfile, "%s   Total SSD IOPS:\t%f\n",
            sourcestr, iops);
        fprintf(outputfile, "%s   Average SSD element IOPS:\t%f\n",
            sourcestr, iops/elts_count);
    }

	fprintf(outputfile, "%s   Total Read reqs issued:\t%d\n",
		sourcestr, t_read);
	fprintf(outputfile, "%s   Total Write reqs issued:\t%d\n",
		sourcestr, t_write);
	fprintf(outputfile, "%s   Total Clean reqs issued:\t%d\n",
		sourcestr, t_erase);
	fprintf(outputfile, "%s   Total Page moved:\t%d\n",
		sourcestr, t_pmove);

    //fprintf(outputfile, "%s SSD average # of pages moved per ssd %d\n\n",
    //  sourcestr, tot_ssd / setsize);
}

//@20090831-Micky:print power consumptions of SSD
static void ssd_power_printstats(int *set, int setsize, char *sourcestr)
{
	int i;
    double total_energy = 0.0;
    double cpu_active_energy = 0.0;
    double cpu_idle_energy = 0.0;
    double cpu_idle_time = 0.0;
    double ssd_runoutstanding = 0.0;
	double bus_active_energy = 0.0;
    double bus_idle_energy = 0.0;
    double ram_active_energy = 0.0;
    double ram_idle_energy = 0.0;
	double leakage_energy = 0.0;
    
	ssd_power_section *tmp;
	double tmp_energy = 0.0;

	fprintf(outputfile, "\n\nSSD POWER CONSUMPTION STATISTICS\n");
	fprintf(outputfile, "---------------------------------------------\n\n");

	// Get the total energy of SSD
	for (i = 0; i < setsize; i ++) {
		int j;
		ssd_t *s = getssd(set[i]);
		
		for (j = 0; j < s->params.nelements; j ++) {
			double element_total_energy = 0.0;
			double runoutstanding = 0.0;
			double element_idle_time = 0.0;
			double element_idle_energy = 0.0;
			ssd_power_element_stat *stat = &(s->elements[j].power_stat);

			// get idle energy
			//element_idle_time = simtime - warmuptime - stat->acc_time;
			//element_idle_time = simtime - stat->acc_time;
			element_idle_time = s->acc_time - stat->acc_time;
			element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

			// get total element energy
			element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
									stat->erase_power_consumed + stat->bus_power_consumed + 
									element_idle_energy;

			total_energy += element_total_energy;
		}

		// get CPU energy
		//cpu_idle_time = stat_get_runval(&s->queue->idlestats);
		//ssd_runoutstanding = s->queue->base.runoutstanding + s->queue->timeout.runoutstanding + s->queue->priority.runoutstanding;
		//ssd_runoutstanding = simtime - cpu_idle_time - warmuptime;
		//cpu_active_energy = s->params.cpu_normal_mode_power * ssd_runoutstanding;
		//cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;
		cpu_idle_time = simtime - s->acc_time;
		cpu_active_energy = s->params.cpu_normal_mode_power * s->acc_time;
		cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;
		ssd_runoutstanding = s->acc_time;

		total_energy += cpu_active_energy;
		total_energy += cpu_idle_energy;
		
		// get BUS energy
		total_energy += s->ssd_power_stat.ssd_bus_power_consumed;
//		total_energy += s->params.ssd_bus_current * 5 * simtime;

		// get RAM energy
		//total_energy += s->params.dram_input_voltage * s->params.dram_active_current * cpu_idle_time;
		//total_energy += s->params.dram_input_voltage * s->params.dram_idle_current * ssd_runoutstanding;
		total_energy += s->params.dram_input_voltage * s->params.dram_idle_current * simtime;

		// get Leakage energy
		leakage_energy = s->params.leakage_power * simtime;
		total_energy += leakage_energy;
		
	}

	fprintf(outputfile, "SSD Total energy used:\t\t%f mJ\n", total_energy );
	fprintf(outputfile, "SSD Running Time, Idle Time:\t%f, \t%f\n", ssd_runoutstanding, cpu_idle_time);
	fprintf(outputfile, "\n");
	
	for (i = 0; i < setsize; i ++) {
		int j;
		double element_read_energy = 0.0;
		double element_write_energy = 0.0;
		double element_erase_energy = 0.0;
		double element_bus_energy = 0.0;
		double telement_idle_energy = 0.0;
		double telement_total_energy = 0.0;
		ssd_t *s = getssd(set[i]);

		for (j = 0; j < s->params.nelements; j ++) {
			double element_total_energy = 0.0;
			double runoutstanding = 0.0;
			double element_idle_time = 0.0;
			double element_idle_energy = 0.0;
			ssd_power_element_stat *stat = &(s->elements[j].power_stat);
			struct ioq * queue = s->elements[j].queue;

			// get idle energy
			runoutstanding = queue->base.runoutstanding + queue->timeout.runoutstanding + queue->priority.runoutstanding;
			//element_idle_time = simtime - warmuptime - stat->acc_time;
			//element_idle_time = simtime - stat->acc_time;
			element_idle_time = s->acc_time - stat->acc_time;
			element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

			// get total element energy
			element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
									stat->erase_power_consumed + stat->bus_power_consumed + 
									element_idle_energy;

			telement_total_energy += element_total_energy;
			element_read_energy += stat->read_power_consumed;
			element_write_energy += stat->write_power_consumed;
			element_erase_energy += stat->erase_power_consumed;
			element_bus_energy += stat->bus_power_consumed;
			telement_idle_energy += element_idle_energy;

			fprintf(outputfile, "%s #%d Flash #%d: Total energy used:\t\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], j, element_total_energy, (element_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Read operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->read_power_consumed, (stat->read_power_consumed / element_total_energy * 100.0), stat->num_reads);
			fprintf(outputfile, "%s #%d Flash #%d:     Write operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->write_power_consumed, (stat->write_power_consumed / element_total_energy * 100.0), stat->num_writes);
			fprintf(outputfile, "%s #%d Flash #%d:     Erase operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->erase_power_consumed, (stat->erase_power_consumed / element_total_energy * 100.0), stat->num_erase);
			fprintf(outputfile, "%s #%d Flash #%d:     Bus transfer energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], j, stat->bus_power_consumed, (stat->bus_power_consumed / element_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Idle status energy:\t%f mJ (%.2f%%) - time:%f\n",
				sourcestr, set[i], j, element_idle_energy, (element_idle_energy / element_total_energy * 100.0), element_idle_time);

			fprintf(outputfile, "\n");
		}

		{
			fprintf(outputfile, "%s #%d Flash: Total energy used:\t\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], telement_total_energy, (telement_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Read operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_read_energy, (element_read_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Write operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_write_energy, (element_write_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Erase operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_erase_energy, (element_erase_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Bus transfer energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_bus_energy, (element_bus_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Idle operation energy:\t%f mJ (%.2f%%)\n\n",
				sourcestr, set[i], telement_idle_energy, (telement_idle_energy / telement_total_energy * 100.0));
		}

		{
			double cpu_total_energy = cpu_active_energy + cpu_idle_energy;
			fprintf(outputfile, "%s #%d CPU: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], cpu_total_energy, (cpu_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d CPU:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], cpu_active_energy, (cpu_active_energy / cpu_total_energy * 100.0), ssd_runoutstanding);
			fprintf(outputfile, "%s #%d CPU:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], cpu_idle_energy, (cpu_idle_energy / cpu_total_energy * 100.0), cpu_idle_time);
			fprintf(outputfile, "\n");
		}

		{
			double bus_total_energy = s->ssd_power_stat.ssd_bus_power_consumed;
			fprintf(outputfile, "%s #%d BUS: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], bus_total_energy, (bus_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d BUS:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], s->ssd_power_stat.ssd_bus_power_consumed, (s->ssd_power_stat.ssd_bus_power_consumed / bus_total_energy * 100.0), s->ssd_power_stat.ssd_bus_time_consumed);
			fprintf(outputfile, "%s #%d BUS:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], 0.0, 0.0, 0.0);
			fprintf(outputfile, "\n");
		}


		{
			//double ram_total_energy = (s->params.dram_input_voltage * s->params.dram_active_current * cpu_idle_time) + (s->params.dram_input_voltage * s->params.dram_idle_current * ssd_runoutstanding);
			double ram_total_energy = s->params.dram_input_voltage * s->params.dram_idle_current * simtime;
			fprintf(outputfile, "%s #%d RAM: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], ram_total_energy, (ram_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d RAM:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], (s->params.dram_input_voltage * s->params.dram_active_current * 0), 0.0, 0.0);
			fprintf(outputfile, "%s #%d RAM:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], (s->params.dram_input_voltage * s->params.dram_idle_current * simtime), 0.0, 0.0);
			fprintf(outputfile, "\n");
		}

		{
			fprintf(outputfile, "%s #%d Leakage: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], leakage_energy, (leakage_energy / total_energy * 100.0));
			fprintf(outputfile, "\n");
		}
	}

	{
		ssd_t *s = getssd(set[0]);
		int wcount = 0;
		int ccount = 0;
		for(i = 0 ; i < s->params.nchannel ; i++){
			wcount += s->CH[i].wcount;
			ccount += s->CH[i].ccount;
		}
		fprintf(outputfile, "%s #%d Way Delay Count:\t%d \n",sourcestr, set[0], wcount);
		fprintf(outputfile, "%s #%d Channel Delay Count:\t%d \n",sourcestr, set[0], ccount);
	}

	fprintf(outputfile, "\n");
    fprintf(outputfile, "\n\n");

	fprintf(outputfile, "\n\nSSD POWER CONSUMPTION STATISTICS(+Idle)\n");
	fprintf(outputfile, "---------------------------------------------\n\n");

	// Get the total energy of SSD
	total_energy = 0.0;
	for (i = 0; i < setsize; i ++) {
		int j;
		ssd_t *s = getssd(set[i]);
		
		for (j = 0; j < s->params.nelements; j ++) {
			double element_total_energy = 0.0;
			double runoutstanding = 0.0;
			double element_idle_time = 0.0;
			double element_idle_energy = 0.0;
			ssd_power_element_stat *stat = &(s->elements[j].power_stat);

			// get idle energy
			//element_idle_time = simtime - warmuptime - stat->acc_time;
			element_idle_time = simtime - stat->acc_time;
			//element_idle_time = s->acc_time - stat->acc_time;
			element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

			// get total element energy
			element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
									stat->erase_power_consumed + stat->bus_power_consumed + 
									element_idle_energy;

			total_energy += element_total_energy;
		}

		// get CPU energy
		//cpu_idle_time = stat_get_runval(&s->queue->idlestats);
		//ssd_runoutstanding = s->queue->base.runoutstanding + s->queue->timeout.runoutstanding + s->queue->priority.runoutstanding;
		//ssd_runoutstanding = simtime - cpu_idle_time - warmuptime;
		//cpu_active_energy = s->params.cpu_normal_mode_power * ssd_runoutstanding;
		//cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;
		cpu_idle_time = simtime - s->acc_time;
		cpu_active_energy = s->params.cpu_normal_mode_power * s->acc_time;
		cpu_idle_energy = s->params.cpu_idle_mode_power * cpu_idle_time;
		ssd_runoutstanding = s->acc_time;

		total_energy += cpu_active_energy;
		total_energy += cpu_idle_energy;
		
		// get BUS energy
		total_energy += s->ssd_power_stat.ssd_bus_power_consumed;
//		total_energy += s->params.ssd_bus_current * 5 * simtime;

		// get RAM energy
		//total_energy += s->params.dram_input_voltage * s->params.dram_active_current * cpu_idle_time;
		//total_energy += s->params.dram_input_voltage * s->params.dram_idle_current * ssd_runoutstanding;
		total_energy += s->params.dram_input_voltage * s->params.dram_idle_current * simtime;

		// get Leakage energy
		leakage_energy = s->params.leakage_power * simtime;
		total_energy += leakage_energy;
		
	}

	fprintf(outputfile, "SSD Total energy used:\t\t%f mJ\n", total_energy );
	fprintf(outputfile, "SSD Running Time, Idle Time:\t%f, \t%f\n", ssd_runoutstanding, cpu_idle_time);
	fprintf(outputfile, "\n");
	
	for (i = 0; i < setsize; i ++) {
		int j;
		double element_read_energy = 0.0;
		double element_write_energy = 0.0;
		double element_erase_energy = 0.0;
		double element_bus_energy = 0.0;
		double telement_idle_energy = 0.0;
		double telement_total_energy = 0.0;
		ssd_t *s = getssd(set[i]);

		for (j = 0; j < s->params.nelements; j ++) {
			double element_total_energy = 0.0;
			double runoutstanding = 0.0;
			double element_idle_time = 0.0;
			double element_idle_energy = 0.0;
			ssd_power_element_stat *stat = &(s->elements[j].power_stat);
			struct ioq * queue = s->elements[j].queue;

			// get idle energy
			runoutstanding = queue->base.runoutstanding + queue->timeout.runoutstanding + queue->priority.runoutstanding;
			//element_idle_time = simtime - warmuptime - stat->acc_time;
			element_idle_time = simtime - stat->acc_time;
			//element_idle_time = s->acc_time - stat->acc_time;
			element_idle_energy = s->params.flash_input_voltage * s->params.flash_idle_current * element_idle_time;

			// get total element energy
			element_total_energy = stat->read_power_consumed + stat->write_power_consumed + 
									stat->erase_power_consumed + stat->bus_power_consumed + 
									element_idle_energy;

			telement_total_energy += element_total_energy;
			element_read_energy += stat->read_power_consumed;
			element_write_energy += stat->write_power_consumed;
			element_erase_energy += stat->erase_power_consumed;
			element_bus_energy += stat->bus_power_consumed;
			telement_idle_energy += element_idle_energy;

			fprintf(outputfile, "%s #%d Flash #%d: Total energy used:\t\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], j, element_total_energy, (element_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Read operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->read_power_consumed, (stat->read_power_consumed / element_total_energy * 100.0), stat->num_reads);
			fprintf(outputfile, "%s #%d Flash #%d:     Write operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->write_power_consumed, (stat->write_power_consumed / element_total_energy * 100.0), stat->num_writes);
			fprintf(outputfile, "%s #%d Flash #%d:     Erase operation energy:\t%f mJ (%.2f%%) - count:%d\n",
				sourcestr, set[i], j, stat->erase_power_consumed, (stat->erase_power_consumed / element_total_energy * 100.0), stat->num_erase);
			fprintf(outputfile, "%s #%d Flash #%d:     Bus transfer energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], j, stat->bus_power_consumed, (stat->bus_power_consumed / element_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Idle status energy:\t%f mJ (%.2f%%) - time:%f\n",
				sourcestr, set[i], j, element_idle_energy, (element_idle_energy / element_total_energy * 100.0), element_idle_time);

			fprintf(outputfile, "\n");
		}

		{
			fprintf(outputfile, "%s #%d Flash: Total energy used:\t\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], telement_total_energy, (telement_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Read operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_read_energy, (element_read_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Write operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_write_energy, (element_write_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Erase operation energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_erase_energy, (element_erase_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash #%d:     Bus transfer energy:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], element_bus_energy, (element_bus_energy / telement_total_energy * 100.0));
			fprintf(outputfile, "%s #%d Flash:     Idle operation energy:\t%f mJ (%.2f%%)\n\n",
				sourcestr, set[i], telement_idle_energy, (telement_idle_energy / telement_total_energy * 100.0));
		}

		{
			double cpu_total_energy = cpu_active_energy + cpu_idle_energy;
			fprintf(outputfile, "%s #%d CPU: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], cpu_total_energy, (cpu_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d CPU:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], cpu_active_energy, (cpu_active_energy / cpu_total_energy * 100.0), ssd_runoutstanding);
			fprintf(outputfile, "%s #%d CPU:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], cpu_idle_energy, (cpu_idle_energy / cpu_total_energy * 100.0), cpu_idle_time);
			fprintf(outputfile, "\n");
		}

		{
			double bus_total_energy = s->ssd_power_stat.ssd_bus_power_consumed;
			fprintf(outputfile, "%s #%d BUS: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], bus_total_energy, (bus_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d BUS:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], s->ssd_power_stat.ssd_bus_power_consumed, (s->ssd_power_stat.ssd_bus_power_consumed / bus_total_energy * 100.0), s->ssd_power_stat.ssd_bus_time_consumed);
			fprintf(outputfile, "%s #%d BUS:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], 0.0, 0.0, 0.0);
			fprintf(outputfile, "\n");
		}


		{
			//double ram_total_energy = (s->params.dram_input_voltage * s->params.dram_active_current * cpu_idle_time) + (s->params.dram_input_voltage * s->params.dram_idle_current * ssd_runoutstanding);
			double ram_total_energy = s->params.dram_input_voltage * s->params.dram_idle_current * simtime;
			fprintf(outputfile, "%s #%d RAM: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], ram_total_energy, (ram_total_energy / total_energy * 100.0));
			fprintf(outputfile, "%s #%d RAM:     Active energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], (s->params.dram_input_voltage * s->params.dram_active_current * 0), 0.0, 0.0);
			fprintf(outputfile, "%s #%d RAM:     Idle energy:\t%f mJ (%.2f%%) - time taken: %f\n",
				sourcestr, set[i], (s->params.dram_input_voltage * s->params.dram_idle_current * simtime), 0.0, 0.0);
			fprintf(outputfile, "\n");
		}

		{
			fprintf(outputfile, "%s #%d Leakage: Total energy used:\t%f mJ (%.2f%%)\n",
				sourcestr, set[i], leakage_energy, (leakage_energy / total_energy * 100.0));
			fprintf(outputfile, "\n");
		}
	}

	fprintf(outputfile, "---------------------------------------------\n\n");
}
//--

/******************************************************
 * Extern Function
 ******************************************************/
void ssd_cleanstats (void)
{
   int i, j;

   for (i=0; i<MAXDEVICES; i++) {
      ssd_t *currdisk = getssd (i);
      if (currdisk) {
          ioqueue_cleanstats(currdisk->queue);
          for (j=0; j<currdisk->params.nelements; j++)
              ioqueue_cleanstats(currdisk->elements[j].queue);
      }
   }
}

void ssd_printstats (void)
{
   struct ioq * queueset[MAXDEVICES*SSD_MAX_ELEMENTS];
   int set[MAXDEVICES];
   int i,j;
   int reqcnt = 0;
   char prefix[80];
   int diskcnt;
   int queuecnt;

   fprintf(outputfile, "\nSSD STATISTICS\n");
   fprintf(outputfile, "---------------------\n\n");

   sprintf_s3(prefix, 80, "ssd ");

   diskcnt = 0;
   queuecnt = 0;
   for (i=0; i<MAXDEVICES; i++) {
      ssd_t *currdisk = getssd (i);
      if (currdisk) {
         struct ioq *q = currdisk->queue;
         queueset[queuecnt] = q;
         queuecnt++;
         reqcnt += ioqueue_get_number_of_requests(q);
         diskcnt++;
      }
   }
   assert (diskcnt == numssds);

   if (reqcnt == 0) {
      fprintf(outputfile, "No ssd requests encountered\n");
      return;
   }

	// temp : micky
	ioqueue_printstats(queueset, queuecnt, prefix);
	//--

   diskcnt = 0;
   for (i=0; i<MAXDEVICES; i++) {
      ssd_t *currdisk = getssd (i);
      if (currdisk) {
         set[diskcnt] = i;
         diskcnt++;
      }
   }
   assert (diskcnt == numssds);

	// temp : micky
	ssd_acctime_printstats(set, numssds, prefix);
	ssd_other_printstats(set, numssds, prefix);
	//--
	
   ssd_printcleanstats(set, numssds, prefix);

	//@20090831-Micky:print power consumptions of SSD
	fprintf (outputfile, "\n\n");
	ssd_power_printstats(set, numssds, prefix);
	//--

   fprintf (outputfile, "\n\n");

   for (i=0; i<numssds; i++) {
      ssd_t *currdisk = getssd (set[i]);
      if (currdisk->printstats == FALSE) {
          continue;
      }
      reqcnt = 0;
      {
          struct ioq *q = currdisk->queue;
          reqcnt += ioqueue_get_number_of_requests(q);
      }
      if (reqcnt == 0) {
          fprintf(outputfile, "No requests for ssd #%d\n\n\n", set[i]);
          continue;
      }
      fprintf(outputfile, "ssd #%d:\n\n", set[i]);
      sprintf_s4(prefix, 80, "ssd #%d ", set[i]);
      {
          struct ioq *q;
          q = currdisk->queue;
          ioqueue_printstats(&q, 1, prefix);
      }
      for (j=0;j<currdisk->params.nelements;j++) {
          char pprefix[100];
          struct ioq *q;
          sprintf_s5(pprefix, 100, "%s elem #%d ", prefix, j);
          q = currdisk->elements[j].queue;
          ioqueue_printstats(&q, 1, pprefix);
      }
      ssd_acctime_printstats(&set[i], 1, prefix);
      ssd_other_printstats(&set[i], 1, prefix);
      fprintf (outputfile, "\n\n");
   }


}

void ssd_printsetstats (int *set, int setsize, char *sourcestr)
{
   int i;
   struct ioq * queueset[MAXDEVICES*SSD_MAX_ELEMENTS];
   int queuecnt = 0;
   int reqcnt = 0;
   char prefix[80];

   //using more secure functions
   sprintf_s4(prefix, 80, "%sssd ", sourcestr);
   for (i=0; i<setsize; i++) {
      ssd_t *currdisk = getssd (set[i]);
      struct ioq *q = currdisk->queue;
      queueset[queuecnt] = q;
      queuecnt++;
      reqcnt += ioqueue_get_number_of_requests(q);
   }
   if (reqcnt == 0) {
      fprintf (outputfile, "\nNo ssd requests for members of this set\n\n");
      return;
   }
   ioqueue_printstats(queueset, queuecnt, prefix);

   ssd_acctime_printstats(set, setsize, prefix);
   ssd_other_printstats(set, setsize, prefix);
}


