#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include "../../code/config/config.h"

/*
 * Include implementation directly to access static functions.
 * This is acceptable for unit testing internal helpers.
 */
#include "../../code/compression/compression_worker.c"


void setUp(void) {}
void tearDown(void) {}

/* ============================================================
 * Helpers
 * ============================================================ */

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "%s", content);
    fclose(f);
}


/* ============================================================
 * Test: extract_base_path
 * ============================================================ */

void test_extract_base_path_basic(void)
{
    char out[PATH_MAX];

    TEST_ASSERT_EQUAL(
        0,
        extract_base_path("/tmp/ptime.jsonl.1.123.4", out, sizeof(out))
    );

    TEST_ASSERT_EQUAL_STRING("/tmp/ptime.jsonl", out);
}


void test_extract_base_path_short(void)
{
    char out[PATH_MAX];

    TEST_ASSERT_EQUAL(
        0,
        extract_base_path("/tmp/ptime.log.1.2.3", out, sizeof(out))
    );

    TEST_ASSERT_EQUAL_STRING("/tmp/ptime.log", out);
}


/* ============================================================
 * Test: compress_file_gzip
 * ============================================================ */

void test_compress_file_basic(void)
{
    const char *src = "/tmp/test_src.txt";
    const char *dst = "/tmp/test_dst.gz";

    write_file(src, "hello world");

    TEST_ASSERT_EQUAL(0, compress_file_gzip(src, dst));
    TEST_ASSERT_TRUE(file_exists(dst));

    struct stat st;
    stat(dst, &st);
    TEST_ASSERT_TRUE(st.st_size > 0);

    unlink(src);
    unlink(dst);
}


/* ============================================================
 * Test: worker integration
 * ============================================================ */

void test_worker_basic(void)
{
    /* Enable compression via config */
    setenv("PROCESS_ANALYZER_CONFIG", "/tmp/test_config.cfg", 1);

    FILE *cfg = fopen("/tmp/test_config.cfg", "w");
    TEST_ASSERT_NOT_NULL(cfg);


    fprintf(cfg,
        "output_dir=/tmp\n"
    	"raw_log_enabled=true\n"
        "raw_jsonl_enabled=true\n"
    	"raw_console_enabled=false\n"
    	"compression_enabled=true\n"
    	"metrics_on_console=true\n"
    	"metrics_on_json=true\n"
    	"max_file_size=1k\n"
    	"max_number_of_files=3\n"
    	"include_self=false\n"
    );
    fclose(cfg);

    config_init();

    compression_worker_init();

    const char *rotated = "/tmp/ptime.jsonl.1.123.1";
    const char *final_gz = "/tmp/ptime.jsonl.1.gz";

    write_file(rotated, "worker data");

    compression_enqueue_file(rotated);

    /* allow worker to process */
    usleep(300000); // 300 ms

    TEST_ASSERT_TRUE(file_exists(final_gz));

    compression_worker_shutdown();

    unlink(final_gz);
    unlink("/tmp/test_config.cfg");
}


/* ============================================================
 * Unity runner
 * ============================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_extract_base_path_basic);
    RUN_TEST(test_extract_base_path_short);
    RUN_TEST(test_compress_file_basic);
    RUN_TEST(test_worker_basic);

    return UNITY_END();
}
