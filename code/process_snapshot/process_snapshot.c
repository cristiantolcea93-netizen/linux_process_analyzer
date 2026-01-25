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
#include <limits.h>

#include "process_snapshot.h"
#include "process_stats.h"
#include "config.h"

#define PROC_PATH "/proc"




static FILE* PSN_pfOutputFile = NULL;
static FILE* PSN_pfOutputJsonlFile = NULL;
static int g_lock_fd = -1;

static void log_data(FILE* file, const char* fmt, ...);
static void print_timestamp(double *timestamp, char* hr_timestamp);
static int is_numeric(const char *s);
static int read_proc_stat(pid_t pid, process_state_input_t *proc_data);
static int read_proc_threads(pid_t pid);
static off_t get_file_size(const char *path);
static void rotate_logs(char* filePath);
static int read_proc_io(pid_t pid, process_state_input_t *p);
static int read_rss_status(pid_t pid, long *rss_kb);
static void write_output_to_json(process_state_input_t* input);


static process_snapshot_status acquire_lock(void)
{
	g_lock_fd = open(CONFIG_LOCK_FILE, O_CREAT | O_RDWR, 0644);
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

static void rotate_logs(char* filePath)
{
    char old_path[256], new_path[256];

    int max_rotations = config_get_max_number_of_files();
    // remove the oldest log entry
    snprintf(old_path, sizeof(old_path),
             "%s.%d", filePath, max_rotations);
    unlink(old_path);

    // rename .N-1 -> .N
    for (int i = max_rotations - 1; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path),
                 "%s.%d", filePath, i);
        snprintf(new_path, sizeof(new_path),
                 "%s.%d", filePath, i + 1);
        rename(old_path, new_path);
    }

    // current log -> .1
    snprintf(old_path, sizeof(old_path),
             "%s", filePath);
    snprintf(new_path, sizeof(new_path),
             "%s.1", filePath);
    rename(old_path, new_path);
}


static void log_data(FILE* file, const char* fmt, ...)
{
	va_list args;

	bool is_console_snapshot_enabled = config_get_raw_console_enabled();

	if(true == is_console_snapshot_enabled)
	{
		/* Print to stdout */
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
	}

	/* Print to file if provided */
	if (file) {
		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
		fflush(file);
	}
}

static void print_timestamp(double *timestamp, char* hr_timestamp)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // seconds + nanoseconds
    time_t sec = ts.tv_sec;
    struct tm tm_info;
    localtime_r(&sec, &tm_info);

  //  char buf[64];
  //  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
  //  log_data(PSN_pfOutputFile,"[%s.%03ld] ", buf, ts.tv_nsec / 1000000); // milliseconds


    snprintf(
            hr_timestamp,
            64,
            "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
            tm_info.tm_year + 1900,
            tm_info.tm_mon + 1,
            tm_info.tm_mday,
            tm_info.tm_hour,
            tm_info.tm_min,
            tm_info.tm_sec,
            ts.tv_nsec / 1000000
        );

    log_data(PSN_pfOutputFile, "[%s] ", hr_timestamp);


    clock_gettime(CLOCK_MONOTONIC, &ts);
    //monotonic fractional timestamp
    *timestamp = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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
            if(sscanf(line + 6, "%ld", rss_kb)==1)
            {
            	fclose(f);
            	return 0;
            }
        }
    }

    fclose(f);
    return -1;
}

static int read_proc_io(pid_t pid, process_state_input_t *p)
{
    char path[64];
    char line[256];
    FILE *f;

    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    f = fopen(path, "r");
    if (!f)
        return -1;

    p->read_kbytes  = 0;
    p->write_kbytes = 0;

    while (fgets(line, sizeof(line), f)) {

        unsigned long long v;

        if (sscanf(line, "read_bytes: %llu", &v) == 1) {
            p->read_kbytes = v / 1024;
            continue;
        }

        if (sscanf(line, "write_bytes: %llu", &v) == 1) {
            p->write_kbytes = v / 1024;
            continue;
        }
    }

    fclose(f);
    return 0;
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

	/*
	 * Continue parsing AFTER ") "
	 */
	int ret = sscanf(rparen + 2,
			"%c %d "         /* state, ppid */
			"%*d %*d %*d %*d %*u %*u %*u %*u %*u "
			"%lu %lu ",       /* utime, stime */
			&proc_data->state,//&state,
			&proc_data->ppid,
			&proc_data->utime,//&utime,
			&proc_data->stime//&stime,
	);
	if (ret != 4)
		return -1;


	ret = read_rss_status(pid, &proc_data->rssKb);

	if(ret != 0)
	{
		//failed to get rss
		proc_data->rssKb = -1;
		proc_data->bo_is_rss_valid = false;
	}
	else
	{
		//rss available
		proc_data->bo_is_rss_valid = true;
	}



	ret = read_proc_io(pid, proc_data);


	if(ret != 0)
	{
		//failed to get io data
		proc_data->read_kbytes  = -1;
		proc_data->write_kbytes = -1;
		proc_data->bo_is_io_valid = false;
	}
	else
	{
		//io data available
		proc_data->bo_is_io_valid = true;
	}


	log_data(PSN_pfOutputFile,"PID=%d COMM=%s STATE=%c PPID=%d UTIME=%lu STIME=%lu RSS(KB)=%ld IOR(KB)=%lld IOW(KB)=%lld ",
			pid, proc_data->comm, proc_data->state, proc_data->ppid, proc_data->utime, proc_data->stime, proc_data->rssKb, proc_data->read_kbytes, proc_data->write_kbytes);

	return 0;
}

