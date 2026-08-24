#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <string.h>








#define SPI_NODE	DT_NODELABEL(spi0)
#define SPI_BASE	DT_REG_ADDR(SPI_NODE)
#define PCLKA_SPEED 100000000


#define SPI_FREQ_HZ	4000000U
#define WORD_BITS	8U

#define MAGIC_LEN 20U
#define MAGIC_TEST_CNT 3

#define TRANSF_LEN	4096U
#define WINDOW_TIME_LEN_MS	2000U
#define VERIFY_EVERY	64U

/* Data watch point registers  */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYC_CNT (*(volatile uint32_t *)0xE0001004UL)



static const struct device *const spi_dev = DEVICE_DT_GET(SPI_NODE);

static const struct spi_config spi_cfg = {
	.frequency = SPI_FREQ_HZ,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(WORD_BITS) | SPI_TRANSFER_MSB,
	.slave = 0,
};



#ifdef CONFIG_TRACING_USER

typedef struct cycle_counts_t {
	volatile uint32_t isr;
	volatile uint32_t scheduler;
	volatile uint32_t busy;
	volatile uint32_t main;
	volatile uint32_t other;
	volatile uint32_t total_cycles;
}cycle_counts_t;

static volatile cycle_counts_t thread_counts;

static volatile uint32_t slice_start_time;    // Tracks when the most recent thread was entered
static volatile uint32_t isr_start_time;      // Tracks when the shallow most interrupt started
static volatile uint32_t switch_out_time;     // Tracks when last thread switch occured
static volatile uint32_t isr_nesting_depth;		// Keeps track of what level of interrupt the application is in

static volatile uint32_t *current_thread_cntr_ptr = &(thread_counts.other); // pointer to which thread counter is the current 
static k_tid_t main_thread_id; /* Captured in main(), busy_tid comes from K_THREAD_DEFINE above */

#endif



static volatile uint32_t busy_count; // How many ticks have happened in busy thread
static uint32_t base_counts; // Base counts per WINDOW_TIME_LEN_MS ms

static uint8_t tx_data[TRANSF_LEN];
static uint8_t rx_data[TRANSF_LEN];

static char magic_word_tx[MAGIC_LEN];
static char magic_word_rx[MAGIC_LEN];

static const struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
static const struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(rx_data) };
static const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
static const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };




/** @brief This test runs a loopback which transmits the message to itself and reads it
 * 
 */
static void readback_magic_message(void)
{
	const struct spi_buf magic_tx_buffer = { .buf = magic_word_tx, .len = MAGIC_LEN };
	const struct spi_buf magic_rx_buffer = { .buf = magic_word_rx, .len = MAGIC_LEN };
	const struct spi_buf_set magic_tx_buf_set = { .buffers = &magic_tx_buffer, .count = 1 };
	const struct spi_buf_set magic_rx_buf_set = { .buffers = &magic_rx_buffer, .count = 1 };

	printk("Readback magic message example:\n");

	for (int loop = 1; loop <= MAGIC_TEST_CNT; loop++) {

		memset(magic_word_tx, 0, MAGIC_LEN);
		memset(magic_word_rx, '!', MAGIC_LEN); /* Wiping the rx bit, any char will work*/


		/* Place the new magic string into magic_word_tx*/
		snprintk(magic_word_tx, MAGIC_LEN, "RA6M5 magic #%d", loop);

		/* Make sure that the magic word has been cleared, it does not equal the new magic word tx */
		if(memcmp(magic_word_rx, magic_word_tx, MAGIC_LEN) == 0){
			printk("\n\nError: Magic word rx not cleared. Test is invalid\n\n");
		}

		int result = spi_transceive(spi_dev, &spi_cfg, &magic_tx_buf_set, &magic_rx_buf_set);

		magic_word_rx[MAGIC_LEN - 1] = '\0';

		bool magic_message_healthy = (result == 0 && memcmp(magic_word_tx, magic_word_rx, MAGIC_LEN) == 0);

		printk(" Loop %d:	Magic Tx string: \"%s\" Magic Rx string: \"%s\" [%s]\n",
		       loop, magic_word_tx, magic_word_rx, (magic_message_healthy ? " MATCH " : "mismatch (ERROR)"));
	}
	printk("\n");
	return;
}






