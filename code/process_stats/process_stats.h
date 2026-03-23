#ifndef PROCESS_STATS_PROCESS_STATS_H_
#define PROCESS_STATS_PROCESS_STATS_H_

#include <unistd.h>
#include <stdbool.h>
#ifdef UNIT_TEST
#include "uthash.h"
#endif

typedef struct{
	bool cpu_average_requested;
	int cpu_average_pids_to_display;

	bool rss_average_requested;
	int rss_average_pids_to_display;

	bool rss_increase_requested;
	int rss_increase_pids_to_display;

	bool rss_delta_requested;
	int rss_delta_pids_to_display;

	bool bytes_read_requested;
	int bytes_read_pids_to_display;

	bool bytes_write_requested;
	int bytes_write_pids_to_display;

	bool read_rate_requested;
	int read_rate_pids_to_display;

	bool write_rate_requested;
	int write_rate_pids_to_display;

	bool fds_increase_requested;
	int fds_increase_pids_to_display;

}process_stats_metrics_arguments;

#ifdef UNIT_TEST
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

	/* FD data */
	unsigned long initial_num_of_fds;
	long fd_delta;

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


typedef struct
{
	pid_t pid;
	char comm[64];
	char state;
	pid_t ppid;
	unsigned long utime;
	unsigned long stime;
	long rssKb;
	double timestamp;
	char h_r_timestamp[64];
	int threads;
	unsigned long long read_kbytes;
	unsigned long long write_kbytes;
	bool bo_is_rss_valid;
	bool bo_is_io_valid;
	unsigned long  number_of_fds;
}process_state_input_t;

extern void process_stats_initialize(const char* prog);
extern void process_stats_update(process_state_input_t* input);
extern void process_stats_snapshot_end(void);
extern void process_stats_print_metrics(process_stats_metrics_arguments * args, uint64_t interval_ms);
extern void process_stats_deinit(void);
#ifdef UNIT_TEST
extern process_state_t* process_stats_get_all(void);
#endif


#endif /* PROCESS_STATS_PROCESS_STATS_H_ */
