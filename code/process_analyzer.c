#include <sys/timerfd.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include "process_snapshot.h"
#include "args_parser.h"
#include "process_stats.h"
#include "config.h"
#include "compression_worker.h"



static int getTimeFd(uint64_t interval_ms);
static void run_snapshot_once(ap_pid_whitelist* whiteList);
static void register_signal_handler();

static ap_arguments gArguments;
static volatile sig_atomic_t g_stop_requested = 0;

static void sigint_handler(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static void register_signal_handler()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}



static int getTimeFd(uint64_t interval_ms)
{
	int tfd;
	struct itimerspec ts;

	tfd = timerfd_create(CLOCK_MONOTONIC, 0);

	if(tfd == -1)
	{
		fprintf(stderr, "getTimeFd: failed to create timer fd!\n");
		return -1;
	}

	//configure timer
	ts.it_value.tv_sec = interval_ms / 1000;
	ts.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;

	//periodic timer
	ts.it_interval = ts.it_value;

	if (timerfd_settime(tfd, 0, &ts, NULL) == -1)
	{
		fprintf(stderr,"getTimeFd: timerfd_settime failed! \n");
		close(tfd);
		return -1;
	}
	return tfd;
}


static void run_snapshot_once(ap_pid_whitelist* whiteList)
{
	collect_snapshot(whiteList);
}


int main(int argc, char **argv)
{
	config_status l_cfg_status = config_init();

	if(config_success == l_cfg_status)
	{
		parse_args_status l_parse_status  = ap_parse_args(argc, argv, &gArguments);

		if(parse_args_ok == l_parse_status)
		{
			// print the configuration banner
			config_print_banner();
			if(true == gArguments.delete_old_files)
			{
				/*delete the old files if the argument was given*/
				if(process_snapshot_success != process_snapshot_delete_old_files())
				{
					/*failed to delete old files, do not start the analyzer*/
					ap_free_arguments(&gArguments);
					return -1;
				}
			}

			if(process_snapshot_success != process_snapshot_initialize())
			{
				//failed to initialize process snapshot
				ap_free_arguments(&gArguments);
				return -1;
			}

			compression_worker_init();

			int tfd = getTimeFd(gArguments.interval_ms);
			if(tfd != -1)
			{
				register_signal_handler();
				int noOfIterations = 0;
				while(noOfIterations < gArguments.count)
				{
					uint64_t expirations;

					if(g_stop_requested)
					{
						//program interrupted
						printf("\nProgram interrupted by user (CTRL+C) or terminated\n");
						break;
					}

					if (read(tfd, &expirations, sizeof(expirations)) != sizeof(expirations))
					{
						if (errno == EINTR && g_stop_requested)
						{
							//program interrupted
							printf("\nProgram interrupted by user (CTRL+C) or terminated\n");
							break;
						}

						fprintf(stderr, "failed to read expirations!");
						break;
					}

					while(expirations-- && noOfIterations < gArguments.count)
					{
						//recover number of iterations in case of expirations caused by high CPU load
						run_snapshot_once(&gArguments.pid_whitelist);
					}
					noOfIterations++;
				}
				close(tfd);
				//deinit snapshot
				process_snapshot_deinit();
				//stop compression worker
				compression_worker_shutdown();
				//print end of execution metrics
				process_stats_print_metrics(&(gArguments.end_metrics_args),gArguments.interval_ms);
				ap_free_arguments(&gArguments);
			}
			else
			{
				//failed to start the timer
				process_snapshot_deinit();
				compression_worker_shutdown();
				ap_free_arguments(&gArguments);
				return -1;
			}

		}
		else
		{
			//failed to parse arguments
			ap_free_arguments(&gArguments);
			return -1;
		}
	}
	else
	{
		//failed to parse the configuration
		ap_free_arguments(&gArguments);
		return -1;
	}

	return 0;
}
