#include <stdio.h>		//fprintf, stderr
#include <stdlib.h>     // calloc, free
#include <string.h>     // strncpy
#include <stdbool.h>    // bool
#include <sys/types.h>  // pid_t
#include <unistd.h>     // sysconf (CPU usage)
#include "uthash.h"
#include "process_stats.h"


typedef struct{

	/* current values */
	pid_t pid;                 /* key */
	char comm[64];
	unsigned long utime;
	unsigned long stime;
	long rss_kb;
	int threads;
	char state;

	/*io data*/
	unsigned long long total_read_kbytes;
	unsigned long long total_write_kbytes;

	/* previous snapshot */
	unsigned long prev_cpu_ticks;
	long prev_rss_kb;
	double prev_timestamp;

	unsigned long number_of_records;
	/* calculated cpu data */
	double cpu_usage;          /* % */
	double average_cpu_usage;
	double cpu_usage_sum;
	long cpu_usage_samples;

	/* calculated rss data*/
	long rss_initial_kb;
	long rss_average_kb;
	long rss_sum;
	long rss_variation_since_startup;

	/* calculated IO data*/
	unsigned long long prev_read_kbytes;
	unsigned long long prev_write_kbytes;

	double avg_read_rate_kb_s;
	double avg_write_rate_kb_s;
	double io_time_acc;

	/* housekeeping */
	bool seen_in_snapshot;

	UT_hash_handle hh;
} process_state_t;

static process_state_t *g_process_table = NULL;
static bool is_module_initialized = false;

static process_state_t *get_or_create_process(const process_state_input_t *input)
{
    process_state_t *ps = NULL;

    HASH_FIND_INT(g_process_table, &input->pid, ps);
    if (!ps) {
        ps = calloc(1, sizeof(*ps));
        if (!ps)
        {
        	fprintf(stderr, "get_or_create_process: Failed to allocate memory for the process %d",input->pid);
            return NULL;
        }

        ps->pid = input->pid;
        ps->utime = input->utime;
        ps->stime = input->stime;
        ps->rss_kb = input->rssKb;
        ps->threads = input->threads;
        ps->prev_cpu_ticks = input->utime + input->stime;
        ps->prev_rss_kb = input->rssKb;
        ps->state = input->state;

        HASH_ADD_INT(g_process_table, pid, ps);
    }

    return ps;
}


static double compute_cpu_usage(
    unsigned long prev_ticks,
    unsigned long curr_ticks,
    double prev_ts,
    double curr_ts)
{
    double delta_ticks = (double)(curr_ticks - prev_ticks);
    double delta_time = curr_ts - prev_ts;

    if (delta_time <= 0.0)
        return 0.0;

    long hz = sysconf(_SC_CLK_TCK);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);

    return (delta_ticks / hz) / delta_time / cpus * 100.0;
}


static int cmp_average_cpu(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->average_cpu_usage > pa->average_cpu_usage) return 1;
	if (pb->average_cpu_usage < pa->average_cpu_usage) return -1;
	return 0;
}


static int cmp_average_rss(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->rss_average_kb > pa->rss_average_kb) return 1;
	if (pb->rss_average_kb < pa->rss_average_kb) return -1;
	return 0;
}

static int cmp_rss_variation(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->rss_variation_since_startup > pa->rss_variation_since_startup) return 1;
	if (pb->rss_variation_since_startup < pa->rss_variation_since_startup) return -1;
	return 0;
}

static int cmp_rss_delta(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (labs(pb->rss_variation_since_startup) > labs(pa->rss_variation_since_startup)) return 1;
	if (labs(pb->rss_variation_since_startup) < labs(pa->rss_variation_since_startup)) return -1;
	return 0;
}

static int compare_total_read_kbytes(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->total_read_kbytes > pa->total_read_kbytes) return 1;
	if (pb->total_read_kbytes < pa->total_read_kbytes) return -1;
	return 0;
}

static int compare_total_write_kbytes(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->total_write_kbytes > pa->total_write_kbytes) return 1;
	if (pb->total_write_kbytes < pa->total_write_kbytes) return -1;
	return 0;
}

static int compare_avg_read_rate(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->avg_read_rate_kb_s > pa->avg_read_rate_kb_s) return 1;
	if (pb->avg_read_rate_kb_s < pa->avg_read_rate_kb_s) return -1;
	return 0;
}

