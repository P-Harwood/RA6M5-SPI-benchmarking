#include "spi.h"

/* Zephyr main renamed SPI_OP_MODE_MASTER to SPI_OP_MODE_CONTROLLER, older trees only have the former */
#ifdef SPI_OP_MODE_CONTROLLER
#define SPI_OP_MODE_CTRL	SPI_OP_MODE_CONTROLLER
#else
#define SPI_OP_MODE_CTRL	SPI_OP_MODE_MASTER
#endif

K_THREAD_STACK_DEFINE(spi_thread_stack, SPI_STACK_SIZE);
static struct k_thread spi_thread_data;
static k_tid_t spi_thread_id;

K_TIMER_DEFINE(spi_tick, NULL, NULL);

static const struct device *const spi_dev = DEVICE_DT_GET(SPI_NODE);

static const struct spi_config spi_cfg = {
	.frequency = SPI_FREQ_HZ,
	.operation = SPI_OP_MODE_CTRL | SPI_WORD_SET(WORD_BITS) | SPI_TRANSFER_MSB,
	/* .peripheral left 0 -> hardware SSL0 */
};

static uint8_t tx_data[TRANSF_LEN] __aligned(4);
static uint8_t rx_data[TRANSF_LEN] __aligned(4);

static const struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
static const struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(rx_data) };
static const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
static const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

static volatile uint32_t spi_overruns;	/* timer periods the thread was too late for */
static volatile uint32_t spi_errors;	/* transfers that returned non-zero */
static volatile uint32_t spi_occurances;

volatile status_logs_t spi_status_log = { .log_name = "SPI_SV_" };

/**
 * @brief True when a tx_buf = rx_buf
 */
static bool spi_validation(uint32_t len)
{
	return memcmp(rx_data, tx_data, len) == 0;
}


void spi_service_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("SPI thread started\n");

	/** Start the timer which ticks based off the passed spi_period_ms parameter */
	k_timer_start(&spi_tick, K_MSEC(SPI_PERIOD_MS), K_MSEC(SPI_PERIOD_MS));

	while (1) {
		/* Get the timer count, calling this function resets it to 0*/
		uint32_t fired = k_timer_status_sync(&spi_tick);

		/* If the counter is above one, then a timer event was missed so add to the overruns*/
		if (fired > 1U) {
			spi_status_log.data.overruns += fired - 1U;
		}
		/* Call the spi transcieve function*/
		if (spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set) != 0) {
			spi_status_log.data.errors++;
		} else if (!spi_validation(TRANSF_LEN)) {
			/* The transfer worked, but the data that came back doesn't match what was sent */
			spi_status_log.data.errors++;
		}
		/* Increment the actions log parameter*/
		spi_status_log.data.actions++;
	}
}




int spi_service_init(void)
{

	if (!device_is_ready(spi_dev)) {
		printk("spi_dev not ready\n");
		return -ENODEV;
	}

	for (uint32_t i = 0U; i < TRANSF_LEN; i++) {
		tx_data[i] = (uint8_t)i;
	}
	/* Start the thread*/
	spi_thread_id = k_thread_create(
		&spi_thread_data,
		spi_thread_stack,
		K_THREAD_STACK_SIZEOF(spi_thread_stack),
		spi_service_entry,
		NULL, NULL, NULL,
		SPI_PRIORITY,
		0,
		K_NO_WAIT);

	k_thread_name_set(spi_thread_id, "spi_thread");

	return 0;
}