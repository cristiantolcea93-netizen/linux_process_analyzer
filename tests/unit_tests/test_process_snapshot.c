#include "unity.h"
#include "process_snapshot.h"
#include <stdbool.h>
#include <stddef.h>

#include "../../code/process_stats/process_stats.h"
#include "../../code/config/config.h"

#include "../../code/process_snapshot/process_snapshot.c"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

// ---- MOCK CONFIG ----

const char* config_get_output_dir(void)
{
    return "/tmp";
}

bool config_get_raw_log_enabled(void)
{
    return true;
}

bool config_get_raw_jsonl_enabled(void)
{
    return true;
}

bool config_get_raw_console_enabled(void)
{
    return false;
}

int config_get_max_number_of_files(void)
{
    return 3;
}

off_t config_get_max_file_size_bytes(void)
{
    return 1024 * 1024;
}

bool config_get_include_self(void)
{
    return false;
}

bool config_get_compression_enabled(void)
{
	return false;
}

// ---- MOCK PROCESS STATS ----

void process_stats_update(process_state_input_t* input)
{
    (void)input;
}

void process_stats_snapshot_end(void)
{
}

void setUp(void) {}
void tearDown(void) {}

void test_is_numeric(void)
{
    TEST_ASSERT_TRUE(is_numeric("1234"));
    TEST_ASSERT_TRUE(is_numeric("0"));

    TEST_ASSERT_FALSE(is_numeric("12a"));
    TEST_ASSERT_FALSE(is_numeric("abc"));
    TEST_ASSERT_FALSE(is_numeric(""));

    TEST_ASSERT_FALSE(is_numeric(" "));
    TEST_ASSERT_FALSE(is_numeric("-1"));
    TEST_ASSERT_FALSE(is_numeric("+12"));

}

void test_get_file_size(void)
{
    const char *file = "/tmp/test_ps_size";

    FILE *f = fopen(file, "w");
    TEST_ASSERT_NOT_NULL(f);

    fprintf(f, "1234567890"); // 10 bytes
    fclose(f);

    off_t sz = get_file_size(file);

    TEST_ASSERT_EQUAL(10, sz);

    unlink(file);
}

void test_rotate_logs(void)
{
    const char *base = "/tmp/ptime_test";

    char file[256];

    // Create main file
    snprintf(file, sizeof(file), "%s.log", base);
    FILE *f = fopen(file, "w");
    fprintf(f, "main");
    fclose(f);

    // Create .1
    char f1[256];
    snprintf(f1, sizeof(f1), "%s.log.1", base);
    f = fopen(f1, "w");
    fprintf(f, "old1");
    fclose(f);

    rotate_logs(file);

    // After rotate:
    // main -> .1

    struct stat st;

    snprintf(f1, sizeof(f1), "%s.log.1", base);
    TEST_ASSERT_EQUAL(0, stat(f1, &st));

    unlink(file);
    unlink(f1);
}


void test_acquire_lock(void)
{
    const char *lock = "/tmp/ptime_test.lock";

    process_snapshot_status r1 = acquire_lock(lock);
    TEST_ASSERT_EQUAL(process_snapshot_success, r1);

    // second lock should fail
    process_snapshot_status r2 = acquire_lock(lock);
    TEST_ASSERT_NOT_EQUAL(process_snapshot_success, r2);

    unlink(lock);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_numeric);
    RUN_TEST(test_get_file_size);
    RUN_TEST(test_rotate_logs);
    RUN_TEST(test_acquire_lock);

    return UNITY_END();
}







