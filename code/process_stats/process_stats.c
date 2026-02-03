#define _XOPEN_SOURCE 700 	// strptime
#define _GNU_SOURCE 		// timegm
#include <stdio.h>			// fprintf, stderr
#include <stdlib.h>     	// calloc, free
#include <string.h>     	// strncpy
#include <stdbool.h>    	// bool
#include <sys/types.h>  	// pid_t
#include <unistd.h>     	// sysconf (CPU usage)
#include <time.h>			// strptime, timegm
#include "uthash.h"
#include "process_stats.h"
#include "process_snapshot.h"
#include "config.h"
#include "args_parser.h"


#ifndef UNIT_TEST
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
	bool bo_is_rss_initialized;
	unsigned long num_of_rss_records;

	/* calculated IO data*/
	unsigned long long prev_read_kbytes;
	unsigned long long prev_write_kbytes;

	double avg_read_rate_kb_s;
	double avg_write_rate_kb_s;
	double io_time_acc;
	bool bo_is_io_initialized;

	/* housekeeping */
	bool seen_in_snapshot;

	UT_hash_handle hh;
} process_state_t;
#endif

typedef enum {
	METRIC_DOUBLE,
	METRIC_INT64,
	METRIC_UINT64
} metric_value_type_t;

typedef struct {
	const char *json_key;            // "e.g. cpu_average"
	const char *value_key;           // "e.g. cpu_average"
	metric_value_type_t value_type;
	double   (*get_double)(process_state_t *);
	int64_t  (*get_int64)(process_state_t *);
} metric_desc_t;

#define PROC_STATS_SCHEMA_VERSION "1.0"

static process_state_t *g_process_table = NULL;
static bool is_module_initialized = false;
static const char* prog_name;
static FILE* pfJsonOutput = NULL;
static uint64_t g_number_of_snapshots = 0;
static char h_r_initial_timestamp[64];
static char h_r_last_timestamp[64];
static bool boIsFirstJsonMetric;

static double get_avg_cpu(process_state_t *ps);
static int64_t get_avg_rss(process_state_t *ps);
static int64_t get_rss_increase(process_state_t *ps);
static int64_t get_total_read(process_state_t *ps);
static double get_avg_read_rate(process_state_t *ps);
static int64_t get_total_write(process_state_t *ps);
static double get_avg_write_rate(process_state_t *ps);
static int64_t get_rss_delta(process_state_t *ps);

typedef enum
{
	avg_cpu = 0,
	avg_rss = 1,
	rss_incr = 2,
	rss_delta = 3,
	bytes_read = 4,
	read_rate = 5,
	written_bytes = 6,
	write_rate = 7
}t_metrics;

