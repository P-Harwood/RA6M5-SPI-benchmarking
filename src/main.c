#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

#include "UART/uart.h"
#include "SPI/spi.h"
#include "coremark.h"
#include "PWM/pwm.h"
#include "ADC/adc.h"
#include "QSPI/qspi.h"
#include "logs/log.h"


/* --- Editing these not advised for demo --- */
#define SPI_BASE	DT_REG_ADDR(SPI_NODE)
#define PCLKA_SPEED	100000000U
#define CORE_CLOCK_HZ	200000000U
// #define S_Cache_enable // If this is enabled, read # Note in readme.md. The application will break otherwise. It will cache the QSPI Registers.
#define COREMARK_STACK_SIZE	4096U
/* --- Editing these not advised for demo --- */


static volatile uint32_t coremark_execs;	// How many CoreMark executions have completed
static volatile uint32_t coremark_last_ticks;	// CoreMark's own timed section of the last execution, get_time()
static uint32_t base_ticks;			// Timed section of the baseline execution when nothing else is happening


static K_SEM_DEFINE(exec_go, 0, 1);	// main -> CoreMark thread: run one execution
static K_SEM_DEFINE(exec_done, 0, 1);	// CoreMark thread -> main: execution finished, result ready

volatile status_logs_t *thread_logs[thread_log_count]; /* Array of thread error/overrun/runs data */
volatile status_logs_t snapshot_logs[thread_log_count];/* Array to copy and snapshot thread_logs*/


/**
 * @brief Create the CoreMark thread which will run when the CPU is not occupied with transmitting data
 */
static void coremark_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	while (1) {
		k_sem_take(&exec_go, K_FOREVER);	/* Wait for main to release the next execution */

		(void) coremark_main();			/* Standard CoreMark run */

		coremark_last_ticks = get_time();
		coremark_execs++;
		k_sem_give(&exec_done);
	}
}

K_THREAD_DEFINE(coremark_tid, COREMARK_STACK_SIZE, coremark_thread_fn, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);


void run_coremark(void){
	k_sem_give(&exec_go);			/* Give semaphore to CoreMark so it can run */
	k_sem_take(&exec_done, K_FOREVER);	/* Wait until CoreMark gives the semaphore back*/
}


/**
 * @brief CoreMark iterations per second of one execution, from CoreMark's own timer
 * @retval float iterations/s
 */
static float coremark_score(uint32_t ticks)
{
	return (ticks == 0U) ? 0.0f : ((float)ITERATIONS / (float)time_in_secs(ticks));
}


/**
 * @brief One measurement window = one complete CoreMark execution with the SPI, UART, ADC and
 * QSPI log services running on their own periods for the whole duration.
 */
static void run_window(uint32_t window_nr)
{

	/* Reset the counters */
	clear_logs();
	/* Reset the counters */
	
	/* Run the benchmarker */
	run_coremark();
	/* Run the benchmarker */

	/* Snap shot overruns and errors */
	uint32_t exec_ticks    = coremark_last_ticks;
	update_snapshots();


	/* There is going to be integer division done on the result of the coremark */
	uint32_t coremark_achieved = 10000U;

	if (base_ticks > 0U && exec_ticks > 0U) { /* Divide by 0 guard */
		float free_ratio = (float)base_ticks / (float)exec_ticks;	/* Baseline ticks divided by the ticks just gained */
		float scaled = free_ratio * 10000.0f + 0.5f;			/* scale free_ratio to a percentage. The 0.5f results in rounding to nearest 0.01. */

		coremark_achieved = (scaled > 10000.0f) ? 10000U : (uint32_t)scaled;	/* Max the scaled to 100% */
	}

	uint32_t cpu_usage = 10000U - coremark_achieved; 

	/* Log the coremark results*/
	printk("\n\n=== === Coremark Results === ===\n"
			"Window %u:\n"
	       	"CoreMark achieved (CPU free): %3u.%02u%% / 100%%  (%u ticks)\n"
	       	"CPU usage: %3u.%02u%%\n"
	       	"CoreMark score: %.2f iterations/s, %.3f CoreMark/MHz\n"
	       	"Baseline ticks (count): %u\n\n\n",

	       	window_nr,
	       	coremark_achieved / 100U, coremark_achieved % 100U, exec_ticks,
	       	cpu_usage / 100U, cpu_usage % 100U,
	       	(double)coremark_score(exec_ticks),
	       	(double)(coremark_score(exec_ticks) / ((float)CORE_CLOCK_HZ / 1000000.0f)),
	       	base_ticks);
	
	/* Log the QSPI snapshot objects */
	dump_log(SnapShot);
	
	/* Clear the log objects which hold data such as errors*/
	clear_logs(); 

	/* wait 500ms before running the next test. */
	k_msleep(500);
}