/**
 * @brief Create the busy thread which will run when the CPU is not occupied with transmitting data
 * 
 */

static void busy_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		busy_count++;
	}
}

K_THREAD_DEFINE(busy_tid, 512, busy_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/** End of busy thread declaration */





/**
 * @brief CPU cycle profiler, active only in builds with CONFIG_TRACING_USER
 * 
 * Zephyr calls empty weak hooks on every ISR entry/exit and thread switch.
 * 25 cycle count over head per ISR
 */
#ifdef CONFIG_TRACING_USER


/**
 * @brief over ride of weak system function
 * sets current_thread_cntr_ptr to point to current thread's counter
 * Tracks scheduler time
 * sets start point for the threads counter
 * This occurs on thread switch entry
 */
void sys_trace_thread_switched_in_user(void)
{
	k_tid_t current_thread_id = k_current_get(); // Get the thread id for the the current thread


	thread_counts.scheduler += DWT_CYC_CNT - switch_out_time; // Add counts taken by the application to reach this point from when the last thread exited to give way

	// Get the current thread's ticks from the current thread's id and set the current thread counter to a pointer to the struct field
	current_thread_cntr_ptr = &(thread_counts.other);
	
	if(current_thread_id == busy_tid){
		current_thread_cntr_ptr = &(thread_counts.busy);
	}
	
	if(current_thread_id == main_thread_id){
		current_thread_cntr_ptr = &(thread_counts.main);
	}

	slice_start_time = DWT_CYC_CNT; /** Set the slice start time to the current system time */
}

/**
 * @brief over ride of weak system function, overwrites thread exit to keep record thread exit time and track time in thread
 */
void sys_trace_thread_switched_out_user(void)
{
	uint32_t time_spent_in_thread = DWT_CYC_CNT - slice_start_time; 
	*current_thread_cntr_ptr += time_spent_in_thread; 

	switch_out_time = DWT_CYC_CNT; // Update to the time which thread is exited and application enters scheduler
}

/**
 * @brief over ride of weak system function, overwrites isr entry to track isr start time and updates current thread
 * counter ( no distinquishing between different isrs )
 */
void sys_trace_isr_enter_user(void)
{
	// nesting depth check ensures that if this is an interrupt, interrupting another interrupt, then start time isnt reset
	if (isr_nesting_depth++ == 0) {
		uint32_t time_spent_in_thread = DWT_CYC_CNT - slice_start_time;
		*current_thread_cntr_ptr += time_spent_in_thread;/* Pause the thread counter */
		isr_start_time = DWT_CYC_CNT;
	}
}
/**
 * @brief over ride of weak system function, overwrites isr exit to track isr depth and exit time
 */
void sys_trace_isr_exit_user(void)
{
	// nesting depth check observes if this is an interrupt returning to an interrupt of if it will be returning to an app thread
	if (--isr_nesting_depth == 0) {
		uint32_t time_spent_in_isr = DWT_CYC_CNT - isr_start_time;
		thread_counts.isr += time_spent_in_isr;

		slice_start_time = DWT_CYC_CNT; /* Resume the previous thread counter */
	}
}

/**
 * @brief create a copy of the profiler thread counters
 * @retval void
 */
static void profiler_take_snapshot(struct cycle_counts_t *snapshot)
{
	unsigned int key = irq_lock();

	snapshot->isr = thread_counts.isr;
	snapshot->scheduler = thread_counts.scheduler;
	snapshot->busy = thread_counts.busy;
	snapshot->main = thread_counts.main;
	snapshot->other = thread_counts.other;

	snapshot->total_cycles = DWT_CYC_CNT;

	irq_unlock(key);
	return;
}



/**
 * @brief Scale one window against the whole window, in tenths of a percent
 */
static float usage(uint32_t part, uint32_t whole)
{
	return (float)(((uint64_t)part * 1000U) / whole) / 10.0f;
}
/**
 * @brief Print how the measuring window's CPU cycles is divided between the threads.
 * 
 * @retval void
 */
static void profiler_print_report(const struct cycle_counts_t *window_start,
				  const struct cycle_counts_t *window_end)
{
	uint32_t total = window_end->total_cycles - window_start->total_cycles;

	uint32_t isr = window_end->isr - window_start->isr;
	uint32_t sched = window_end->scheduler - window_start->scheduler;
	uint32_t busy = window_end->busy - window_start->busy;
	uint32_t main_t = window_end->main - window_start->main;
	uint32_t other = window_end->other - window_start->other;

	printk(	"\nThread usages:\n"
			"	ISR 					%5.1f%%,\n"
			"	Scheduler 				%5.1f%%,\n"
			"	Main 					%5.1f%%,\n"
			"	Other 					%5.1f%%,\n"
			"	Application (Free) 			%5.1f%%\n\n\n\n",
	       (double)usage(isr, total),
	       (double)usage(sched, total),
	       (double)usage(main_t, total),
	       (double)usage(other, total),
	       (double)usage(busy, total));
	return;
}

#endif /* CONFIG_TRACING_USER */

/** End of profiler declaration */




static void run_window(uint32_t window_nr)
{
	uint32_t start_time = k_uptime_get_32();

	uint32_t window_start_busy_Count = busy_count;
	uint32_t measure_occur_cnt = 0;

	uint32_t transfer_error = 0;

	uint32_t measure_time_taken = 0;

#ifdef CONFIG_TRACING_USER
	struct cycle_counts_t window_start_profile;
	struct cycle_counts_t window_end_profile;

	profiler_take_snapshot(&window_start_profile);
#endif

	while (measure_time_taken < WINDOW_TIME_LEN_MS) {

		bool verify_window = (measure_occur_cnt % VERIFY_EVERY) == 0;

		if (verify_window) {
			tx_data[0] = (uint8_t)measure_occur_cnt;
			tx_data[1] = (uint8_t)(measure_occur_cnt >> 8);
			tx_data[2] = (uint8_t)window_nr;
			memset(rx_data, 0xEE, 4);
		}

		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);

		bool data_error = (verify_window && (memcmp(tx_data,rx_data,TRANSF_LEN)!=0));

		if (ret !=0 || data_error) {
			transfer_error++;
		}

		measure_occur_cnt++;
		measure_time_taken = k_uptime_get_32() - start_time;
	}



	uint32_t new_low_priority_counts = busy_count - window_start_busy_Count;
	uint64_t best_case_counts = (uint64_t)base_counts * measure_time_taken / WINDOW_TIME_LEN_MS;

	uint32_t increment_achieved = 1000U;

	if (best_case_counts > 0) {
		uint64_t free_ratio = ((uint64_t)new_low_priority_counts * 1000U) / best_case_counts;

		increment_achieved = (free_ratio > 1000U) ? 1000U : (uint32_t)free_ratio;
	}

	uint64_t bytes_sent = (uint64_t) measure_occur_cnt * TRANSF_LEN;
	uint32_t data_rate = (uint32_t)( bytes_sent / measure_time_taken); // Elapsed time is in ms, so this is b/ms or kb/s

	/* Increment percenteage is good for gauging overhead added by tracing, also sram usage */
	printk("Incrementations achieved in window (count++):		%3u.%u%% / 100%%\n"
	       "%2u transfers per window\n"
	       "Transfer speed: %2u kB/s\n",
	       increment_achieved / 10, increment_achieved % 10,
	       measure_occur_cnt, data_rate);
	
		  if(transfer_error>0){
			printk("Errors: %u \n", transfer_error);
		  }

#ifdef CONFIG_TRACING_USER
	profiler_take_snapshot(&window_end_profile);
	(void) profiler_print_report(&window_start_profile, &window_end_profile);
#endif

	printk("\n");
}

