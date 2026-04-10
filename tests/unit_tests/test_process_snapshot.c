#include "unity.h"
#include "process_snapshot.h"
#include <stdbool.h>
#include <stddef.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../../code/process_stats/process_stats.h"
#include "../../code/config/config.h"

#define TEST_PROC_ROOT "/tmp/process_snapshot_unit_proc"

static DIR *(*real_opendir_fn)(const char *) = opendir;
static const char *g_mock_opendir_path = NULL;
static int g_mock_opendir_errno = 0;

static DIR *mockable_opendir(const char *path)
{
    if ((g_mock_opendir_path != NULL) &&
        (strcmp(path, g_mock_opendir_path) == 0))
    {
        errno = g_mock_opendir_errno;
        return NULL;
    }

    return real_opendir_fn(path);
}

#define PROC_PATH TEST_PROC_ROOT
#define opendir mockable_opendir
#include "../../code/process_snapshot/process_snapshot.c"
#undef opendir
#undef PROC_PATH

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

void process_stats_update(const process_state_input_t* input)
{
    (void)input;
}

void process_stats_snapshot_end(void)
{
}

static void remove_tree_if_exists(const char *path)
{
    DIR *dir_handle = opendir(path);
    struct dirent *entry;

    if (dir_handle == NULL)
    {
        unlink(path);
        rmdir(path);
        return;
    }

    while ((entry = readdir(dir_handle)) != NULL)
    {
        char child_path[512];

        if ((strcmp(entry->d_name, ".") == 0) ||
            (strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }

        snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        remove_tree_if_exists(child_path);
    }

    closedir(dir_handle);
    rmdir(path);
}

static void ensure_dir_exists(const char *path, mode_t mode)
{
    int ret = mkdir(path, mode);

    if ((ret != 0) && (errno != EEXIST))
    {
        TEST_FAIL_MESSAGE("Failed to create test directory");
    }
}

static void create_empty_file(const char *path)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);

    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
}

static void create_fd_fixture(pid_t pid)
{
    char path[512];

    ensure_dir_exists(TEST_PROC_ROOT, 0700);

    snprintf(path, sizeof(path), "%s/%d", TEST_PROC_ROOT, pid);
    ensure_dir_exists(path, 0700);

    snprintf(path, sizeof(path), "%s/%d/fd", TEST_PROC_ROOT, pid);
    ensure_dir_exists(path, 0700);
}

static void create_proc_fixture(pid_t pid)
{
    char path[512];

    ensure_dir_exists(TEST_PROC_ROOT, 0700);

    snprintf(path, sizeof(path), "%s/%d", TEST_PROC_ROOT, pid);
    ensure_dir_exists(path, 0700);
}

static void create_text_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");

    TEST_ASSERT_NOT_NULL(file);
    fputs(content, file);
    fclose(file);
}

void setUp(void)
{
    g_mock_opendir_path = NULL;
    g_mock_opendir_errno = 0;
    remove_tree_if_exists(TEST_PROC_ROOT);
    ensure_dir_exists(TEST_PROC_ROOT, 0700);
}

void tearDown(void)
{
    g_mock_opendir_path = NULL;
    g_mock_opendir_errno = 0;
    remove_tree_if_exists(TEST_PROC_ROOT);
}

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

void test_is_pid_in_filter(void)
{
	int pids[] = {101, 202, 303};
	ap_pid_whitelist whitelist = {
		.filter_pids = pids,
		.filter_pids_count = 3,
		.filter_pids_capacity = 3,
		.filter_comms = NULL,
		.filter_comms_count = 0,
		.filter_comms_capacity = 0
	};

	TEST_ASSERT_TRUE(is_pid_in_filter(202, &whitelist));
	TEST_ASSERT_FALSE(is_pid_in_filter(999, &whitelist));

}

void test_is_comm_in_filter(void)
{
	char comm0[] = "bash";
	char comm1[] = "sleep";
	char *comms[] = {comm0, comm1};
	ap_pid_whitelist whitelist = {
		.filter_pids = NULL,
		.filter_pids_count = 0,
		.filter_pids_capacity = 0,
		.filter_comms = comms,
		.filter_comms_count = 2,
		.filter_comms_capacity = 2
	};

	TEST_ASSERT_TRUE(is_comm_in_filter("bash", &whitelist));
	TEST_ASSERT_TRUE(is_comm_in_filter("sleep", &whitelist));
	TEST_ASSERT_FALSE(is_comm_in_filter("systemd", &whitelist));
}

void test_if_filtering_enabled(void)
{
	ap_pid_whitelist whitelist;

	whitelist.filter_pids_count=0;
	whitelist.filter_comms_count=0;

	TEST_ASSERT_FALSE(is_filtering_enabled(&whitelist));

	whitelist.filter_pids_count=1;
	whitelist.filter_comms_count=0;

	TEST_ASSERT_TRUE(is_filtering_enabled(&whitelist));

	whitelist.filter_pids_count=0;
	whitelist.filter_comms_count=12;

	TEST_ASSERT_TRUE(is_filtering_enabled(&whitelist));

	whitelist.filter_pids_count=100;
	whitelist.filter_comms_count=12;
	TEST_ASSERT_TRUE(is_filtering_enabled(&whitelist));
}

