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
#include <zlib.h>


#include "process_snapshot.h"
#include "process_stats.h"
#include "config.h"
#include "compression_worker.h"

#ifndef PROC_PATH
#define PROC_PATH "/proc"
#endif

#define COMPRESS_BUFFER_SIZE 65536   /* 64 KB */
#define OUTPUT_STREAM_BUFFER_SIZE (64 * 1024)



static FILE* PSN_pfOutputFile = NULL;
static FILE* PSN_pfOutputJsonlFile = NULL;
static int g_lock_fd = -1;
static DIR *dir = NULL;
static long g_page_size_kb = 0;

static void log_data(FILE* file, const char* fmt, ...);
static void flush_output_files(void);
static void configure_output_buffer(FILE *file);
static void initialize_page_size_kb(void);
static void print_timestamp(double *timestamp, char* hr_timestamp);
static int is_numeric(const char *s);
static int read_proc_stat(pid_t pid, process_state_input_t *proc_data);
static off_t get_file_size(const char *path);
static void rotate_logs(const char* filePath);
static void rotate_and_reopen(FILE **pf, const char *path);
static int read_proc_io(pid_t pid, process_state_input_t *p);
static long count_open_fds_for_pid(pid_t pid);
static void read_fd(process_state_input_t* process_data);
static void write_output_to_json(process_state_input_t* input);
static process_snapshot_status acquire_lock(const char* lock_file_path);
static bool is_pid_in_filter(pid_t pid, const ap_pid_whitelist* whiteList);
static bool is_comm_in_filter(const char *comm, const ap_pid_whitelist* whiteList);
static bool is_filtering_enabled(const ap_pid_whitelist* whiteList);


