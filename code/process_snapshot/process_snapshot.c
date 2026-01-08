#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/file.h>
#include <fcntl.h>

#include "process_snapshot.h"
#include "process_stats.h"

#define PROC_PATH "/proc"


#define PSN_MAX_LOG_SIZE   (5 * 1024 * 1024)  // 5 MB
#define PSN_MAX_ROTATIONS  3

/*todo: replace this with configuration option*/
#define PSN_LOG_DIR   "/tmp/ptime"
#define PSN_LOG_FILE  "ptime.log"

#define PSN_FULL_FILE_PATH "/tmp/ptime/ptime.log"

#define PSN_LOCK_FILE "/tmp/ptime/ptime.lock"

static FILE* PSN_pfOutputFile = NULL;
static int g_lock_fd = -1;

static void log_data(FILE* file, const char* fmt, ...);
static void print_timestamp(double *timestampInSeconds);
static int is_numeric(const char *s);
static int read_proc_stat(pid_t pid, process_state_input_t *proc_data);
static int read_proc_threads(pid_t pid);
static process_snapshot_status make_log_dir(void);
static off_t get_file_size(const char *path);
static void rotate_logs(void);


static process_snapshot_status acquire_lock(void)
{
	g_lock_fd = open(PSN_LOCK_FILE, O_CREAT | O_RDWR, 0644);
	if (g_lock_fd < 0)
	{
		printf("acquire_lock: failed to open lock file\n");
		return process_snapshot_error;
	}

	// exclusive and non blocking lock
	if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0)
	{
		if (errno == EWOULDBLOCK)
		{
			printf("acquire_lock: process_analyzer is already running\n");
		}
		else
		{
			perror("flock");
		}
		close(g_lock_fd);
		g_lock_fd = -1;
		return process_snapshot_error;
	}
	return process_snapshot_success;
}

static off_t get_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return -1;
    return st.st_size;
}

static void rotate_logs(void)
{
    char old_path[256], new_path[256];

    // remove the oldest log entry
    snprintf(old_path, sizeof(old_path),
             "%s/%s.%d", PSN_LOG_DIR, PSN_LOG_FILE, PSN_MAX_ROTATIONS);
    unlink(old_path);

    // rename .N-1 -> .N
    for (int i = PSN_MAX_ROTATIONS - 1; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path),
                 "%s/%s.%d", PSN_LOG_DIR, PSN_LOG_FILE, i);
        snprintf(new_path, sizeof(new_path),
                 "%s/%s.%d", PSN_LOG_DIR, PSN_LOG_FILE, i + 1);
        rename(old_path, new_path);
    }

    // current log -> .1
    snprintf(old_path, sizeof(old_path),
             "%s/%s", PSN_LOG_DIR, PSN_LOG_FILE);
    snprintf(new_path, sizeof(new_path),
             "%s/%s.1", PSN_LOG_DIR, PSN_LOG_FILE);
    rename(old_path, new_path);
}


static void log_data(FILE* file, const char* fmt, ...)
{
	va_list args;
	// todo add config file to print to stdout
#if 0
	/* Print to stdout */
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
#endif
	/* Print to file if provided */
	if (file) {
		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
		fflush(file);
	}
}

static void print_timestamp(double *timestampInSeconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // seconds + nanoseconds
    time_t sec = ts.tv_sec;
    struct tm tm_info;
    localtime_r(&sec, &tm_info);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    log_data(PSN_pfOutputFile,"[%s.%03ld] ", buf, ts.tv_nsec / 1000000); // milliseconds

    *timestampInSeconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int is_numeric(const char *s)
{
    for (; *s; s++) {
        if (!isdigit(*s))
            return 0;
    }
    return 1;
}

static int read_rss_status(pid_t pid, long *rss_kb)
{
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "VmRSS:", 6) == 0)
        {
            sscanf(line + 6, "%ld", rss_kb);
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return -1;
}

