#ifndef ARGS_PARSER_ARGS_PARSER_H_
#define ARGS_PARSER_ARGS_PARSER_H_

#include <stdint.h>
#include <stdbool.h>
#include "process_stats.h"

#define PROCESS_ANALYZER_VERSION	"process_analyzer V1.1"

typedef struct {
	uint64_t 						interval_ms;   		// snapshot interval in ms
	int      						count;         		// number of snapshots
	bool 							delete_old_files;	// delete old files generated during previous builds
	process_stats_metrics_arguments	end_metrics_args;	// end metrics arguments
}ap_arguments;

typedef enum{
	parse_args_error = -1,
	parse_args_ok = 0
}parse_args_status;

extern parse_args_status ap_parse_args(int argc, char **argv, ap_arguments *cfg);

#endif /* ARGS_PARSER_ARGS_PARSER_H_ */
