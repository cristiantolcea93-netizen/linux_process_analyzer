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
static void print_timestamp();
static int is_numeric(const char *s);
static int read_proc_stat(pid_t pid);
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
	/* Print to stdout */
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	/* Print to file if provided */
	if (file) {
		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
		fflush(file);
	}
}


static void print_timestamp()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // seconds + nanoseconds
    time_t sec = ts.tv_sec;
    struct tm tm_info;
    localtime_r(&sec, &tm_info);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    log_data(PSN_pfOutputFile,"[%s.%03ld] ", buf, ts.tv_nsec / 1000000); // milliseconds
}

static int is_numeric(const char *s)
{
    for (; *s; s++) {
        if (!isdigit(*s))
            return 0;
    }
    return 1;
}

static int read_proc_stat(pid_t pid)
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

    char comm[64] = {0};
    size_t len = rparen - lparen - 1;
    if (len >= sizeof(comm))
        len = sizeof(comm) - 1;
    memcpy(comm, lparen + 1, len);

    char state;
    pid_t ppid;
    unsigned long utime, stime;
    long rss, rssNice;

    /*
     * Continue parsing AFTER ") "
     */
    int ret = sscanf(rparen + 2,
        "%c %d "         /* state, ppid */
        "%*d %*d %*d %*d %*u %*u %*u %*u %*u "
        "%lu %lu "       /* utime, stime */
        "%*d %*d %*d %*d %*d %*d "
        "%ld",           /* rss */
        &state,
        &ppid,
        &utime,
        &stime,
        &rss
    );

    rssNice = (rss * sysconf(_SC_PAGESIZE))/1024;

    if (ret != 5)
        return -1;

    print_timestamp();
    log_data(PSN_pfOutputFile,"PID=%d COMM=%s STATE=%c PPID=%d UTIME=%lu STIME=%lu RSS=%ld RSS(KB)=%ld ",
           pid, comm, state, ppid, utime, stime, rss, rssNice);

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

	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		if (de->d_type != DT_DIR)
			continue;

		if (!is_numeric(de->d_name))
			continue;

		pid_t pid = atoi(de->d_name);

		if (read_proc_stat(pid) == 0) {
			int threads = read_proc_threads(pid);
			if (threads >= 0)
				log_data(PSN_pfOutputFile,"THREADS=%d\n", threads);
		}
	}

	closedir(dir);
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