static const metric_desc_t g_metrics[] = {
		{
			.json_key = "cpu_average",
			.value_key = "cpu_avg_pct",
			.value_type = METRIC_DOUBLE,
			.get_double = get_avg_cpu
		},
		{
			.json_key = "rss_average",
			.value_key = "rss_avg_kb",
			.value_type = METRIC_INT64,
			.get_int64 = get_avg_rss
		},
		{
			.json_key = "rss_increase",
			.value_key = "rss_incr_kb",
			.value_type = METRIC_INT64,
			.get_int64 = get_rss_increase
		},
		{
			.json_key = "rss_delta",
			.value_key = "rss_delta_kb",
			.value_type = METRIC_INT64,
			.get_int64 = get_rss_delta
		},
		{
			.json_key = "kbytes_read",
			.value_key = "bytes_read_kb",
			.value_type = METRIC_INT64,
			.get_int64 = get_total_read
		},
		{
			.json_key = "read_rate",
			.value_key = "read_rate_kbps",
			.value_type = METRIC_DOUBLE,
			.get_double = get_avg_read_rate
		},
		{
			.json_key = "written_kbytes",
			.value_key = "written_bytes_kb",
			.value_type = METRIC_INT64,
			.get_int64 = get_total_write
		},
		{
			.json_key = "write_rate",
			.value_key = "write_rate_kbps",
			.value_type = METRIC_DOUBLE,
			.get_double = get_avg_write_rate
		}
	};

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
	if(true == input->bo_is_io_valid)
	{
		if (false == proc_state->bo_is_io_initialized)
		{
			proc_state->prev_read_kbytes  = input->read_kbytes;
			proc_state->prev_write_kbytes = input->write_kbytes;
			proc_state->bo_is_io_initialized = true;
			return;
		}

		double dt = input->timestamp - proc_state->prev_timestamp;

		if(dt <= 0.0)
			return;


		uint64_t read_delta  = 0;
		uint64_t write_delta = 0;

		if (input->read_kbytes >= proc_state->prev_read_kbytes)
			read_delta = input->read_kbytes - proc_state->prev_read_kbytes;

		if (input->write_kbytes >= proc_state->prev_write_kbytes)
			write_delta = input->write_kbytes - proc_state->prev_write_kbytes;

		proc_state->total_read_kbytes  += read_delta;
		proc_state->total_write_kbytes += write_delta;
		proc_state->io_time_acc        += dt;


		proc_state->prev_read_kbytes  = input->read_kbytes;
		proc_state->prev_write_kbytes = input->write_kbytes;
	}

}

static void calculate_rss_data(process_state_input_t* input, process_state_t* proc_state)
{
	if(true == input->bo_is_rss_valid)
	{
		if(false == proc_state->bo_is_rss_initialized)
		{
			proc_state->rss_initial_kb = input->rssKb;
			proc_state->bo_is_rss_initialized = true;
		}
		proc_state->prev_rss_kb = input->rssKb;
		proc_state->rss_kb = input->rssKb;
		//calculate sum for avg rss usage
		proc_state->rss_sum += proc_state->rss_kb;

		//calculate rss variation
		proc_state->rss_variation_since_startup = proc_state->rss_kb - proc_state->rss_initial_kb;
		proc_state->num_of_rss_records++;
	}
}

static void open_metrics_file(void)
{
	if(true == config_get_metrics_json_enabled())
	{
		int path_length = snprintf(NULL, 0, "%s/%s", config_get_output_dir(), CONFIG_METRICS_JSON) + 1;
		char* openfilePath = malloc(path_length);

		if(openfilePath == NULL)
		{
			fprintf(stderr,"process_stats_initialize: failed to allocate memory for openfilePath!\n");
		}
		else
		{
			snprintf(openfilePath, path_length, "%s/%s", config_get_output_dir(), CONFIG_METRICS_JSON);
			pfJsonOutput = fopen(openfilePath, "w");

			if(pfJsonOutput != NULL)
			{
				//all good
			}
			else
			{
				fprintf(stderr,"process_stats_initialize: failed to open %s\n", openfilePath);
			}


			free(openfilePath);
		}
	}
	else
	{
		//json output not enabled in the configuration
	}
}

static char *get_hostname_alloc(void)
{
    long max_len = -1;

#ifdef HOST_NAME_MAX
    max_len = HOST_NAME_MAX;
#else
    max_len = sysconf(_SC_HOST_NAME_MAX);
#endif

    if (max_len <= 0)
        max_len = 256; // fallback to 256

    /* +1 for null terminator */
    size_t buf_len = (size_t)max_len + 1;

    /* calloc → zero-init */
    char *hostname = calloc(buf_len, 1);
    if (!hostname)
        return NULL;

    if (gethostname(hostname, buf_len) != 0)
    {
        free(hostname);
        return NULL;
    }
    return hostname;
}

