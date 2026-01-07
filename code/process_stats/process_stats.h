#ifndef PROCESS_STATS_PROCESS_STATS_H_
#define PROCESS_STATS_PROCESS_STATS_H_

#include <unistd.h>



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


extern void process_stats_update(process_state_input_t* input);
extern void process_stats_snapshot_end(void);
extern void process_stats_print_top_cpu(int top_n);



#endif /* PROCESS_STATS_PROCESS_STATS_H_ */