/**
 * @brief Entry messages which announce the build configurations
 * @retval void
 */
void build_config_print(void)
{
	printk("\nRA6M5 Coremark benchmarking\n\n"); /* Application title*/

	/* Configuration logs */
	printk("=== === Build configuration === ===\n"
	       "CONFIG_SPI_RA_DTC=%s\n"
	       "CONFIG_SPI_INTERRUPT=%s\n"
	       "CONFIG_UART_ASYNC_API=%s\n"
	       "CONFIG_TRACING_USER=%s\n"
	       "CONFIG_TICKLESS_KERNEL=%s\n\n",
	       IS_ENABLED(CONFIG_SPI_RA_DTC) ? "y" : "n",
	       IS_ENABLED(CONFIG_SPI_INTERRUPT) ? "y" : "n",
	       IS_ENABLED(CONFIG_UART_ASYNC_API) ? "y" : "n",
	       IS_ENABLED(CONFIG_TRACING_USER) ? "y" : "n",
	       IS_ENABLED(CONFIG_TICKLESS_KERNEL) ? "y" : "n");

	printk("=== === DTC Enabled === ===\n"
		"Rx=%s\n"
		"Tx=%s\n\n",
	       DT_PROP(SPI_NODE, rx_dtc) ? " True " : " False ",
	       DT_PROP(SPI_NODE, tx_dtc) ? " True " : " False ");

	printk("\n=== === SPI Config === ===\n"
		"SPI config *requested* with zephyr API: %u Hz,\n"
		"Bit transmission size: %u,\n"
		"Total packet size in bytes: %u,\n"
		"SPI period: %u ms\n\n",
		SPI_FREQ_HZ, WORD_BITS, TRANSF_LEN, SPI_PERIOD_MS);

	printk("\n=== === UART Config === ===\n"
		"UART config: %u baud,\n"
		"Total packet size in bytes: %u,\n"
		"UART period: %u ms\n\n",
		UART_BAUD_HZ, UART_TX_LEN, UART_PERIOD_MS);

	printk("\n=== === QSPI Config === ===\n"
		"QSPI Clock = PCLKA/%u = %u Hz\n"
		"%uB blocks of %uB records\n" 
		"%u block RAM queue\n"
		"%u KB flash ring at 0x%06x\n\n",
	       QSPI_CLK_DIV, PCLKA_SPEED / QSPI_CLK_DIV,
	       QSPI_LOG_BLOCK_SIZE, (uint32_t)sizeof(struct adc_record), QSPI_LOG_QUEUE_BLOCKS,
	       QSPI_LOG_SIZE / 1024U, QSPI_LOG_START);

	printk(" System tick rate: %d Hz\n\n", CONFIG_SYS_CLOCK_TICKS_PER_SEC);

	return;
}



int main(void)
{
	int ret;

	/* Populate the thread logging array with pointers to thread logs for each thread*/
	thread_logs[0] = &spi_status_log; 
	thread_logs[1] = &adc_status_log; 
	thread_logs[2] = &uart_status_log; 
	thread_logs[3] = &qspi_status_log; 

	/* Print the build configurations*/
	(void) build_config_print();

	/* Compile time S_Cache include option, requires MPU protection of QSPI registers*/
#ifdef S_Cache_enable
	R_CACHE->SCACTL = 1U;
	__DSB();
	__ISB();
#endif

	/* Start the coremark baselining*/
	printk("Running baseline CoreMark, peripherals are not running\n\n");
	(void)run_coremark();

	/* Set the baseline coremark score from the coremark test which has just completed*/
	base_ticks = coremark_last_ticks;

	/* Print the results of the baseline coremark test*/
	printk("\nBaseline CoreMark: %.2f iterations/s,\n%u ticks per execution (= 100%% CoreMark achieved)\n\n",
	       (double)coremark_score(base_ticks), base_ticks);


	/* These calls start each of the threads*/
	ret = spi_service_init();
	if (ret != 0) {
		printk("spi_service_init: %d\n", ret);
		return ret;
	}

	ret = uart_service_init();
	if (ret != 0) {
		printk("uart_service_init: %d\n", ret);
		return ret;
	}

	ret = qspi_service_init();
	if (ret != 0) {
		printk("qspi_service_init: %d\n", ret);
		return ret;
	}

	ret = adc_service_init();
	if (ret != 0) {
		printk("adc_service_init: %d\n", ret);
		return ret;
	}

	ret = pwm_service_init();
	if (ret != 0) {
		printk("pwm_service_init: %d\n", ret);
		return ret;
	}
	printk("Services started\n\n");

	/* Uint32 sized for loop to continously run the coremark test*/
	for (uint32_t i = 0; ; i++) {
		run_window(i);
	}

	return 0;
}