static void metrics_json_begin(
        uint64_t snapshot_count,
        uint32_t interval_ms,
        const char *version,
        const char *start_time_iso,
        const char *end_time_iso,
        const char *prog_name,
        double duration_sec)
{

	char *hostname = get_hostname_alloc();
	if (!hostname)
	    hostname = strdup("unknown");

	boIsFirstJsonMetric = true;

    fprintf(pfJsonOutput,
        "{\n"
        "\t\"meta\": {\n"
        "\t\t\"tool\": \"%s\",\n"
        "\t\t\"version\": \"%s\",\n"
        "\t\t\"schema_version\": \"%s\",\n"
        "\t\t\"hostname\": \"%s\",\n"
        "\t\t\"interval_ms\": %u,\n"
        "\t\t\"start_time\": \"%s\",\n"
        "\t\t\"end_time\": \"%s\",\n"
        "\t\t\"duration_sec\": %.3f,\n"
        "\t\t\"snapshots\": %lu\n"
        "\t},\n"
        "\t\"metrics\": {\n",
        prog_name,
        version,
        PROC_STATS_SCHEMA_VERSION,
        hostname,
        interval_ms,
        start_time_iso,
        end_time_iso,
        duration_sec,
        snapshot_count
    );

    free(hostname);

}


static void write_metric_block_json(const metric_desc_t *m, process_state_t **arr, int n)
{
	int i;

	if(pfJsonOutput != NULL)
	{
		if(false  == boIsFirstJsonMetric)
		{
			// add comma for the previous metric
			fprintf(pfJsonOutput, ",\n");
		}
		else
		{

			boIsFirstJsonMetric = false;
		}
		fprintf(pfJsonOutput, "\t\t\"%s\":[\n", m->json_key);

			for (i = 0; i < n; i++) {
				process_state_t *ps = arr[i];

				fprintf(pfJsonOutput,
					"\t\t\t{\n"
					"\t\t\t\"pid\": %d,\n"
					"\t\t\t\"comm\": \"%s\",\n"
					"\t\t\t\"state\": \"%c\",\n",
					ps->pid,
					ps->comm,
					ps->state
				);

				if (m->value_type == METRIC_DOUBLE) {
					fprintf(pfJsonOutput,
						"\t\t\t\"%s\": %.2f,\n",
						m->value_key,
						m->get_double(ps)
					);
				} else {
					fprintf(pfJsonOutput,
						"\t\t\t\"%s\": %lld,\n",
						m->value_key,
						(long long)m->get_int64(ps)
					);
				}

				fprintf(pfJsonOutput,
					"\t\t\t\"threads\": %d,\n"
					"\t\t\t\"records\": %lu\n"
					"\t\t\t}%s\n",
					ps->threads,
					ps->number_of_records,
					(i + 1 < n) ? "," : ""
				);
			}

			fprintf(pfJsonOutput, "\t\t]");
	}

}


static void metrics_json_end(void)
{
	if(pfJsonOutput != NULL)
	{
		fprintf(pfJsonOutput,
				"\n"
				"\t}\n"
				"}\n"
		);
		fclose(pfJsonOutput);
		boIsFirstJsonMetric = true;
		pfJsonOutput = NULL;
	}

}

static double parse_timestamp_to_double(const char *ts)
{
	struct tm tm = {0};
	char *ms_part;
	double seconds;

	/* Parse "YYYY-MM-DD HH:MM:SS" */
	if (!strptime(ts, "%Y-%m-%d %H:%M:%S", &tm))
		return -1.0;

	/* Convert to epoch seconds */
	seconds = (double)timegm(&tm);

	/* Parse milliseconds if present */
	ms_part = strchr(ts, '.');
	if (ms_part)
	{
		int ms = atoi(ms_part + 1);
		seconds += ms / 1000.0;
	}

	return seconds;
}

static double get_snapshot_duration(const char* h_r_initial_timestamp, const char* h_r_last_timestamp)
{
	double start_time = parse_timestamp_to_double(h_r_initial_timestamp);
	double end_time = parse_timestamp_to_double(h_r_last_timestamp);
	double duration_sec = 0.0;

	if (start_time > 0 && end_time > 0 && end_time >= start_time)
	{
		duration_sec = end_time - start_time;
	}
	return duration_sec;
}