static void write_output_to_json(process_state_input_t* input)
{
	if(PSN_pfOutputJsonlFile)
	{
		fprintf(PSN_pfOutputJsonlFile,
		        "{"
		        "\"timestamp\":\"%s\","
		        "\"pid\":%d,"
		        "\"comm\":\"%s\","
		        "\"state\":\"%c\","
		        "\"ppid\":%d,"
		        "\"utime\":%lu,"
		        "\"stime\":%lu,"
		        "\"rss_kb\":%ld,"
		        "\"io_read_kb\":%lld,"
		        "\"io_write_kb\":%lld,"
		        "\"threads\":%d"
		        "}\n",
				input->h_r_timestamp,
		        input->pid,
		        input->comm,
		        input->state,
		        input->ppid,
				input->utime,
				input->stime,
				input->rssKb,
		        input->read_kbytes,
		        input->write_kbytes,
				input->threads
		    );
	}
	else
	{
		//jsonl file not required by configuration
	}
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

process_snapshot_status process_snapshot_delete_old_files(void)
{
	struct dirent *ent;

	DIR *dir = opendir(config_get_output_dir());
	if (dir != NULL)
	{
		/* scan the directory */
		while ((ent = readdir(dir)) != NULL)
		{
			if ((strncmp(ent->d_name, CONFIG_LOG_FILE,strlen(CONFIG_LOG_FILE)) == 0) ||
					(strncmp(ent->d_name, CONFIG_JSON_FILE,strlen(CONFIG_JSON_FILE)) == 0))
			{
				/*remove only the files created by the tool*/
				char deletefilePath[512];
				snprintf(deletefilePath, sizeof(deletefilePath), "%s/%s", config_get_output_dir(), ent->d_name);

				if(0 == remove(deletefilePath))
				{
					printf("Removed old file %s\n",ent->d_name);
				}
				else
				{
					closedir (dir);
					fprintf(stderr, "process_snapshot_delete_old_files: Failed to remove old file %s\n", ent->d_name);
					return  process_snapshot_error;
				}
			}
		}
		closedir (dir);
	}
	else
	{
		if(errno ==  ENOENT)
		{
			/*directory doesn't exist - this is not an error*/
			return process_snapshot_success;
		}
		else
		{
			/* could not open directory */
			fprintf(stderr, "process_snapshot_delete_old_files: Failed to open dir %s\n", config_get_output_dir());
			return process_snapshot_error;
		}

	}
	return process_snapshot_success;
}

process_snapshot_status collect_snapshot(void)
{
	DIR *dir = opendir(PROC_PATH);
	if (!dir) {
		fprintf(stderr,"failed to open /proc");
		return process_snapshot_error;
	}
	process_state_input_t process_data;

	print_timestamp(&process_data.timestamp, process_data.h_r_timestamp);
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
			if (process_data.threads >= 0){
				log_data(PSN_pfOutputFile,"THREADS=%d\n", process_data.threads);
			}

			//feed the data to process_stat
			process_stats_update(&process_data);

			//generate json file
			write_output_to_json(&process_data);
		}
	}

	closedir(dir);
	log_data(PSN_pfOutputFile,"SNAPSHOT END ################# \n");
	process_stats_snapshot_end();
	return process_snapshot_success;
}

process_snapshot_status process_snapshot_initialize(void)
{
	process_snapshot_status retVal;
	char openfilePath[PATH_MAX];

	bool isRawLogEnabled = config_get_raw_log_enabled();
	bool isJsonlEnabled = config_get_raw_jsonl_enabled();

	if((false == isRawLogEnabled) && (false == isJsonlEnabled))
	{
		//no files required by configuration
		return process_snapshot_success;
	}

	snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_LOG_FILE);



	retVal = acquire_lock();
	if(process_snapshot_success == retVal)
	{
		if(true == isRawLogEnabled)
		{
			off_t size = get_file_size(openfilePath);
			if (size >= config_get_max_file_size_bytes())
			{
				rotate_logs(openfilePath);
			}
			PSN_pfOutputFile = fopen(openfilePath, "a");

			if(!PSN_pfOutputFile)
			{
				retVal = process_snapshot_error;
				fprintf(stderr, "process_snapshot_initialize: Failed to open output file!\n");
			}
			else
			{
				retVal = process_snapshot_success;
			}
		}
		else
		{
			//.log file disabled from the configuration
		}

		if(true == isJsonlEnabled)
		{
			//jsonl file handling
			memset(openfilePath, 0x00, sizeof(openfilePath));
			snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_JSON_FILE);
			off_t size = get_file_size(openfilePath);

			if (size >= config_get_max_file_size_bytes())
			{
				rotate_logs(openfilePath);
			}

			PSN_pfOutputJsonlFile = fopen(openfilePath, "a");
			if(!PSN_pfOutputJsonlFile)
			{
				retVal = process_snapshot_error;
				fprintf(stderr, "process_snapshot_initialize: Failed to open jsonl output file!\n");
			}
			else
			{
				retVal = process_snapshot_success;
			}
		}
		else
		{
			//.jsonl raw data not required by configuration
		}
	}
	else
	{
		//error logged by acquire_lock
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

	if(PSN_pfOutputFile)
	{
		if(fclose(PSN_pfOutputFile)!=0)
		{
			fprintf(stderr, "process_snapshot_deinit: Failed to close PSN_pfOutputFile!\n");
		}
	}

	if(PSN_pfOutputJsonlFile)
	{
		if(fclose(PSN_pfOutputJsonlFile)!=0)
		{
			fprintf(stderr, "process_snapshot_deinit: Failed to close PSN_pfOutputJsonlFile!\n");
		}
	}
}