static process_snapshot_status acquire_lock(const char* lock_file_path)
{

	g_lock_fd = open(lock_file_path, O_CREAT | O_RDWR, 0644);
	if (g_lock_fd < 0)
	{
		printf("acquire_lock: failed to open lock file\n");
		return process_snapshot_aquire_lock_failed;
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
		return process_snapshot_aquire_lock_failed;
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

static void rotate_logs(const char* filePath)
{
    char old_path[PATH_MAX], new_path[PATH_MAX];
    bool compression_enabled = config_get_compression_enabled();
    int max_rotations = config_get_max_number_of_files();

    // remove oldest (try both)
    snprintf(old_path, sizeof(old_path), "%s.%d.gz", filePath, max_rotations);
    unlink(old_path);

    snprintf(old_path, sizeof(old_path), "%s.%d", filePath, max_rotations);
    unlink(old_path);

    for (int i = max_rotations - 1; i >= 1; i--)
    {
        if (compression_enabled)
        {
            int ret;

            snprintf(old_path, sizeof(old_path), "%s.%d.gz", filePath, i);
            snprintf(new_path, sizeof(new_path), "%s.%d.gz", filePath, i + 1);

            ret = rename(old_path, new_path);

            if (ret != 0 && errno != ENOENT)
            {
                fprintf(stderr,
                        "rotate rename failed (gz), trying fallback %s -> %s\n",
                        old_path, new_path);
            }

            if (ret == 0)
                continue;
        }

        // fallback or non-compression path
        snprintf(old_path, sizeof(old_path), "%s.%d", filePath, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", filePath, i + 1);

        if (rename(old_path, new_path) != 0 && errno != ENOENT)
        {
            fprintf(stderr,"rotate rename failed %s -> %s\n",old_path, new_path);
        }
    }

    // current -> .1
    snprintf(old_path, sizeof(old_path), "%s", filePath);

    if (compression_enabled)
    {
    	static uint32_t rotation_counter = 0;

    	/* filename.1.time.counter => unique name required if compression is enabled to cover the
    	 * corner case when compression thread is slower than the second rotation and it tries to
    	 * compress a file which was already overwritten by the thread doing the rotation
    	 */
        snprintf(new_path, sizeof(new_path),"%s.1.%ld.%u",filePath,time(NULL),rotation_counter);
        rotation_counter++;
    }
    else
    {
        snprintf(new_path, sizeof(new_path),"%s.1",filePath);
    }

    if (rename(old_path, new_path) != 0 && errno != ENOENT)
    {
        fprintf(stderr,"rotate rename failed %s -> %s\n",old_path, new_path);
    }

    if (compression_enabled)
    {
        compression_enqueue_file(new_path);
    }
}

static void rotate_and_reopen(FILE **pf, const char *path)
{
    if (*pf)
    {
        fflush(*pf);
        fclose(*pf);
        *pf = NULL;
    }

    rotate_logs(path);

    *pf = fopen(path, "a");
    if (!*pf)
    {
       fprintf(stderr,"fopen failed after rotate on %s", path);
       return;
    }

    configure_output_buffer(*pf);
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
	}
}

static void flush_output_files(void)
{
	if (PSN_pfOutputFile)
	{
		fflush(PSN_pfOutputFile);
	}

	if (PSN_pfOutputJsonlFile)
	{
		fflush(PSN_pfOutputJsonlFile);
	}
}

static void configure_output_buffer(FILE *file)
{
	if (file)
	{
		setvbuf(file, NULL, _IOFBF, OUTPUT_STREAM_BUFFER_SIZE);
	}
}

static void initialize_page_size_kb(void)
{
	long page_size;

	if (g_page_size_kb > 0)
	{
		return;
	}

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size > 0)
	{
		g_page_size_kb = page_size / 1024;
	}

	if (g_page_size_kb <= 0)
	{
		g_page_size_kb = 4;
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
    if (!s || *s == '\0')
        return 0;

    for (; *s; s++) {
        if (!isdigit((unsigned char)*s))
            return 0;
    }

	return 1;
}

static bool is_pid_in_filter(pid_t pid, const ap_pid_whitelist* whiteList)
{

	for (size_t i = 0; i < whiteList->filter_pids_count; i++)
	{
		if (whiteList->filter_pids[i] == pid)
		{
			return true;
		}
	}

	return false;
}

static bool is_comm_in_filter(const char *comm, const ap_pid_whitelist* whiteList)
{

	if (comm == NULL || *comm == '\0')
	{
		return false;
	}

	for (size_t i = 0; i < whiteList->filter_comms_count; i++)
	{
		if (strcmp(whiteList->filter_comms[i], comm) == 0)
		{
			return true;
		}
	}

	return false;
}

static bool is_filtering_enabled(const ap_pid_whitelist* whiteList)
{
	if((whiteList->filter_comms_count == 0)&&(whiteList->filter_pids_count == 0))
	{
		return false;
	}
	else
	{
		return true;
	}
}


static long count_open_fds_for_pid(pid_t pid)
{
	char path[64];
	const struct dirent *entry;
	DIR *fd_dir;
	unsigned long count = 0;

	snprintf(path, sizeof(path), PROC_PATH "/%d/fd", pid);
	fd_dir = opendir(path);
	if (fd_dir == NULL)
		return -1;

	while ((entry = readdir(fd_dir)) != NULL)
	{
		if (is_numeric(entry->d_name))
		{
			count++;
		}
	}

	closedir(fd_dir);
	return (long)count;
}

static void read_fd(process_state_input_t* process_data)
{
	long count;

	if (process_data == NULL)
		return;

	process_data->bo_is_fd_valid = false;
	process_data->number_of_fds = 0;

	count = count_open_fds_for_pid(process_data->pid);

	if (count < 0)
		return;

	process_data->number_of_fds = (unsigned long)count;
	process_data->bo_is_fd_valid = true;
}

static int read_proc_io(pid_t pid, process_state_input_t *p)
{
    char path[64];
    char line[256];
    FILE *f;

    snprintf(path, sizeof(path), PROC_PATH "/%d/io", pid);
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
	long rss_pages = -1;
	int discard_int;
	unsigned int discard_uint;
	unsigned long discard_ulong;
	unsigned long long discard_ullong;
	long discard_long;

	snprintf(path, sizeof(path), PROC_PATH "/%d/stat", pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	if (!fgets(buf, sizeof(buf), f))
	{
		fclose(f);
		return -1;
	}
	fclose(f);

	/*
	 * Format:
	 * pid (comm) state ppid ... utime stime ... rss
	 */

	const char *lparen = strchr(buf, '(');
	const char *rparen = strrchr(buf, ')');
	if (!lparen || !rparen)
		return -1;

	initialize_page_size_kb();

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
			"%d %d %d %d %u %u %u %u %u "
			"%lu %lu "        /* utime, stime */
			"%ld %ld %ld %ld "
			"%d "             /* num_threads */
			"%ld %llu %lu "
			"%ld",            /* rss in pages */
			&proc_data->state,//&state,
			&proc_data->ppid,
			&discard_int,
			&discard_int,
			&discard_int,
			&discard_int,
			&discard_uint,
			&discard_uint,
			&discard_uint,
			&discard_uint,
			&discard_uint,
			&proc_data->utime,//&utime,
			&proc_data->stime,//&stime,
			&discard_long,
			&discard_long,
			&discard_long,
			&discard_long,
			&proc_data->threads,
			&discard_long,
			&discard_ullong,
			&discard_ulong,
			&rss_pages
	);
	if (ret != 22)
		return -1;

	proc_data->rssKb = -1;
	proc_data->bo_is_rss_valid = false;
	if (rss_pages >= 0)
	{
		proc_data->rssKb = rss_pages * g_page_size_kb;
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
		        "\"threads\":%d,"
				"\"fds\":%ld"
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
				input->threads,
				input->bo_is_fd_valid ? (long)input->number_of_fds : -1L
		    );
	}
	else
	{
		//jsonl file not required by configuration
	}
}

static void handle_rotations(void)
{
	bool isRawLogEnabled = config_get_raw_log_enabled();
	bool isJsonlEnabled = config_get_raw_jsonl_enabled();
	char openfilePath[PATH_MAX];

	if((false == isRawLogEnabled) && (false == isJsonlEnabled))
	{
		//no files required by configuration
		return ;
	}
	if(true == isRawLogEnabled)
	{
		memset(openfilePath, 0x00, sizeof(openfilePath));
		snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_LOG_FILE);
		off_t size = get_file_size(openfilePath);

		if (size >= config_get_max_file_size_bytes())
		{
			rotate_and_reopen(&PSN_pfOutputFile, openfilePath);
		}
	}

	if(true == isJsonlEnabled)
	{
		memset(openfilePath, 0x00, sizeof(openfilePath));
		snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_JSON_FILE);
		off_t size = get_file_size(openfilePath);

		if (size >= config_get_max_file_size_bytes())
		{
			rotate_and_reopen(&PSN_pfOutputJsonlFile, openfilePath);
		}

	}

}