static int compare_avg_write_rate(const void *a, const void *b)
{
	const process_state_t *pa = *(const process_state_t**)a;
	const process_state_t *pb = *(const process_state_t**)b;
	if (pb->avg_write_rate_kb_s > pa->avg_write_rate_kb_s) return 1;
	if (pb->avg_write_rate_kb_s < pa->avg_write_rate_kb_s) return -1;
	return 0;
}

static void calculate_io_data(process_state_input_t* input,process_state_t* proc_state)
{
    if (proc_state->number_of_records > 0) {

        double dt = input->timestamp - proc_state->prev_timestamp;

        if (dt > 0.0) {

            uint64_t read_delta  = 0;
            uint64_t write_delta = 0;

            if (input->read_kbytes >= proc_state->prev_read_kbytes)
                read_delta = input->read_kbytes - proc_state->prev_read_kbytes;

            if (input->write_kbytes >= proc_state->prev_write_kbytes)
                write_delta = input->write_kbytes - proc_state->prev_write_kbytes;

            proc_state->total_read_kbytes  += read_delta;
            proc_state->total_write_kbytes += write_delta;
            proc_state->io_time_acc        += dt;
        }
    }
    proc_state->prev_read_kbytes  = input->read_kbytes;
    proc_state->prev_write_kbytes = input->write_kbytes;
    proc_state->prev_timestamp    = input->timestamp;
}

void process_stats_snapshot_end(void)
{
	/**un-commenting this code optimizes memory usage of this tool but will cause processes
	 * which ended before the analyzer to be missed from the end of the execution analysis*/
#if 0
	process_state_t *ps, *tmp;
	HASH_ITER(hh, g_process_table, ps, tmp)
	{
		if(!ps->seen_in_snapshot)
		{
			//process stopped, deallocate memory
			 HASH_DEL(g_process_table, ps);
			 free(ps);
		}
		else
		{
			ps->seen_in_snapshot = false;
		}
	}
#endif
}

void process_stats_initialize(void)
{
	is_module_initialized = true;
}

