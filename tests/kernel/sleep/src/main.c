/*
 * Copyright (c) 2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tc_util.h>
#include <ztest.h>
#include <arch/cpu.h>
#include <sys/util.h>
#include <irq_offload.h>
#include <stdbool.h>

#if defined(CONFIG_ASSERT) && defined(CONFIG_DEBUG)
#define THREAD_STACK    (512 + CONFIG_TEST_EXTRA_STACKSIZE)
#else
#define THREAD_STACK    (384 + CONFIG_TEST_EXTRA_STACKSIZE)
#endif

#define TEST_THREAD_PRIORITY    -4
#define HELPER_THREAD_PRIORITY  -10

#define ONE_SECOND		(MSEC_PER_SEC)
#define ONE_SECOND_ALIGNED	\
	(uint32_t)(k_ticks_to_ms_floor64(k_ms_to_ticks_ceil32(ONE_SECOND) + _TICK_ALIGN))

#if defined(CONFIG_SOC_XILINX_ZYNQMP)
/*
 * The Xilinx QEMU, used to emulate the Xilinx ZynqMP platform, is particularly
 * unstable in terms of timing. The tick margin of at least 5 is necessary to
 * allow this test to pass with a reasonable repeatability.
 */
#define TICK_MARGIN		5
#else
#define TICK_MARGIN		1
#endif

static struct k_sem test_thread_sem;
static struct k_sem helper_thread_sem;
static struct k_sem task_sem;

static K_THREAD_STACK_DEFINE(test_thread_stack, THREAD_STACK);
static K_THREAD_STACK_DEFINE(helper_thread_stack, THREAD_STACK);

static k_tid_t test_thread_id;
static k_tid_t helper_thread_id;

static struct k_thread test_thread_data;
static struct k_thread helper_thread_data;

static bool test_failure = true;     /* Assume the test will fail */

/**
 * @brief Test sleep and wakeup APIs
 *
 * @defgroup kernel_sleep_tests Sleep Tests
 *
 * @ingroup all_tests
 *
 * This module tests the following sleep and wakeup scenarios:
 * 1. k_sleep() without cancellation
 * 2. k_sleep() cancelled via k_wakeup()
 * 3. k_sleep() cancelled via k_wakeup()
 * 4. k_sleep() cancelled via k_wakeup()
 * 5. k_sleep() - no cancellation exists
 *
 * @{
 * @}
 */
static void test_objects_init(void)
{
	k_sem_init(&test_thread_sem, 0, UINT_MAX);
	k_sem_init(&helper_thread_sem, 0, UINT_MAX);
	k_sem_init(&task_sem, 0, UINT_MAX);

	TC_PRINT("Kernel objects initialized\n");
}

static void align_to_tick_boundary(void)
{
	uint32_t tick;

	tick = k_uptime_get_32();
	while (k_uptime_get_32() == tick) {
		/* Busy wait to align to tick boundary */
#if defined(CONFIG_ARCH_POSIX)
		k_busy_wait(50);
#endif
	}

}

/* Shouldn't ever sleep for less than requested time, but allow for 1
 * tick of "too long" slop for aliasing between wakeup and
 * measurement. Qemu at least will leak the external world's clock
 * rate into the simulator when the host is under load.
 */
static int sleep_time_valid(uint32_t start, uint32_t end, uint32_t dur)
{
	uint32_t dt = end - start;

	return dt >= dur && dt <= (dur + TICK_MARGIN);
}

static void test_thread(int arg1, int arg2)
{
	uint32_t start_tick;
	uint32_t end_tick;

	k_sem_take(&test_thread_sem, K_FOREVER);

	TC_PRINT("Testing normal expiration of k_sleep()\n");
	align_to_tick_boundary();

	start_tick = k_uptime_get_32();
	k_sleep(K_SECONDS(1));
	end_tick = k_uptime_get_32();

	if (!sleep_time_valid(start_tick, end_tick, ONE_SECOND_ALIGNED)) {
		TC_ERROR(" *** k_sleep() slept for %d ticks not %d.",
			 end_tick - start_tick, ONE_SECOND_ALIGNED);

		return;
	}

	TC_PRINT("Testing: test thread sleep + helper thread wakeup test\n");
	k_sem_give(&helper_thread_sem);   /* Activate helper thread */
	align_to_tick_boundary();

	start_tick = k_uptime_get_32();
	k_sleep(K_SECONDS(1));
	end_tick = k_uptime_get_32();

	if (end_tick - start_tick > TICK_MARGIN) {
		TC_ERROR(" *** k_wakeup() took too long (%d ticks)\n",
			 end_tick - start_tick);
		return;
	}

	TC_PRINT("Testing: test thread sleep + isr offload wakeup test\n");
	k_sem_give(&helper_thread_sem);   /* Activate helper thread */
	align_to_tick_boundary();

	start_tick = k_uptime_get_32();
	k_sleep(K_SECONDS(1));
	end_tick = k_uptime_get_32();

	if (end_tick - start_tick > TICK_MARGIN) {
		TC_ERROR(" *** k_wakeup() took too long (%d ticks)\n",
			 end_tick - start_tick);
		return;
	}

	TC_PRINT("Testing: test thread sleep + main wakeup test thread\n");
	k_sem_give(&task_sem);    /* Activate task */
	align_to_tick_boundary();

	start_tick = k_uptime_get_32();
	k_sleep(K_SECONDS(1));	/* Task will execute */
	end_tick = k_uptime_get_32();

	if (end_tick - start_tick > TICK_MARGIN) {
		TC_ERROR(" *** k_wakeup() took too long (%d ticks) at LAST\n",
			 end_tick - start_tick);
		return;
	}
	test_failure = false;
}