void test_count_open_fds_for_pid_returns_correct_count(void)
{
    const pid_t pid = 4242;
    char path[512];

    create_fd_fixture(pid);

    snprintf(path, sizeof(path), "%s/%d/fd/0", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    snprintf(path, sizeof(path), "%s/%d/fd/1", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    snprintf(path, sizeof(path), "%s/%d/fd/25", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    snprintf(path, sizeof(path), "%s/%d/fd/not_an_fd", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    TEST_ASSERT_EQUAL(3, count_open_fds_for_pid(pid));
}

void test_count_open_fds_for_pid_returns_minus_one_for_invalid_pid(void)
{
    TEST_ASSERT_EQUAL(-1, count_open_fds_for_pid(99999));
}

void test_count_open_fds_for_pid_handles_permission_denied(void)
{
    const pid_t pid = 5151;
    char path[512];

    create_fd_fixture(pid);

    snprintf(path, sizeof(path), "%s/%d/fd/0", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    snprintf(path, sizeof(path), "%s/%d/fd", TEST_PROC_ROOT, pid);
    g_mock_opendir_path = path;
    g_mock_opendir_errno = EACCES;

    TEST_ASSERT_EQUAL(-1, count_open_fds_for_pid(pid));

    g_mock_opendir_path = NULL;
    g_mock_opendir_errno = 0;
}

void test_read_fd_sets_valid_flag_and_fd_count(void)
{
    const pid_t pid = 6161;
    char path[512];
    process_state_input_t process_data;

    memset(&process_data, 0, sizeof(process_data));
    process_data.pid = pid;

    create_fd_fixture(pid);

    snprintf(path, sizeof(path), "%s/%d/fd/0", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    snprintf(path, sizeof(path), "%s/%d/fd/1", TEST_PROC_ROOT, pid);
    create_empty_file(path);

    read_fd(&process_data);

    TEST_ASSERT_TRUE(process_data.bo_is_fd_valid);
    TEST_ASSERT_EQUAL(2, process_data.number_of_fds);
}

void test_read_fd_clears_value_when_directory_cannot_be_read(void)
{
    const pid_t pid = 7171;
    char path[512];
    process_state_input_t process_data;

    memset(&process_data, 0, sizeof(process_data));
    process_data.pid = pid;
    process_data.number_of_fds = 99;
    process_data.bo_is_fd_valid = true;

    create_fd_fixture(pid);

    snprintf(path, sizeof(path), "%s/%d/fd", TEST_PROC_ROOT, pid);
    g_mock_opendir_path = path;
    g_mock_opendir_errno = EACCES;

    read_fd(&process_data);

    TEST_ASSERT_FALSE(process_data.bo_is_fd_valid);
    TEST_ASSERT_EQUAL(0, process_data.number_of_fds);

    g_mock_opendir_path = NULL;
    g_mock_opendir_errno = 0;
}

void test_read_proc_stat_reads_rss_threads_and_io_without_status_file(void)
{
    const pid_t pid = 8181;
    char path[512];
    long expected_rss_kb;
    long page_size = sysconf(_SC_PAGESIZE);
    process_state_input_t process_data;

    if (page_size <= 0)
    {
        page_size = 4096;
    }

    create_proc_fixture(pid);

    snprintf(path, sizeof(path), "%s/%d/stat", TEST_PROC_ROOT, pid);
    create_text_file(
        path,
        "8181 (demo_proc) R 1 2 3 4 5 6 7 8 9 10 110 220 0 0 0 0 7 0 12345 4096 25 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");

    snprintf(path, sizeof(path), "%s/%d/io", TEST_PROC_ROOT, pid);
    create_text_file(
        path,
        "read_bytes: 4096\n"
        "write_bytes: 8192\n");

    memset(&process_data, 0, sizeof(process_data));
    process_data.pid = pid;

    TEST_ASSERT_EQUAL(0, read_proc_stat(pid, &process_data));

    expected_rss_kb = 25 * (page_size / 1024);

    TEST_ASSERT_EQUAL(pid, process_data.pid);
    TEST_ASSERT_EQUAL_STRING("demo_proc", process_data.comm);
    TEST_ASSERT_EQUAL('R', process_data.state);
    TEST_ASSERT_EQUAL(1, process_data.ppid);
    TEST_ASSERT_EQUAL(110, process_data.utime);
    TEST_ASSERT_EQUAL(220, process_data.stime);
    TEST_ASSERT_TRUE(process_data.bo_is_rss_valid);
    TEST_ASSERT_EQUAL(7, process_data.threads);
    TEST_ASSERT_EQUAL(expected_rss_kb, process_data.rssKb);
    TEST_ASSERT_TRUE(process_data.bo_is_io_valid);
    TEST_ASSERT_EQUAL_UINT64(4, process_data.read_kbytes);
    TEST_ASSERT_EQUAL_UINT64(8, process_data.write_kbytes);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_numeric);
    RUN_TEST(test_get_file_size);
    RUN_TEST(test_rotate_logs);
    RUN_TEST(test_acquire_lock);
    RUN_TEST(test_is_pid_in_filter);
    RUN_TEST(test_is_comm_in_filter);
    RUN_TEST(test_if_filtering_enabled);
    RUN_TEST(test_count_open_fds_for_pid_returns_correct_count);
    RUN_TEST(test_count_open_fds_for_pid_returns_minus_one_for_invalid_pid);
    RUN_TEST(test_count_open_fds_for_pid_handles_permission_denied);
    RUN_TEST(test_read_fd_sets_valid_flag_and_fd_count);
    RUN_TEST(test_read_fd_clears_value_when_directory_cannot_be_read);
    RUN_TEST(test_read_proc_stat_reads_rss_threads_and_io_without_status_file);

    return UNITY_END();
}