static double get_avg_cpu(process_state_t *ps)
{
	return ps->average_cpu_usage;
}

static int64_t get_avg_rss(process_state_t *ps)
{
	return ps->rss_average_kb;
}

static int64_t get_rss_increase(process_state_t *ps)
{
	return ps->rss_variation_since_startup;
}

static int64_t get_total_read(process_state_t *ps)
{
	return ps->total_read_kbytes;
}

static double get_avg_read_rate(process_state_t *ps)
{
	return ps->avg_read_rate_kb_s;
}

static int64_t get_total_write(process_state_t *ps)
{
	return ps->total_write_kbytes;
}

static double get_avg_write_rate(process_state_t *ps)
{
	return ps->avg_write_rate_kb_s;
}

static int64_t get_rss_delta(process_state_t *ps)
{
	return ps->rss_variation_since_startup;
}



void process_stats_snapshot_end(void)
{
	if(true == is_module_initialized)
	{
		g_number_of_snapshots++;
	}


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

void process_stats_initialize(const char* prog)
{
	is_module_initialized = true;
	prog_name = prog;
}

void process_stats_print_metrics(process_stats_metrics_arguments * args, uint64_t interval_ms)
{
	if(true == is_module_initialized)
	{
		int count = HASH_COUNT(g_process_table);
		if (count == 0) return;

		process_state_t **arr = malloc(sizeof(process_state_t*) * count);
		if (!arr) return;

		int i = 0;
		process_state_t *ps, *tmp;

		open_metrics_file();

		double duration_sec = get_snapshot_duration((const char*)h_r_initial_timestamp,(const char*) h_r_last_timestamp);


		metrics_json_begin(g_number_of_snapshots, interval_ms, PROCESS_ANALYZER_VERSION, h_r_initial_timestamp, h_r_last_timestamp, prog_name, duration_sec);

		HASH_ITER(hh, g_process_table, ps, tmp)
		{
			arr[i++] = ps;

			if((true == args->cpu_average_requested) && (ps->cpu_usage_samples))
			{
				ps->average_cpu_usage = ps->cpu_usage_sum / ps->cpu_usage_samples;
			}
			if((true == args->rss_average_requested) && (ps->num_of_rss_records))
			{
				ps->rss_average_kb  = ps->rss_sum / ps->num_of_rss_records;
			}

			if((true == args->read_rate_requested) && (ps->io_time_acc))
			{
				ps->avg_read_rate_kb_s = ps->total_read_kbytes/ps->io_time_acc;
			}
			if(true == args->write_rate_requested && (ps->io_time_acc))
			{
				ps->avg_write_rate_kb_s = ps->total_write_kbytes/ps->io_time_acc;
			}
		}

		if(true == args->cpu_average_requested)
		{
			//calculate average only when requested

			qsort(arr, count, sizeof(process_state_t*), cmp_average_cpu);
			int n = args->cpu_average_pids_to_display < count ? args->cpu_average_pids_to_display : count;

			if(true == config_get_metrics_console_enabled())
			{
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

			write_metric_block_json(&g_metrics[avg_cpu],arr,n);
		}

		if(true == args->rss_average_requested)
		{
			int n = args->rss_average_pids_to_display < count ? args->rss_average_pids_to_display : count;
			//calculate average only when requested
			qsort(arr, count, sizeof(process_state_t*), cmp_average_rss);

			if(true == config_get_metrics_console_enabled())
			{
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
			write_metric_block_json(&g_metrics[avg_rss],arr,n);
		}

		if(true == args->rss_increase_requested)
		{
			int n = args->rss_increase_pids_to_display < count ? args->rss_increase_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), cmp_rss_variation);

			if(true == config_get_metrics_console_enabled())
			{
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
			write_metric_block_json(&g_metrics[rss_incr],arr,n);
		}

		if(true == args->rss_delta_requested)
		{
			int n = args->rss_delta_pids_to_display < count ? args->rss_delta_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), cmp_rss_delta);

			if(true == config_get_metrics_console_enabled())
			{
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
			write_metric_block_json(&g_metrics[rss_delta],arr,n);
		}

		if(true == args->bytes_read_requested)
		{
			int n = args->bytes_read_pids_to_display < count ? args->bytes_read_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_total_read_kbytes);

			if(true == config_get_metrics_console_enabled())
			{
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
			write_metric_block_json(&g_metrics[bytes_read],arr,n);
		}

		if(true == args->bytes_write_requested)
		{
			int n = args->bytes_write_pids_to_display < count ? args->bytes_write_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_total_write_kbytes);

			if(true == config_get_metrics_console_enabled())
			{
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
			write_metric_block_json(&g_metrics[written_bytes],arr,n);
		}

		if(true == args->read_rate_requested)
		{
			int n = args->read_rate_pids_to_display < count ? args->read_rate_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_avg_read_rate);

			if(true == args->bytes_write_requested)
			{
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
			write_metric_block_json(&g_metrics[read_rate],arr,n);
		}

		if(true == args->write_rate_requested)
		{
			int n = args->write_rate_pids_to_display < count ? args->write_rate_pids_to_display : count;
			qsort(arr, count, sizeof(process_state_t*), compare_avg_write_rate);


			if(true == args->bytes_write_requested)
			{
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
			write_metric_block_json(&g_metrics[write_rate],arr,n);
		}

		free(arr);

		metrics_json_end();
	}
	else
	{
		// module not initialized, nothing to do
	}

}

/*
 * Snapshot policy:
 * - CPU data is mandatory (stat read failure discards snapshot)
 * - RSS and IO are optional
 * - Optional metrics are initialized on first valid sample
 * - Invalid optional data is stored as -1 in .log and .jsonl files but excluded from aggregation
 */


void process_stats_update(process_state_input_t* input)
{
	if(true == is_module_initialized)
	{
		process_state_t* proc_state = get_or_create_process(input);

		if(0 == strlen(h_r_initial_timestamp))
		{
			/*initial timestamp not initialized, initialize it*/
			strncpy(h_r_initial_timestamp, input->h_r_timestamp,64);
			strncpy(h_r_last_timestamp, input->h_r_timestamp,64);
		}
		else
		{
			/*save the latest timestamp*/
			strncpy(h_r_last_timestamp, input->h_r_timestamp,64);
		}
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

			proc_state->prev_cpu_ticks = curr_ticks;
			proc_state->utime = input->utime;
			proc_state->stime = input->stime;

			proc_state->threads = input->threads;

			strncpy(proc_state->comm,input->comm,sizeof(proc_state->comm));

			proc_state->number_of_records++;


			//calculate rss data
			calculate_rss_data(input, proc_state);

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

void process_stats_deinit(void)
{
    process_state_t *cur, *tmp;

    /* Free process hash table */
    HASH_ITER(hh, g_process_table, cur, tmp)
    {
        HASH_DEL(g_process_table, cur);
        free(cur);
    }

    g_process_table = NULL;

    /* Close metrics file if open */
    if (pfJsonOutput)
    {
        fflush(pfJsonOutput);
        fclose(pfJsonOutput);
        pfJsonOutput = NULL;
    }

    /* Reset globals */
    is_module_initialized   = false;
    prog_name               = NULL;
    g_number_of_snapshots   = 0;
    boIsFirstJsonMetric     = true;

    memset(h_r_initial_timestamp, 0, sizeof(h_r_initial_timestamp));
    memset(h_r_last_timestamp, 0, sizeof(h_r_last_timestamp));
}


#ifdef UNIT_TEST
process_state_t* process_stats_get_all(void)
{
    return g_process_table;
}
#endif
