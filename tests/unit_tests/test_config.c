#include "unity.h"

#include "config.h"

#include <stdlib.h>
#include <unistd.h>


/* --------------------------------------------------
   Setup / Teardown
-------------------------------------------------- */

void setUp(void)
{
    unsetenv("PROCESS_ANALYZER_CONFIG");
}

void tearDown(void)
{
}


/* --------------------------------------------------
   Test 1: Default config
-------------------------------------------------- */

void test_config_default_values(void)
{
    config_status ret;

    ret = config_init();

    TEST_ASSERT_EQUAL(config_success, ret);

    TEST_ASSERT_TRUE(config_get_raw_log_enabled());
    TEST_ASSERT_TRUE(config_get_raw_jsonl_enabled());
    TEST_ASSERT_FALSE(config_get_raw_console_enabled());

    TEST_ASSERT_TRUE(config_get_metrics_console_enabled());
    TEST_ASSERT_TRUE(config_get_metrics_json_enabled());

    TEST_ASSERT_FALSE(config_get_include_self());

    TEST_ASSERT_EQUAL(3, config_get_max_number_of_files());
}


/* --------------------------------------------------
   Test 2: Valid config file
-------------------------------------------------- */

void test_config_valid_file(void)
{
    config_status ret;

    setenv("PROCESS_ANALYZER_CONFIG",
           "../../../tests/unit_tests/test_data/valid.conf",
           1);

    ret = config_init();

    TEST_ASSERT_EQUAL(config_success, ret);

    TEST_ASSERT_EQUAL_STRING(
        "/tmp/ptime_test",
        config_get_output_dir()
    );

    TEST_ASSERT_TRUE(config_get_raw_log_enabled());
    TEST_ASSERT_FALSE(config_get_raw_jsonl_enabled());

    TEST_ASSERT_TRUE(config_get_include_self());

    TEST_ASSERT_EQUAL(3, config_get_max_number_of_files());
}


/* --------------------------------------------------
   Test 3: Invalid config
-------------------------------------------------- */

void test_config_invalid_file(void)
{
    config_status ret;

    setenv("PROCESS_ANALYZER_CONFIG",
           "../../../tests/unit_tests/test_data/invalid.conf",
           1);

    ret = config_init();

    TEST_ASSERT_EQUAL(config_error_parse, ret);
}


/* --------------------------------------------------
   Main
-------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_config_default_values);
    RUN_TEST(test_config_valid_file);
    RUN_TEST(test_config_invalid_file);

    return UNITY_END();
}
