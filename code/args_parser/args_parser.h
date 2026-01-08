#ifndef ARGS_PARSER_ARGS_PARSER_H_
#define ARGS_PARSER_ARGS_PARSER_H_

#include <stdint.h>
#include <stdbool.h>
#include "process_stats.h"


typedef struct {
	uint64_t 						interval_ms;   		// snapshot interval in ms
	int      						count;         		// number of snapshots
	process_stats_metrics_arguments	end_metrics_args;	// end metrics arguments
}ap_arguments;

typedef enum{
	parse_args_error = -1,
	parse_args_ok = 0
}parse_args_status;

extern parse_args_status ap_parse_args(int argc, char **argv, ap_arguments *cfg);

#endif /* ARGS_PARSER_ARGS_PARSER_H_ */
