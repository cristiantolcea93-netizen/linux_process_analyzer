#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "args_parser.h"


static parse_args_status parse_count(const char *arg, int *out_count);
static void print_usage(const char *prog);
static parse_args_status parse_duration_ms(const char *arg, uint64_t *out_ms);


static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -i, --interval <dur>   Interval (e.g. 500ms, 1s, 2m)\n");
    printf("  -n, --count <N>        Number of snapshots\n");
    printf("  -h, --help             Show this help\n");
}

static parse_args_status parse_count(const char *arg, int *out_count)
{
    char *endptr = NULL;
    errno = 0;

    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0' || value <= 0)
        return parse_args_error;

    *out_count = (int)value;
    return parse_args_ok;
}


static parse_args_status parse_duration_ms(const char *arg, uint64_t *out_ms)
{
    size_t len = strlen(arg);
    if (len < 2)
        return parse_args_error;

    char *endptr = NULL;
    errno = 0;

    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || value <= 0)
        return -1;

    const char *unit = endptr;

    if (strcmp(unit, "ms") == 0) {
        *out_ms = (uint64_t)value;
    } else if (strcmp(unit, "s") == 0) {
        *out_ms = (uint64_t)value * 1000;
    } else if (strcmp(unit, "m") == 0) {
        *out_ms = (uint64_t)value * 60 * 1000;
    } else {
        return parse_args_error;
    }

    return parse_args_ok;
}


parse_args_status ap_parse_args(int argc, char **argv, ap_arguments *cfg)
{
	bool boIntervalProvided = false;
	bool boCountProvided = false;
	static struct option long_opts[] = {
			{ "interval", required_argument, 0, 'i' },
			{ "count",    required_argument, 0, 'n' },
			{ "help",     no_argument,       0, 'h' },
			{ 0, 0, 0, 0 }
	};

	int opt;
	    while ((opt = getopt_long(argc, argv, "i:n:h", long_opts, NULL)) != -1) {
	        switch (opt) {
	        case 'i':
	            if (parse_duration_ms(optarg, &cfg->interval_ms) != parse_args_ok)
	            {
	                fprintf(stderr, "Invalid interval: %s\n", optarg);
	                return parse_args_error;
	            }
	            else
	            {
	            	boIntervalProvided = true;
	            }
	            break;

	        case 'n':
	            if (parse_count(optarg, &cfg->count) != parse_args_ok)
	            {
	                fprintf(stderr, "Invalid count: %s\n", optarg);
	                return parse_args_error;
	            }
	            else
	            {
	            	boCountProvided = true;
	            }
	            break;

	        case 'h':
	        default:
	        	print_usage(argv[0]);
	            return -1;
	        }
	    }

	    if((false == boCountProvided) || (false == boIntervalProvided))
	    {
	    	fprintf(stderr, "Missing mandatory parameter. interval provided %d, count provided %d \n", boIntervalProvided, boCountProvided);
	    	print_usage(argv[0]);
	    	return parse_args_error;
	    }

	    return parse_args_ok;
}
