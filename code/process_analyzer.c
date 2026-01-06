#include <sys/timerfd.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include "process_snapshot.h"
#include "args_parser.h"


static int getTimeFd(uint64_t interval_ms);
static void run_snapshot_once();

ap_arguments gArguments;

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


static void run_snapshot_once()
{
	process_snapshot_status l_process_status = process_snapshot_error;
	l_process_status = process_snapshot_initialize();
	if(process_snapshot_success == l_process_status)
	{
		collect_snapshot();
		process_snapshot_deinit();
	}
}


int main(int argc, char **argv)
{
	parse_args_status l_parse_status  = ap_parse_args(argc, argv, &gArguments);

	if(parse_args_ok == l_parse_status)
	{
		int tfd = getTimeFd(gArguments.interval_ms);
		if(tfd != -1)
		{
			int noOfIterations = 0;
			while(noOfIterations < gArguments.count)
			{
				uint64_t expirations;
				if (read(tfd, &expirations, sizeof(expirations)) != sizeof(expirations))
				{
					fprintf(stderr, "failed to read expirations!");
					break;
				}

				while(expirations-- && noOfIterations < gArguments.count)
				{
					//recover number of iterations in case of expirations caused by high CPU load
					run_snapshot_once();
				}
				noOfIterations++;
			}



		}

	}
	return 0;
}
