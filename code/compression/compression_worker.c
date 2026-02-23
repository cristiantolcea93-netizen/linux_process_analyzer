#include "compression_worker.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <zlib.h>

#include "config.h"

#define GZ_SUFFIX ".gz"

typedef struct compress_job {
    char path[PATH_MAX];
    struct compress_job *next;
} compress_job_t;


static compress_job_t *job_head = NULL;
static compress_job_t *job_tail = NULL;

static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;

static pthread_t worker_thread;
static bool worker_running = 0;

static int extract_base_path(const char *rotated_path, char *base_out, size_t base_out_size);
static int compress_file_gzip(const char *src, const char *dst);
static void *compression_thread_func(void *arg);



/**
 * Extract base log path from rotated path.
 *
 * Example:
 *   /dir/ptime.jsonl.1.123.x  -> /dir/ptime.jsonl
 *   /dir/ptime.jsonl.1        -> /dir/ptime.jsonl
 *
 * Returns 0 on success, -1 on failure.
 */
static int extract_base_path(const char *rotated_path,char *base_out,size_t base_out_size)
{
    if (!rotated_path || !base_out || base_out_size == 0)
        return -1;

    size_t len = strnlen(rotated_path, base_out_size);

    if (len == 0 || len >= base_out_size)
        return -1;

    memcpy(base_out, rotated_path, len + 1);

    // remove last 3 extensions if present (.counter, .timestamp, .1)
    for (int i = 0; i < 3; i++)
    {
        char *dot = strrchr(base_out, '.');
        if (!dot)
            return -1;

        *dot = '\0';
    }

    return 0;
}
/* ============================================================
 * GZIP compression
 * ============================================================ */

static int compress_file_gzip(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;

    gzFile out = gzopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        return -2;
    }

    char buffer[8192];
    int read_bytes;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {

        int written = gzwrite(out, buffer, read_bytes);

        if (written == 0)
        {
            int err;
            const char *msg = gzerror(out, &err);
            fprintf(stderr, "gzwrite error: %s\n", msg);
            gzclose(out);
            fclose(in);
            return -3;
        }
    }

    gzclose(out);
    fclose(in);

    return 0;
}


/* ============================================================
 * Queue handling
 * ============================================================ */

void compression_enqueue_file(const char *path)
{
	if(config_get_compression_enabled())
	{
		if (!path)
			return;

		compress_job_t *job = malloc(sizeof(compress_job_t));
		if (!job)
		{
			fprintf(stderr, "compression_enqueue_file: could not allocate memory for the job\n");
			return;
		}


		strncpy(job->path, path, PATH_MAX - 1);
		job->path[PATH_MAX - 1] = '\0';
		job->next = NULL;

		pthread_mutex_lock(&job_mutex);

		if (job_tail)
			job_tail->next = job;
		else
			job_head = job;

		job_tail = job;

		pthread_cond_signal(&job_cond);
		pthread_mutex_unlock(&job_mutex);
	}
}


/* ============================================================
 * Worker thread
 * ============================================================ */

static void *compression_thread_func(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&job_mutex);

        while (worker_running && job_head == NULL)
            pthread_cond_wait(&job_cond, &job_mutex);

        if (!worker_running && job_head == NULL)
        {
            pthread_mutex_unlock(&job_mutex);
            break;
        }

        compress_job_t *job = job_head;

        if (job_head)
            job_head = job_head->next;

        if (!job_head)
            job_tail = NULL;

        pthread_mutex_unlock(&job_mutex);

        if (!job)
            continue;

        char gz_path[PATH_MAX];
        char final_gz_path[PATH_MAX];
        char base_path[PATH_MAX];

        if (extract_base_path(job->path, base_path, sizeof(base_path)) != 0)
        {
            fprintf(stderr, "failed to extract base path from %s\n", job->path);
            free(job);
            continue;
        }

        // tmp gz = job path + .gz
        int written = snprintf(gz_path, sizeof(gz_path), "%s%s", job->path, GZ_SUFFIX);

        if (written < 0 || (size_t)written >= sizeof(gz_path))
        {
            fprintf(stderr, "compression_thread_func: path too long for %s\n", job->path);
            free(job);
            continue;
        }

        // final gz = base + ".1.gz"
        written = snprintf(final_gz_path, sizeof(final_gz_path),
                           "%s.1%s", base_path, GZ_SUFFIX);

        if (written < 0 || (size_t)written >= sizeof(final_gz_path))
        {
            fprintf(stderr, "compression_thread_func: final path too long for %s\n", job->path);
            free(job);
            continue;
        }

        int rc = compress_file_gzip(job->path, gz_path);

        if (rc == 0)
        {
            // move tmp gzip to final rotated name
            if (rename(gz_path, final_gz_path) != 0)
            {
                fprintf(stderr,
                        "compression_thread_func: rename failed %s -> %s\n",
                        gz_path, final_gz_path);
            }
            else
            {
                unlink(job->path);   // remove original only after success
            }
        }
        else
        {
            fprintf(stderr,
                    "compression_thread_func: compression failed for %s\n",
                    job->path);
        }

        free(job);
    }

    return NULL;
}


/* ============================================================
 * Public API
 * ============================================================ */

void compression_worker_init(void)
{
	if(config_get_compression_enabled())
	{
		worker_running = true;
		pthread_create(&worker_thread, NULL, compression_thread_func, NULL);
	}
}


void compression_worker_shutdown(void)
{

	if(config_get_compression_enabled())
	{
		pthread_mutex_lock(&job_mutex);

		worker_running = false;
		pthread_cond_signal(&job_cond);

		pthread_mutex_unlock(&job_mutex);

		pthread_join(worker_thread, NULL);

		/* cleanup remaining jobs if any */

		while (job_head)
		{
			compress_job_t *tmp = job_head;
			job_head = job_head->next;
			free(tmp);
		}

		job_tail = NULL;
	}

}