/**
 * @brief print the real SPI clock speed, what is requested by zephyr may not be the actual clock speed
 * @retval void
 */
static void print_real_clock_speed(void)
{
	volatile uint8_t  *spbr_address = (uint8_t  *)(SPI_BASE + 0x0A);
	volatile uint16_t *spcmd0_address = (uint16_t *)(SPI_BASE + 0x10);

	uint32_t spbr = *spbr_address;
	uint32_t brdv = (*spcmd0_address >> 2) & 3U;

	uint32_t total_divider = (2U * (spbr + 1U)) << brdv;
	uint32_t total_speed = PCLKA_SPEED / total_divider;

	printk("\n\n Actual SPI bitrate: %u Hz, SPBR %u, BRDV %u\n\n", total_speed, spbr, brdv);
	return;
}

/**
 * @brief Entry messages which announce the build configurations
 * @retval void
 */
void welcome_messages(void){

	printk ("\nRA6M5 r_spi benchmarking\n\n");

	printk ("Build configuration: "
		"\n CONFIG_SPI_RA_DTC=%s, "
		"\n CONFIG_SPI_INTERRUPT=%s "
		"\n Cycle profiler=%s "
		"\n\n",
			IS_ENABLED(CONFIG_SPI_RA_DTC) ? "y" : "n",
			IS_ENABLED(CONFIG_SPI_INTERRUPT) ? "y" : "n",
			IS_ENABLED(CONFIG_TRACING_USER) ? "on" : "off");

	printk ("DTC Enabled:\
		\n Rx=%s \
		\n Tx=%s \
		\n\n",
	       DT_PROP(SPI_NODE, rx_dtc) ? " True " : " False ", DT_PROP(SPI_NODE, tx_dtc)? " True " : " False ");


	printk (" SPI config *requested* with zephyr API: %u Hz, \
		\n Bit transmission size: %u,\
		\n Total Packet size in Bytes: %u,\
		\n Measurement window: %ums\
		\n\n",
	       SPI_FREQ_HZ, WORD_BITS, TRANSF_LEN, WINDOW_TIME_LEN_MS);

	return;
}




