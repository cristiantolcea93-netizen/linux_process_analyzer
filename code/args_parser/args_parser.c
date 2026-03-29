#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include "args_parser.h"
#include "process_stats.h"
#include "config.h"

#define MAX_NUMBER_OF_SNAPSHOTS 0x7FFFFFFF
#define AP_INITIAL_FILTER_CAPACITY 8

//#define debug_whitelist un-comment this line to enable printing of the white list at the start of the program

static parse_args_status parse_integer_arg(const char *arg, int *out_count);
static parse_args_status parse_number_of_snapshots(const char* arg, int *out);
static void print_usage(const char *prog);
static parse_args_status parse_duration_ms(const char *arg, uint64_t *out_ms);
static parse_args_status append_filter_pid(ap_arguments *cfg, int pid);
static parse_args_status append_filter_comm(ap_arguments *cfg, const char *comm_start, size_t comm_len);
static void dump_whitelist_filters(ap_pid_whitelist* whitelist);

typedef enum
{
	filter_parse_pid = 0,
	filter_parse_comm = 1
} filter_parse_type;

static parse_args_status parse_filter_pid_list(const char *arg, ap_arguments *cfg, filter_parse_type type);
static parse_args_status parse_pid_token(const char *token_start, size_t token_len, int *out_pid);


static void print_usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("Options:\n");
	printf("  -i, --interval <dur>        Interval (e.g. 500ms, 1s, 2m)\n");
	printf("  -n, --count <N>             Number of snapshots. Use \"infinity\" to collect snapshots until the program is interrupted or terminated\n");
	printf("  -c, --cpu_usage <N>         Display N processes with the highest average CPU usage at the end of execution\n");
	printf("  -r, --rss_usage <N>         Display N processes with the highest average RSS usage at the end of execution\n");
	printf("  -s, --rss_increase <N>      Display N processes with the highest RSS increase at the end of execution\n");
	printf("  -d, --rss_delta <N>         Display N processes with the highest RSS delta at the end of execution\n");
	printf("  -e  --bytes_read <N>        Display N processes with the highest amount of KB read from the disk\n");
	printf("  -f  --bytes_write <N>       Display N processes with the highest amount of KB written to disk\n");
	printf("  -g  --read_rate <N>         Display N processes with the highest disk read rate (KB/s)\n");
	printf("  -a  --write_rate <N>        Display N processes with the highest disk write rate (KB/s)\n");
	printf("  -p  --opened_fds <N>        Display N processes with the highest number of opened file descriptors\n");
	printf("  -m  --fds_increase <N>      Display N processes with the highest increase in opened file descriptors\n");
	printf("  -o  --fds_delta <N>         Display N processes with the highest absolute delta in opened file descriptors\n");
	printf("  -k  --filter_by_pid <pid>   Comma-separated list of PIDs. If not provided all running processes are included\n");
	printf("  -l  --filter_by_name <name> Comma-separated list of process names.If not provided all running processes are included\n");
	printf("  -j  --delete_old_files      Delete the files stored during previous executions\n");
	printf("  -v  --version               Display the version of the tool and returns\n");
	printf("  -h, --help                  Show this help\n");
}

static parse_args_status parse_number_of_snapshots(const char* arg, int *out)
{
	if(strcmp(arg, "infinity") == 0)
	{
		// check if it is infinity
		*out = MAX_NUMBER_OF_SNAPSHOTS;
		return parse_args_ok;
	}
	else
	{
		// not infinity, handle it as a regular integer value
		return parse_integer_arg(arg, out);
	}
}