void process_stats_print_metrics(process_stats_metrics_arguments * args)
{
	if(true == is_module_initialized)
	{
		int count = HASH_COUNT(g_process_table);
		if (count == 0) return;

		process_state_t **arr = malloc(sizeof(process_state_t*) * count);
		if (!arr) return;

		int i = 0;
		process_state_t *ps, *tmp;
		HASH_ITER(hh, g_process_table, ps, tmp)
		{
			arr[i++] = ps;

			if(true == args->cpu_average_requested)
			{
				ps->average_cpu_usage = ps->cpu_usage_sum / ps->cpu_usage_samples;
			}
			if(true == args->rss_average_requested)
			{
				ps->rss_average_kb  = ps->rss_sum / ps->number_of_records;
			}

			if(true == args->read_rate_requested)
			{
				ps->avg_read_rate_kb_s = ps->total_read_kbytes/ps->io_time_acc;
			}
			if(true == args->write_rate_requested)
			{
				ps->avg_write_rate_kb_s = ps->total_write_kbytes/ps->io_time_acc;
			}
		}

		if(true == args->cpu_average_requested)
		{
			//calculate average only when requested

			qsort(arr, count, sizeof(process_state_t*), cmp_average_cpu);

			int n = args->cpu_average_pids_to_display < count ? args->cpu_average_pids_to_display : count;
			printf("\nTop %d processes by average CPU usage:\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "AVG CPU%", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8.2f %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->average_cpu_usage,
						ps->threads,
						ps->number_of_records);
			}
		}

		if(true == args->rss_average_requested)
		{
			int n = args->rss_average_pids_to_display < count ? args->rss_average_pids_to_display : count;
			//calculate average only when requested
			qsort(arr, count, sizeof(process_state_t*), cmp_average_rss);
			printf("\nTop %d processes by average RSS (KB):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "AVG RSS (KB)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8ld %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->rss_average_kb,
						ps->threads,
						ps->number_of_records);
			}
		}

		if(true == args->rss_increase_requested)
		{
			int n = args->rss_increase_pids_to_display < count ? args->rss_increase_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), cmp_rss_variation);
			printf("\nTop %d processes by RSS increase since startup (KB):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "RSS increase (KB)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8ld %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->rss_variation_since_startup,
						ps->threads,
						ps->number_of_records);
			}
		}

		if(true == args->rss_delta_requested)
		{
			int n = args->rss_delta_pids_to_display < count ? args->rss_delta_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), cmp_rss_delta);
			printf("\nTop %d processes by RSS delta since startup (KB):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "RSS delta (KB)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8ld %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->rss_variation_since_startup,
						ps->threads,
						ps->number_of_records);
			}

		}

		if(true == args->bytes_read_requested)
		{
			int n = args->bytes_read_pids_to_display < count ? args->bytes_read_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_total_read_kbytes);
			printf("\nTop %d processes by bytes read from disk (KB):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "Bytes read (KB)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8lld %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->total_read_kbytes,
						ps->threads,
						ps->number_of_records);
			}

		}

		if(true == args->bytes_write_requested)
		{
			int n = args->bytes_write_pids_to_display < count ? args->bytes_write_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_total_write_kbytes);
			printf("\nTop %d processes by bytes written to disk (KB):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "Bytes written (KB)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8lld %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->total_write_kbytes,
						ps->threads,
						ps->number_of_records);
			}
		}

		if(true == args->read_rate_requested)
		{
			int n = args->read_rate_pids_to_display < count ? args->read_rate_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_avg_read_rate);
			printf("\nTop %d processes by disk read rate (KB/s):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "RR(KB/s)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8.2f %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->avg_read_rate_kb_s,
						ps->threads,
						ps->number_of_records);
			}
		}

		if(true == args->write_rate_requested)
		{
			int n = args->write_rate_pids_to_display < count ? args->write_rate_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_avg_write_rate);
			printf("\nTop %d processes by disk write rate (KB/s):\n", n);
			printf("%-6s %-20s %-6s %-12s %-8s %-15s\n","PID", "COMM", "STATE", "WR(KB/s)", "THREADS", "RECORDS");
			for (i = 0; i < n; i++)
			{
				ps = arr[i];
				printf("%-6d %-20.20s %-6c %8.2f %8d %15lu\n",
						ps->pid,
						ps->comm,
						ps->state,
						ps->avg_write_rate_kb_s,
						ps->threads,
						ps->number_of_records);
			}
		}


		free(arr);
	}
	else
	{
		// module not initialized, nothing to do
	}

}

void process_stats_update(process_state_input_t* input)
{
	if(true == is_module_initialized)
	{
		process_state_t* proc_state = get_or_create_process(input);

		if(proc_state)
		{
			unsigned long curr_ticks = input->utime + input->stime;
			unsigned long prev_ticks = proc_state->prev_cpu_ticks;


			proc_state->cpu_usage = 0.0;
			if (proc_state->number_of_records > 0)
			{
				if (curr_ticks >= proc_state->prev_cpu_ticks)
				{
					double delta_time = input->timestamp - proc_state->prev_timestamp;
					if(delta_time > 0.0)
					{
						proc_state->cpu_usage = compute_cpu_usage(prev_ticks, curr_ticks, proc_state->prev_timestamp, input->timestamp);
						// calculate sum for avg cpu usage
						proc_state->cpu_usage_sum += proc_state->cpu_usage;
						proc_state->cpu_usage_samples++;
					}
				}
			}

			if(proc_state->number_of_records == 0)
			{
				proc_state->rss_initial_kb = input->rssKb;
			}


			proc_state->prev_cpu_ticks = curr_ticks;
			proc_state->prev_rss_kb = input->rssKb;


			proc_state->utime = input->utime;
			proc_state->stime = input->stime;
			proc_state->rss_kb = input->rssKb;
			proc_state->threads = input->threads;

			strncpy(proc_state->comm,input->comm,sizeof(proc_state->comm));

			proc_state->number_of_records++;


			//calculate sum for avg rss usage
			proc_state->rss_sum += proc_state->rss_kb;

			//calculate rss variation
			proc_state->rss_variation_since_startup = proc_state->rss_kb - proc_state->rss_initial_kb;

            //calculate io data
			calculate_io_data(input, proc_state);

			proc_state->prev_timestamp = input->timestamp;

			proc_state->seen_in_snapshot = true;
		}
		else
		{
			//memory allocation failed, nothing to do
		}
	}
	else
	{
		// no analysis is performed if the module is not initialized
	}

}