static int read_proc_stat(pid_t pid, process_state_input_t *proc_data)
{
	char path[64];
	char buf[1024];

	snprintf(path, sizeof(path), PROC_PATH "/%d/stat", pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);

	/*
	 * Format:
	 * pid (comm) state ppid ... utime stime ... rss
	 */

	char *lparen = strchr(buf, '(');
	char *rparen = strrchr(buf, ')');
	if (!lparen || !rparen)
		return -1;

	memset(&proc_data->comm, 0x00, sizeof(proc_data->comm));
	size_t len = rparen - lparen - 1;
	if (len >= sizeof(proc_data->comm))
		len = sizeof(proc_data->comm) - 1;
	memcpy(proc_data->comm, lparen + 1, len);

	pid_t ppid;
	/*
	 * Continue parsing AFTER ") "
	 */
	int ret = sscanf(rparen + 2,
			"%c %d "         /* state, ppid */
			"%*d %*d %*d %*d %*u %*u %*u %*u %*u "
			"%lu %lu ",       /* utime, stime */
			&proc_data->state,//&state,
			&ppid,
			&proc_data->utime,//&utime,
			&proc_data->stime//&stime,
	);
	if (ret != 4)
		return -1;


	ret = read_rss_status(pid, &proc_data->rssKb);

	if(ret != 0)
		return -1;

	log_data(PSN_pfOutputFile,"PID=%d COMM=%s STATE=%c PPID=%d UTIME=%lu STIME=%lu RSS(KB)=%ld ",
			pid, proc_data->comm, proc_data->state, ppid, proc_data->utime, proc_data->stime, proc_data->rssKb);

	return 0;
}

static int read_proc_threads(pid_t pid)
{
    char path[64];
    char line[256];
    int threads = -1;

    snprintf(path, sizeof(path), PROC_PATH "/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Threads:", 8) == 0) {
            sscanf(line + 8, "%d", &threads);
            break;
        }
    }
    fclose(f);

    return threads;
}

static process_snapshot_status make_log_dir(void)
{
	process_snapshot_status l_retVal;

	if (mkdir(PSN_LOG_DIR, 0755) < 0)
	{
		if (errno == EEXIST)
		{
			l_retVal = process_snapshot_success;
		}
		else
		{
			printf("make_log_dir: failed to create directory for the output files\n");
			l_retVal = process_snapshot_error;
		}
	}
	else
	{
		l_retVal = process_snapshot_success;
	}

	return l_retVal;

}

process_snapshot_status collect_snapshot(void)
{
	DIR *dir = opendir(PROC_PATH);
	if (!dir) {
		perror("opendir /proc");
		return process_snapshot_error;
	}
	process_state_input_t process_data;

	print_timestamp(&process_data.timestamp_sec);
	log_data(PSN_pfOutputFile," SNAPSHOT START ################# \n");

	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {

		if (de->d_type != DT_DIR)
			continue;

		if (!is_numeric(de->d_name))
			continue;

		process_data.pid = atoi(de->d_name);

		if(process_data.pid == getpid())
			continue;

		if (read_proc_stat(process_data.pid,&process_data) == 0) {
			process_data.threads = read_proc_threads(process_data.pid);
			if (process_data.threads >= 0)
				log_data(PSN_pfOutputFile,"THREADS=%d\n", process_data.threads);
		}
		//feed the data to process_stat
		process_stats_update(&process_data);
	}

	closedir(dir);
	log_data(PSN_pfOutputFile,"SNAPSHOT END ################# \n");
	process_stats_snapshot_end();
	return process_snapshot_success;
}

process_snapshot_status process_snapshot_initialize(void)
{
	process_snapshot_status retVal;
	char openfilePath[256];
	snprintf(openfilePath, sizeof(openfilePath), "%s/%s", PSN_LOG_DIR, PSN_LOG_FILE);

	retVal = make_log_dir();

	if(process_snapshot_success == retVal)
	{
		retVal = acquire_lock();
		if(process_snapshot_success == retVal)
		{
			off_t size = get_file_size(openfilePath);
			if (size >= PSN_MAX_LOG_SIZE)
			{
				rotate_logs();
			}

			PSN_pfOutputFile = fopen(openfilePath, "a");

			if(!PSN_pfOutputFile)
			{
				retVal = process_snapshot_error;
				printf("process_snapshot_initialize: Failed to open output file!\n");
			}
			else
			{
				retVal = process_snapshot_success;
			}
		}
		else
		{
			//error logged by acquire_lock
		}
	}
	else
	{
		//error logged in make_log_dir
	}

	return retVal;
}

void process_snapshot_deinit(void)
{
	if (g_lock_fd >= 0)
	{
		flock(g_lock_fd, LOCK_UN);
		close(g_lock_fd);
		g_lock_fd = -1;
	}
}



