#ifndef PROCESS_STATS_PROCESS_STATS_H_
#define PROCESS_STATS_PROCESS_STATS_H_

#include <unistd.h>
#include <stdbool.h>

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

}process_stats_metrics_arguments;


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
}process_state_input_t;

extern void process_stats_initialize(const char* prog);
extern void process_stats_update(process_state_input_t* input);
extern void process_stats_snapshot_end(void);
extern void process_stats_print_metrics(process_stats_metrics_arguments * args, uint64_t interval_ms);



#endif /* PROCESS_STATS_PROCESS_STATS_H_ */
