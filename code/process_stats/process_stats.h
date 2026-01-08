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

}process_stats_metrics_arguments;


typedef struct
{
	pid_t pid;
	char comm[64];
	char state;
	unsigned long utime;
	unsigned long stime;
	long rssKb;
	double timestamp_sec;
	int threads;
}process_state_input_t;

extern void process_stats_initialize(void);
extern void process_stats_update(process_state_input_t* input);
extern void process_stats_snapshot_end(void);
extern void process_stats_print_metrics(process_stats_metrics_arguments * args);



#endif /* PROCESS_STATS_PROCESS_STATS_H_ */