process_snapshot_status process_snapshot_delete_old_files(void)
{
	DIR *output_dir = opendir(config_get_output_dir());
	if (output_dir != NULL)
	{
		const struct dirent *ent;

		/* scan the directory */
		while ((ent = readdir(output_dir)) != NULL)
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
					closedir(output_dir);
					fprintf(stderr, "process_snapshot_delete_old_files: Failed to remove old file %s\n", ent->d_name);
					return  process_snapshot_error;
				}
			}
		}
		closedir(output_dir);
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

process_snapshot_status collect_snapshot(ap_pid_whitelist* whiteList)
{
	//handle rotation
	handle_rotations();

	process_state_input_t process_data;

	print_timestamp(&process_data.timestamp, process_data.h_r_timestamp);
	log_data(PSN_pfOutputFile," SNAPSHOT START ################# \n");

	struct dirent *de;
	rewinddir(dir);
	while ((de = readdir(dir)) != NULL) {

		if (de->d_type != DT_DIR)
			continue;

		if (!is_numeric(de->d_name))
			continue;

		process_data.pid = atoi(de->d_name);

		if(false == config_get_include_self())
		{
			//PID of the test program is included only if specified in the configuration
			if(process_data.pid == getpid())
				continue;
		}

		if (read_proc_stat(process_data.pid,&process_data) == 0)
		{
			if(true == is_filtering_enabled(whiteList))
			{
				//only check the filters if at least one filter is enabled
				if((false == is_comm_in_filter(process_data.comm, whiteList)) &&
						(false == is_pid_in_filter(process_data.pid, whiteList)))
				{
					//neither process name (--filter_by_name) nor its PID (--filter_by_name) was requested
					continue;
				}
			}

			read_fd(&process_data);

			log_data(PSN_pfOutputFile,"PID=%d COMM=%s STATE=%c PPID=%d UTIME=%lu STIME=%lu RSS(KB)=%ld IOR(KB)=%lld IOW(KB)=%lld THREADS=%d FD=%ld\n",
					process_data.pid, process_data.comm, process_data.state, process_data.ppid, process_data.utime, process_data.stime, process_data.rssKb, process_data.read_kbytes, process_data.write_kbytes, process_data.threads, process_data.bo_is_fd_valid ? (long)process_data.number_of_fds : -1L);

			//feed the data to process_stat
			process_stats_update(&process_data);

			//generate json file
			write_output_to_json(&process_data);
		}
	}

	log_data(PSN_pfOutputFile,"SNAPSHOT END ################# \n");
	flush_output_files();
	process_stats_snapshot_end();
	return process_snapshot_success;
}

process_snapshot_status process_snapshot_initialize(void)
{
	process_snapshot_status retVal;
	char openfilePath[PATH_MAX];

	bool isRawLogEnabled = config_get_raw_log_enabled();
	bool isJsonlEnabled = config_get_raw_jsonl_enabled();

	dir = opendir(PROC_PATH);
	if (!dir)
	{
		fprintf(stderr,"failed to open /proc");
		return process_snapshot_error;
	}

	initialize_page_size_kb();

	if((false == isRawLogEnabled) && (false == isJsonlEnabled))
	{
		//no files required by configuration
		return process_snapshot_success;
	}

	snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_LOCK_FILE);
	retVal = acquire_lock((const char*)openfilePath);

	if(process_snapshot_success == retVal)
	{
		if(true == isRawLogEnabled)
		{
			memset(openfilePath, 0x00, sizeof(openfilePath));
			snprintf(openfilePath, sizeof(openfilePath), "%s/%s", config_get_output_dir(), CONFIG_LOG_FILE);
			PSN_pfOutputFile = fopen(openfilePath, "a");

			if(!PSN_pfOutputFile)
			{
				retVal = process_snapshot_error;
				fprintf(stderr, "process_snapshot_initialize: Failed to open output file!\n");
			}
			else
			{
				configure_output_buffer(PSN_pfOutputFile);
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

			PSN_pfOutputJsonlFile = fopen(openfilePath, "a");
			if(!PSN_pfOutputJsonlFile)
			{
				retVal = process_snapshot_error;
				fprintf(stderr, "process_snapshot_initialize: Failed to open jsonl output file!\n");
			}
			else
			{
				configure_output_buffer(PSN_pfOutputJsonlFile);
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

	if(dir)
	{
		if(closedir(dir)!=0)
		{
			fprintf(stderr, "process_snapshot_deinit: Failed to close proc dir!\n");
		}
	}
}