static parse_args_status parse_integer_arg(const char *arg, int *out_count)
{
    char *endptr = NULL;
    errno = 0;

    long value = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0' || value <= 0 || value > INT_MAX)
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
        return parse_args_error;

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

void ap_free_arguments(ap_arguments *cfg)
{
	if (cfg == NULL)
	{
		return;
	}

	if (cfg->pid_whitelist.filter_comms != NULL)
	{
		for (size_t i = 0; i < cfg->pid_whitelist.filter_comms_count; i++)
		{
			free(cfg->pid_whitelist.filter_comms[i]);
		}
		free(cfg->pid_whitelist.filter_comms);
	}

	free(cfg->pid_whitelist.filter_pids);

	cfg->pid_whitelist.filter_pids = NULL;
	cfg->pid_whitelist.filter_pids_count = 0;
	cfg->pid_whitelist.filter_pids_capacity = 0;
	cfg->pid_whitelist.filter_comms = NULL;
	cfg->pid_whitelist.filter_comms_count = 0;
	cfg->pid_whitelist.filter_comms_capacity = 0;
}

static parse_args_status append_filter_pid(ap_arguments *cfg, int pid)
{
	for (size_t i = 0; i < cfg->pid_whitelist.filter_pids_count; i++)
	{
		if (cfg->pid_whitelist.filter_pids[i] == pid)
		{
			// duplicate PID in filter list, ignore
			return parse_args_ok;
		}
	}

	if (cfg->pid_whitelist.filter_pids_count == cfg->pid_whitelist.filter_pids_capacity)
	{
		size_t new_capacity = (cfg->pid_whitelist.filter_pids_capacity == 0) ?
				AP_INITIAL_FILTER_CAPACITY : (cfg->pid_whitelist.filter_pids_capacity * 2);

		int *new_buffer = realloc(cfg->pid_whitelist.filter_pids, new_capacity * sizeof(int));
		if (new_buffer == NULL)
		{
			fprintf(stderr, "append_filter_pid: failed to allocate memory for filter list\n");
			return parse_args_error;
		}

		cfg->pid_whitelist.filter_pids = new_buffer;
		cfg->pid_whitelist.filter_pids_capacity = new_capacity;
	}

	cfg->pid_whitelist.filter_pids[cfg->pid_whitelist.filter_pids_count++] = pid;
	return parse_args_ok;
}

static parse_args_status append_filter_comm(ap_arguments *cfg, const char *comm_start, size_t comm_len)
{
	for (size_t i = 0; i < cfg->pid_whitelist.filter_comms_count; i++)
	{
		if ((strlen(cfg->pid_whitelist.filter_comms[i]) == comm_len) &&
			(strncmp(cfg->pid_whitelist.filter_comms[i], comm_start, comm_len) == 0))
		{
			// duplicate command name in filter list, ignore
			return parse_args_ok;
		}
	}

	if (cfg->pid_whitelist.filter_comms_count == cfg->pid_whitelist.filter_comms_capacity)
	{
		size_t new_capacity = (cfg->pid_whitelist.filter_comms_capacity == 0) ?
				AP_INITIAL_FILTER_CAPACITY : (cfg->pid_whitelist.filter_comms_capacity * 2);

		char **new_buffer = realloc(cfg->pid_whitelist.filter_comms, new_capacity * sizeof(char*));
		if (new_buffer == NULL)
		{
			fprintf(stderr, "append_filter_comm: failed to allocate memory for filter list\n");
			return parse_args_error;
		}

		cfg->pid_whitelist.filter_comms = new_buffer;
		cfg->pid_whitelist.filter_comms_capacity = new_capacity;
	}

	char *new_comm = malloc(comm_len + 1);
	if (new_comm == NULL)
	{
		fprintf(stderr, "append_filter_comm: failed to allocate memory for process name\n");
		return parse_args_error;
	}

	memcpy(new_comm, comm_start, comm_len);
	new_comm[comm_len] = '\0';
	cfg->pid_whitelist.filter_comms[cfg->pid_whitelist.filter_comms_count++] = new_comm;
	return parse_args_ok;
}


static void dump_whitelist_filters(ap_pid_whitelist* whitelist)
{
#ifdef debug_whitelist
	printf("############ White list filters - begin ###########\n");
	if (whitelist->filter_comms != NULL)
	{
		if(whitelist->filter_comms_count != 0)
		{
			printf("COMM list:\n");
			for (size_t i = 0; i < whitelist->filter_comms_count; i++)
			{
				printf("%s\n",whitelist->filter_comms[i]);
			}
		}
		else
		{
			printf("Empty process name list!\n");
		}

	}
	else
	{
		printf("No process name filter given!\n");
	}

	if (whitelist->filter_pids_count !=0)
	{
		printf("PID list:\n");
		for (size_t i = 0;i<whitelist->filter_pids_count;i++)
		{
			printf("%d\n",whitelist->filter_pids[i]);
		}
	}
	else
	{
		printf("Empty PID list!\n");
	}

	printf("############ White list filters - end ###########\n");
#else
	(void)whitelist;
#endif
}

static parse_args_status parse_pid_token(const char *token_start, size_t token_len, int *out_pid)
{
	unsigned long long value = 0;

	if (token_len == 0 || token_start == NULL || out_pid == NULL)
	{
		return parse_args_error;
	}

	for (size_t i = 0; i < token_len; i++)
	{
		char current_char = token_start[i];
		if (!isdigit((unsigned char)current_char))
		{
			return parse_args_error;
		}

		value = (value * 10) + (unsigned int)(current_char - '0');
		if (value > INT_MAX)
		{
			return parse_args_error;
		}
	}

	if (value == 0)
	{
		return parse_args_error;
	}

	*out_pid = (int)value;
	return parse_args_ok;
}

static parse_args_status parse_filter_pid_list(const char *arg, ap_arguments *cfg, filter_parse_type type)
{
	if (arg == NULL)
	{
		return parse_args_error;
	}

	const char *cursor = arg;

	while (isspace((unsigned char)*cursor))
	{
		cursor++;
	}

	if (*cursor == '\0')
	{
		return parse_args_error;
	}

	while (1)
	{
		const char *token_start = cursor;
		const char *segment_end = cursor;

		while (*segment_end != '\0' && *segment_end != ',')
		{
			segment_end++;
		}

		const char *token_end = segment_end;

		while (token_start < token_end && isspace((unsigned char)*token_start))
		{
			token_start++;
		}

		while (token_end > token_start && isspace((unsigned char)*(token_end - 1)))
		{
			token_end--;
		}

		size_t token_len = (size_t)(token_end - token_start);

		if (token_len == 0)
		{
			return parse_args_error;
		}

		if (type == filter_parse_pid)
		{
			int parsed_pid = 0;
			if (parse_pid_token(token_start, token_len, &parsed_pid) != parse_args_ok)
			{
				return parse_args_error;
			}

			if (append_filter_pid(cfg, parsed_pid) != parse_args_ok)
			{
				return parse_args_error;
			}
		}
		else
		{
			if (append_filter_comm(cfg, token_start, token_len) != parse_args_ok)
			{
				return parse_args_error;
			}
		}

		cursor = segment_end;
		if (*cursor == '\0')
		{
			break;
		}

		if (*cursor != ',')
		{
			return parse_args_error;
		}

		cursor++;
		while (isspace((unsigned char)*cursor))
		{
			cursor++;
		}

		if (*cursor == '\0')
		{
			return parse_args_error;
		}
	}

	return parse_args_ok;
}


parse_args_status ap_parse_args(int argc, char **argv, ap_arguments *cfg)
{
	bool boIntervalProvided = false;
	bool boCountProvided = false;
	ap_free_arguments(cfg);

	static struct option long_opts[] = {
				{ "interval", 			required_argument, 0, 'i' },
				{ "count",    			required_argument, 0, 'n' },
				{ "cpu_usage",      	required_argument, 0, 'c' },
				{ "rss_usage",      	required_argument, 0, 'r' },
				{ "rss_increase",      	required_argument, 0, 's' },
				{ "rss_delta",      	required_argument, 0, 'd' },
				{ "bytes_read",			required_argument, 0, 'e' },
				{ "bytes_write",		required_argument, 0, 'f' },
				{ "read_rate",			required_argument, 0, 'g' },
				{ "write_rate", 		required_argument, 0, 'a' },
				{ "opened_fds", 		required_argument, 0, 'p' },
				{ "fds_increase", 		required_argument, 0, 'm' },
				{ "fds_delta", 			required_argument, 0, 'o' },
				{ "filter_by_pid",		required_argument, 0, 'k' },
				{ "filter_by_name",		required_argument, 0, 'l' },
				{ "delete_old_files",	no_argument, 	   0, 'j' },
				{ "version",			no_argument, 	   0, 'v' },
				{ "help",     			no_argument,       0, 'h' },
				{ 0, 0, 0, 0 }
		};

	int opt;
	while ((opt = getopt_long(argc, argv, "i:n:c:r:s:d:e:f:g:a:m:p:o:k:l:jvh", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'i':
			if (parse_duration_ms(optarg, &cfg->interval_ms) != parse_args_ok)
			{
				fprintf(stderr, "Invalid interval: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				boIntervalProvided = true;
			}
			break;

		case 'n':
			if (parse_number_of_snapshots(optarg, &cfg->count) != parse_args_ok)
			{
				fprintf(stderr, "Invalid count: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				boCountProvided = true;
			}
			break;

		case 'c':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.cpu_average_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid cpu_usage: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.cpu_average_requested = true;
			}
			break;

		case 'r':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.rss_average_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid rss_usage: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.rss_average_requested = true;
			}
			break;

		case 's':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.rss_increase_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid rss_increase: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.rss_increase_requested = true;
			}
			break;

		case 'd':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.rss_delta_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid rss_delta: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.rss_delta_requested = true;
			}
			break;

		case 'e':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.bytes_read_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid bytes_read: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.bytes_read_requested = true;
			}
			break;

		case 'f':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.bytes_write_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid bytes_write: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.bytes_write_requested = true;
			}
			break;

		case 'g':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.read_rate_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid read_rate: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.read_rate_requested = true;
			}
			break;

		case 'a':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.write_rate_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid write_rate: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.write_rate_requested = true;
			}
			break;

		case 'm':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.fds_increase_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid FD increase: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.fds_increase_requested = true;
			}
			break;

		case 'p':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.opened_fds_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid opened FDs: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.opened_fds_requested = true;
			}
			break;

		case 'o':
			if (parse_integer_arg(optarg, &cfg->end_metrics_args.fds_delta_pids_to_display) != parse_args_ok)
			{
				fprintf(stderr, "Invalid delta FDs: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			else
			{
				cfg->end_metrics_args.fds_delta_requested = true;
			}
			break;

		case 'k':
			if (parse_filter_pid_list(optarg, cfg, filter_parse_pid) != parse_args_ok)
			{
				fprintf(stderr, "Invalid filter_by_pid list: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			break;

		case 'l':
			if (parse_filter_pid_list(optarg, cfg, filter_parse_comm) != parse_args_ok)
			{
				fprintf(stderr, "Invalid filter_by_name list: %s\n", optarg);
				ap_free_arguments(cfg);
				return parse_args_error;
			}
			break;

		case 'j':
			cfg->delete_old_files = true;
			break;

		case 'v':
			printf("%s\n", PROCESS_ANALYZER_VERSION);
			ap_free_arguments(cfg);
			return parse_args_error;

		case 'h':
		default:
			print_usage(argv[0]);
			ap_free_arguments(cfg);
			return parse_args_error;
		}
	}

	if((false == boCountProvided) || (false == boIntervalProvided))
	{
		fprintf(stderr, "Missing mandatory parameter. Interval provided %d, count provided %d. Use -i and -n arguments \n", boIntervalProvided, boCountProvided);
		print_usage(argv[0]);
		ap_free_arguments(cfg);
		return parse_args_error;
	}

	if((true == cfg->end_metrics_args.cpu_average_requested) ||
	   (true == cfg->end_metrics_args.rss_average_requested) ||
	   (true == cfg->end_metrics_args.rss_increase_requested)||
	   (true == cfg->end_metrics_args.rss_delta_requested)	 ||
	   (true == cfg->end_metrics_args.bytes_read_requested)  ||
	   (true == cfg->end_metrics_args.bytes_write_requested) ||
	   (true == cfg->end_metrics_args.read_rate_requested)   ||
	   (true == cfg->end_metrics_args.write_rate_requested)  ||
	   (true == cfg->end_metrics_args.fds_increase_requested)||
	   (true == cfg->end_metrics_args.opened_fds_requested)  ||
	   (true == cfg->end_metrics_args.fds_delta_requested))
	{
		// initialize process stats only if at least one metric was requested AND at least one output method was enabled in the configuration

		if(true == config_get_metrics_console_enabled() || (true == config_get_metrics_json_enabled()))
		{
			process_stats_initialize(argv[0]);
		}
	}

	//dump process white list
	dump_whitelist_filters(&cfg->pid_whitelist);

	return parse_args_ok;
}