int main(void)
{

	if (!device_is_ready(spi_dev)) {
		printk("SPI device %s is not ready\n", spi_dev->name);
		return -ENODEV;
	}

	/* Start the DWT cycle counter, the profiler timebase */
	DEMCR |= (1UL << 24);
	DWT_CYC_CNT = 0;
	DWT_CTRL |= 1UL;

#ifdef CONFIG_TRACING_USER
	main_thread_id = k_current_get(); /* Profiler needs this to know main thread */
#endif

	(void) welcome_messages();
	(void) readback_magic_message();
	(void) print_real_clock_speed();


	for (uint32_t i = 0; i < TRANSF_LEN; i++) {
		tx_data[i] = (uint8_t)(i ^ (i >> 8));
	}

	int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
	bool rx_match_tx = memcmp(tx_data, rx_data, TRANSF_LEN) == 0;

	if (ret == 0 && rx_match_tx) {
		printk("Loopback packet test: OK\n");
	} else {
		printk("Loopback packet test: FAILED\n");
	}


	k_sleep(K_MSEC(100)); // Cooldown period before taking a sample of CPU usage

	uint32_t initial_busy_count = busy_count; // Take a snapshot of busy count

	k_sleep(K_MSEC(WINDOW_TIME_LEN_MS)); // Sleep for the standard window length

	base_counts = busy_count - initial_busy_count; // Calculate how many counts have happened in standard window

	printk("\nMax achievable increments in the set window: %u ticks / %u ms window (= 100%% Increment achieved)\n\n",
	       base_counts, WINDOW_TIME_LEN_MS);


	for (uint32_t i = 0; ; i++) {
		run_window(i);
	}
	return 0;
}