#include "unity.h"

#include "../code/process_stats/process_stats.h"

#include <string.h>



static double t = 0;

void reset_time(void)
{
    t = 0;
}
/* -------------------------------------------------
 * Test lifecycle
 * ------------------------------------------------- */

void setUp(void)
{
    /* reset internal state before each test */
    reset_time();
    process_stats_deinit();
    process_stats_initialize("test");
}

void tearDown(void)
{

}


/* -------------------------------------------------
 * Helpers
 * ------------------------------------------------- */

static process_state_input_t make_sample(
    pid_t pid,
    unsigned long utime,
	unsigned long stime,
    long rss,
    uint64_t read_kb,
    uint64_t write_kb,
    int threads
)
{
	t += 1.0;

    process_state_input_t s;

    memset(&s, 0, sizeof(s));

    s.pid   = pid;
    s.state = 'R';

    s.utime = utime;
    s.stime = stime;

    s.rssKb = rss;
    s.bo_is_rss_valid = (rss >= 0);

    s.read_kbytes  = read_kb;
    s.write_kbytes = write_kb;
    s.bo_is_io_valid = true;

    s.threads = threads;
    s.timestamp = t;

    return s;
}


/* -------------------------------------------------
 * Tests
 * ------------------------------------------------- */


/* Basic init works */
void test_initialize(void)
{
    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NULL(list);
}


/* Single process update */
void test_single_process_update(void)
{
    process_state_input_t s =
        make_sample(100, 2000,5122, 2000, 100, 50, 2);

    process_stats_update(&s);
    process_stats_snapshot_end();

    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NOT_NULL(list);

    TEST_ASSERT_EQUAL(100, list->pid);
    TEST_ASSERT_EQUAL(2, list->threads);
}


/* Multiple updates for same PID */
void test_multiple_updates_same_pid(void)
{
    long hz   = sysconf(_SC_CLK_TCK);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);

    TEST_ASSERT(hz > 0);
    TEST_ASSERT(cpus > 0);

    //need to set a config without json file write to avoid test seg fault when calling process_stats_print_metrics
    setenv("PROCESS_ANALYZER_CONFIG",
               "../../tests/test_data/valid.conf",
               1);
    config_init();

    double target_cpu = 80.0;

    unsigned long delta_ticks =
        (unsigned long)((target_cpu / 100.0) * hz * cpus);


    unsigned long utime = 100;
    unsigned long stime = 100;
    long rss = 2000;
    long sum_rss=0;
    long rss_decrease_per_iteration=-10;
    int i;
    uint64_t readKb = 50;
    uint64_t writeKb = 10;

    uint64_t read_increase_per_iteration = 500;
    uint64_t write_increase_per_iteration = 20;

    for(int i =0; i<10;i++)
    {
    	process_state_input_t s1 =
    			make_sample(200,
    					utime,
						stime,
						rss,
						readKb,
						writeKb,
						1);

         sum_rss+=rss;
    	 process_stats_update(&s1);
    	 process_stats_snapshot_end();
    	 utime+=delta_ticks / 2;
    	 stime+=delta_ticks / 2;
    	 rss += rss_decrease_per_iteration;
    	 readKb += read_increase_per_iteration;
    	 writeKb += write_increase_per_iteration;

    }
    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NOT_NULL(list);

    /*
     * check CPU
     *
     */
    TEST_ASSERT_FLOAT_WITHIN(2.0, target_cpu,list->cpu_usage);

    /*
     * RSS delta
     */
    i=9;
    TEST_ASSERT_EQUAL(i*rss_decrease_per_iteration,list->rss_variation_since_startup);

    /*
     * IO total initial  numOfIterations * increase_per_iteration
     */
    i=9;
    TEST_ASSERT_EQUAL((i*read_increase_per_iteration),list->total_read_kbytes);

    TEST_ASSERT_EQUAL((i*write_increase_per_iteration),list->total_write_kbytes);


    process_stats_metrics_arguments args;
    memset(&args, 0x00, sizeof(args));
    args.cpu_average_requested = true;
    args.rss_average_requested = true;
    args.write_rate_requested = true;
    args.read_rate_requested = true;

    process_stats_print_metrics(&args,0);

    list = process_stats_get_all();


    //average cpu
    TEST_ASSERT_FLOAT_WITHIN(2.0, target_cpu,list->average_cpu_usage);
    //average rss
    TEST_ASSERT_EQUAL((sum_rss/10),list->rss_average_kb);

    TEST_ASSERT_FLOAT_WITHIN(1.0, read_increase_per_iteration,list->avg_read_rate_kb_s);

    TEST_ASSERT_FLOAT_WITHIN(1.0, write_increase_per_iteration,list->avg_write_rate_kb_s);

}



/* Two different processes */
void test_multiple_processes(void)
{
    process_state_input_t p1 =
        make_sample(300, 20, 52, 1000, 10, 5, 1);

    process_state_input_t p2 =
        make_sample(400, 20, 52, 2000, 20, 15, 3);

    process_stats_update(&p1);
    process_stats_update(&p2);

    process_stats_snapshot_end();

    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NOT_NULL(list);

    size_t count = HASH_COUNT(list);
    TEST_ASSERT_EQUAL(2, count);

    int found1 = 0;
    int found2 = 0;

    process_state_t *cur, *tmp;

    HASH_ITER(hh, list, cur, tmp)
    {
        if (cur->pid == 300)
        {
            found1 = 1;
            TEST_ASSERT_EQUAL(1, cur->threads);
        }

        if (cur->pid == 400)
        {
            found2 = 1;
            TEST_ASSERT_EQUAL(3, cur->threads);
        }
    }

    TEST_ASSERT_TRUE(found1);
    TEST_ASSERT_TRUE(found2);
}



/* RSS invalid samples ignored */
void test_invalid_rss_ignored(void)
{
    process_state_input_t s1 =
        make_sample(500, 20,52, -1, 10, 5, 1);

    process_state_input_t s2 =
        make_sample(500, 20,52, 2000, 10, 5, 1);

    process_stats_update(&s1);
    process_stats_snapshot_end();

    process_stats_update(&s2);
    process_stats_snapshot_end();

    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NOT_NULL(list);

    /* First invalid ignored */
    TEST_ASSERT_EQUAL(0, list->rss_variation_since_startup);
}


/* IO invalid samples ignored */
void test_invalid_io_ignored(void)
{
    process_state_input_t s1 = make_sample(600, 20,52, 1000, 0, 0 , 1);

    s1.bo_is_io_valid = false;

    process_state_input_t s2 = make_sample(600, 20,50, 1200, 20, 15, 1);

    process_stats_update(&s1);
    process_stats_snapshot_end();

    process_stats_update(&s2);
    process_stats_snapshot_end();

    process_state_input_t s3 = make_sample(600, 20,50, 1200, 40, 30, 1);

    process_stats_update(&s3);
    process_stats_snapshot_end();

    process_state_t *list = process_stats_get_all();

    TEST_ASSERT_NOT_NULL(list);

    /* Only s2 and s3 samples counted */
    TEST_ASSERT_EQUAL(20, list->total_read_kbytes);
    TEST_ASSERT_EQUAL(15, list->total_write_kbytes);
}


/* -------------------------------------------------
 * Main
 * ------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_initialize);
    RUN_TEST(test_single_process_update);
    RUN_TEST(test_multiple_updates_same_pid);
    RUN_TEST(test_multiple_processes);
    RUN_TEST(test_invalid_rss_ignored);
    RUN_TEST(test_invalid_io_ignored);

    return UNITY_END();
}
