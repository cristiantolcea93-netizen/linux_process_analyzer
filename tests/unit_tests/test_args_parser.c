#include "unity.h"

#include "../../code/args_parser/args_parser.h"

#include <string.h>
#include <stdlib.h>


/* =========================================================
   Setup / Teardown
   ========================================================= */

void setUp(void)
{
}

void tearDown(void)
{
}


/* =========================================================
   Helpers
   ========================================================= */

static void reset_cfg(ap_arguments *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

static parse_args_status run_parse(
    int argc, char **argv,
    ap_arguments *cfg)
{
    optind = 1;
    optarg = NULL;
    opterr = 0;

    return ap_parse_args(argc, argv, cfg);
}

/* =========================================================
   Tests
   ========================================================= */


/* No arguments */
void test_no_args(void)
{
    char *argv[] = {
        "process_analyzer", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(1, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);

    TEST_ASSERT_EQUAL(0, cfg.interval_ms);
    TEST_ASSERT_EQUAL(0, cfg.count);
    TEST_ASSERT_FALSE(cfg.delete_old_files);
}


/* Basic valid args */
void test_basic_args(void)
{

    char *argv[] = {
        "process_analyzer",
        "-i", "100ms",
        "-n", "10",
        "-j", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(6, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_ok, ret);

    TEST_ASSERT_EQUAL(100, cfg.interval_ms);
    TEST_ASSERT_EQUAL(10, cfg.count);
    TEST_ASSERT_EQUAL(1,cfg.delete_old_files);
}


/* Long options */
void test_long_options(void)
{
    char *argv[] = {
        "process_analyzer",
        "--interval", "1s",
        "--count", "5", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret =run_parse(5, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_ok, ret);

    TEST_ASSERT_EQUAL(1000, cfg.interval_ms);
    TEST_ASSERT_EQUAL(5, cfg.count);
}


/* CPU metric option */
void test_cpu_metric(void)
{
    char *argv[] = {
        "process_analyzer",
        "-c", "7", "-i",
		"1m", "-n", "2", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(7, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_ok, ret);

    TEST_ASSERT_TRUE(cfg.end_metrics_args.cpu_average_requested);
    TEST_ASSERT_EQUAL(7,
        cfg.end_metrics_args.cpu_average_pids_to_display);
}


/* Multiple metrics */
void test_multiple_metrics(void)
{
    char *argv[] = {
        "process_analyzer",
        "-c", "5",
        "-r", "3",
        "-s", "2",
        "-d", "4",
		"-i","1m",
		"-n", "2",
		NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(13, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_ok, ret);

    TEST_ASSERT_TRUE(cfg.end_metrics_args.cpu_average_requested);
    TEST_ASSERT_TRUE(cfg.end_metrics_args.rss_average_requested);
    TEST_ASSERT_TRUE(cfg.end_metrics_args.rss_increase_requested);
    TEST_ASSERT_TRUE(cfg.end_metrics_args.rss_delta_requested);

    TEST_ASSERT_EQUAL(5,
        cfg.end_metrics_args.cpu_average_pids_to_display);

    TEST_ASSERT_EQUAL(3,
        cfg.end_metrics_args.rss_average_pids_to_display);

    TEST_ASSERT_EQUAL(2,
        cfg.end_metrics_args.rss_increase_pids_to_display);

    TEST_ASSERT_EQUAL(4,
        cfg.end_metrics_args.rss_delta_pids_to_display);
}


/* Infinity count */
void test_infinity_count(void)
{
    char *argv[] = {
        "process_analyzer",
		"-i","1m",
        "-n", "infinity", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(5, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_ok, ret);

	TEST_ASSERT_EQUAL(0x7FFFFFFF, cfg.count);
}

/* Filter by one PID */
void test_filter_by_pid_single(void)
{
	char *argv[] = {
		"process_analyzer",
		"-i", "100ms",
		"-n", "10",
		"-k", "1234",
		NULL
	};

	ap_arguments cfg;
	reset_cfg(&cfg);

	parse_args_status ret = run_parse(7, argv, &cfg);

	TEST_ASSERT_EQUAL(parse_args_ok, ret);
	TEST_ASSERT_EQUAL(1, cfg.filter_pids_count);
	TEST_ASSERT_EQUAL(1234, cfg.filter_pids[0]);
}

/* Filter by comma separated PID list */
void test_filter_by_pid_list(void)
{
	char *argv[] = {
		"process_analyzer",
		"-i", "100ms",
		"-n", "10",
		"-k", "1234,4567,9999",
		NULL
	};

	ap_arguments cfg;
	reset_cfg(&cfg);

	parse_args_status ret = run_parse(7, argv, &cfg);

	TEST_ASSERT_EQUAL(parse_args_ok, ret);
	TEST_ASSERT_EQUAL(3, cfg.filter_pids_count);
	TEST_ASSERT_EQUAL(1234, cfg.filter_pids[0]);
	TEST_ASSERT_EQUAL(4567, cfg.filter_pids[1]);
	TEST_ASSERT_EQUAL(9999, cfg.filter_pids[2]);
}

/* Invalid PID list */
void test_filter_by_pid_invalid_list(void)
{
	char *argv[] = {
		"process_analyzer",
		"-i", "100ms",
		"-n", "10",
		"-k", "1234,abc",
		NULL
	};

	ap_arguments cfg;
	reset_cfg(&cfg);

	parse_args_status ret = run_parse(7, argv, &cfg);

	TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* Invalid interval */
void test_invalid_interval(void)
{
    char *argv[] = {
        "process_analyzer",
        "-i", "xyz",NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(3, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* Missing value */
void test_missing_value(void)
{
    char *argv[] = {
        "process_analyzer",
        "-i", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(2, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* Unknown option */
void test_unknown_option(void)
{
    char *argv[] = {
        "process_analyzer",
        "--unknown", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(2, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* Help */
void test_help(void)
{
    char *argv[] = {
        "process_analyzer",
        "--help", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(2, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* Version */
void test_version(void)
{
    char *argv[] = {
        "process_analyzer",
        "--version", NULL
    };

    ap_arguments cfg;
    reset_cfg(&cfg);

    parse_args_status ret = run_parse(2, argv, &cfg);

    TEST_ASSERT_EQUAL(parse_args_error, ret);
}


/* =========================================================
   Main
   ========================================================= */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_args);
    RUN_TEST(test_basic_args);
    RUN_TEST(test_long_options);
    RUN_TEST(test_cpu_metric);
    RUN_TEST(test_multiple_metrics);
    RUN_TEST(test_infinity_count);
    RUN_TEST(test_filter_by_pid_single);
    RUN_TEST(test_filter_by_pid_list);
    RUN_TEST(test_filter_by_pid_invalid_list);
    RUN_TEST(test_invalid_interval);
    RUN_TEST(test_missing_value);
    RUN_TEST(test_unknown_option);
    RUN_TEST(test_help);
    RUN_TEST(test_version);

    return UNITY_END();
}
