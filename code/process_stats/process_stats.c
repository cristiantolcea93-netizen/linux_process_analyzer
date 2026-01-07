#include <stdio.h>		//fprintf, stderr
#include <stdlib.h>     // calloc, free
#include <string.h>     // strncpy
#include <stdbool.h>    // bool
#include <sys/types.h>  // pid_t
#include <unistd.h>     // sysconf (CPU usage)
#include "uthash.h"
#include "process_stats.h"


typedef struct{
    pid_t pid;                 /* key */

    char comm[64];

    /* valori curente */
    unsigned long utime;
    unsigned long stime;
    long rss_kb;
    int threads;

    /* snapshot anterior */
    unsigned long prev_cpu_ticks;
    long prev_rss_kb;
    double prev_timestamp;

    /* rezultate calculate */
    double cpu_usage;          /* % */
    long rss_delta_kb;

    /* housekeeping */
    bool seen_in_snapshot;

    char state;

    UT_hash_handle hh;
} process_state_t;

static process_state_t *g_process_table = NULL;

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


static int cmp(const void *a, const void *b)
   {
       const process_state_t *pa = *(const process_state_t**)a;
       const process_state_t *pb = *(const process_state_t**)b;
       if (pb->cpu_usage > pa->cpu_usage) return 1;
       if (pb->cpu_usage < pa->cpu_usage) return -1;
       return 0;
   }


void process_stats_snapshot_end(void)
{
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
}

void process_stats_print_top_cpu(int top_n)
{
    int count = HASH_COUNT(g_process_table);
    if (count == 0) return;

    process_state_t **arr = malloc(sizeof(process_state_t*) * count);
    if (!arr) return;

    int i = 0;
    process_state_t *ps, *tmp;
    HASH_ITER(hh, g_process_table, ps, tmp) {
        arr[i++] = ps;
    }

    qsort(arr, count, sizeof(process_state_t*), cmp);

    int n = top_n < count ? top_n : count;
    printf("\nTop %d processes by CPU usage:\n", n);
    printf("%-6s %-20s %-8s %-6s %-6s\n", "PID", "COMM", "STATE", "CPU%", "THREADS");
    for (i = 0; i < n; i++) {
        ps = arr[i];
        printf("%-6d %-20s %-8c %-6.2f %-6d\n",
               ps->pid,
               ps->comm,
               ps->state,
               ps->cpu_usage,
               ps->threads);
    }

    free(arr);
}

void process_stats_update(process_state_input_t* input)
{
	process_state_t* proc_state = get_or_create_process(input);

	if(proc_state)
	{
		unsigned long curr_ticks = input->utime + input->stime;
		unsigned long prev_ticks = proc_state->prev_cpu_ticks;
		double delta_time = input->timestamp_sec - proc_state->prev_timestamp;

		if(proc_state->prev_timestamp > 0.0 && delta_time > 0.0)
		{
			proc_state->cpu_usage = compute_cpu_usage(prev_ticks, curr_ticks, proc_state->prev_timestamp, input->timestamp_sec);

			proc_state->rss_delta_kb = input->rssKb - proc_state->prev_rss_kb;
		}

		proc_state->prev_cpu_ticks = curr_ticks;
		proc_state->prev_rss_kb = input->rssKb;
		proc_state->prev_timestamp = input->timestamp_sec;

		proc_state->utime = input->utime;
		proc_state->stime = input->stime;
		proc_state->rss_kb = input->rssKb;
		proc_state->threads = input->threads;

		strncpy(proc_state->comm,input->comm,sizeof(proc_state->comm));
		proc_state->seen_in_snapshot = true;
	}
	else
	{
		//memory allocation failed, nothing to do
	}
}
