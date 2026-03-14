#ifndef ARGS_PARSER_ARGS_PARSER_H_
#define ARGS_PARSER_ARGS_PARSER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "process_stats.h"

#define PROCESS_ANALYZER_VERSION	"process_analyzer V1.2"

typedef struct{
	int*                            filter_pids;
	size_t                          filter_pids_count;
	size_t                          filter_pids_capacity;
	char**                          filter_comms;
	size_t                          filter_comms_count;
	size_t                          filter_comms_capacity;
}ap_pid_whitelist;

typedef struct {
	uint64_t                        interval_ms;        // snapshot interval in ms
	int                             count;              // number of snapshots
	bool 	                        delete_old_files;   // delete old files generated during previous builds
	process_stats_metrics_arguments	end_metrics_args;   // end metrics arguments
	ap_pid_whitelist                pid_whitelist;      // white list containing the PIDs to be included in the analysis
}ap_arguments;

typedef enum{
	parse_args_error = -1,
	parse_args_ok = 0
}parse_args_status;

extern parse_args_status ap_parse_args(int argc, char **argv, ap_arguments *cfg);
extern void ap_free_arguments(ap_arguments *cfg);

#endif /* ARGS_PARSER_ARGS_PARSER_H_ */