static void irq_offload_isr(void *arg)
{

	k_wakeup((k_tid_t) arg);
}

static void helper_thread(int arg1, int arg2)
{

	k_sem_take(&helper_thread_sem, K_FOREVER);
	/* Wake the test thread */
	k_wakeup(test_thread_id);
	k_sem_take(&helper_thread_sem, K_FOREVER);
	/* Wake the test thread from an ISR */
	irq_offload(irq_offload_isr, (void *)test_thread_id);
}

/**
 * @brief Test sleep functionality
 *
 * @ingroup kernel_sleep_tests
 *
 * @see k_sleep(), k_wakeup(), k_uptime_get_32()
 */
void test_sleep(void)
{
	int status = TC_FAIL;
	uint32_t start_tick;
	uint32_t end_tick;

	/*
	 * Main thread(test_main) priority is 0 but ztest thread runs at
	 * priority -1. To run the test smoothly make both main and ztest
	 * threads run at same priority level.
	 */
	k_thread_priority_set(k_current_get(), 0);
	test_objects_init();

	test_thread_id = k_thread_create(&test_thread_data, test_thread_stack,
					 THREAD_STACK,
					 (k_thread_entry_t) test_thread,
					 0, 0, NULL, TEST_THREAD_PRIORITY,
					 0, K_NO_WAIT);

	TC_PRINT("Test thread started: id = %p\n", test_thread_id);

	helper_thread_id = k_thread_create(&helper_thread_data,
					   helper_thread_stack, THREAD_STACK,
					   (k_thread_entry_t) helper_thread,
					   0, 0, NULL, HELPER_THREAD_PRIORITY,
					   0, K_NO_WAIT);

	TC_PRINT("Helper thread started: id = %p\n", helper_thread_id);

	/* Activate test_thread */
	k_sem_give(&test_thread_sem);

	/* Wait for test_thread to activate us */
	k_sem_take(&task_sem, K_FOREVER);

	/* Wake the test thread */
	k_wakeup(test_thread_id);

	zassert_false(test_failure, "test failure");

	TC_PRINT("Testing kernel k_sleep()\n");
	align_to_tick_boundary();
	start_tick = k_uptime_get_32();
	k_sleep(K_SECONDS(1));
	end_tick = k_uptime_get_32();
	zassert_true(sleep_time_valid(start_tick, end_tick, ONE_SECOND_ALIGNED),
		     "k_sleep() slept for %d ticks, not %d\n",
		     end_tick - start_tick, ONE_SECOND_ALIGNED);

	status = TC_PASS;
}

extern void test_usleep(void);

static void forever_thread_entry(void *p1, void *p2, void *p3)
{
	int32_t ret;

	ret = k_sleep(K_FOREVER);
	zassert_equal(ret, K_TICKS_FOREVER, "unexpected return value");
	k_sem_give(&test_thread_sem);
}

void test_sleep_forever(void)
{
	test_objects_init();

	test_thread_id = k_thread_create(&test_thread_data,
					 test_thread_stack,
					 THREAD_STACK,
					 forever_thread_entry,
					 0, 0, NULL, TEST_THREAD_PRIORITY,
					 K_USER | K_INHERIT_PERMS, K_NO_WAIT);

	/* Allow forever thread to run */
	k_yield();

	k_wakeup(test_thread_id);
	k_sem_take(&test_thread_sem, K_FOREVER);
}

/*test case main entry*/
void test_main(void)
{
	k_thread_access_grant(k_current_get(), &test_thread_sem);

	ztest_test_suite(sleep,
			 ztest_1cpu_unit_test(test_sleep),
			 ztest_1cpu_user_unit_test(test_usleep),
			 ztest_1cpu_unit_test(test_sleep_forever));
	ztest_run_test_suite(sleep);
}
