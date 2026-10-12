#include "uart.h"

#include <string.h>


K_THREAD_STACK_DEFINE(uart_thread_stack, UART_STACK_SIZE);
static struct k_thread uart_thread_data;
static k_tid_t uart_thread_id;

K_TIMER_DEFINE(uart_tick, NULL, NULL);
static K_SEM_DEFINE(uart_tx_done, 0, 1);


static const char UART_MSG[] = "UART TX";
#define UART_MSG_LEN (sizeof(UART_MSG) - 1U)




static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

static const struct uart_config uart_cfg = {
	.baudrate = UART_BAUD_HZ,
	.parity = UART_CFG_PARITY_NONE,
	.stop_bits = UART_CFG_STOP_BITS_1,
	.data_bits = UART_CFG_DATA_BITS_8,
	.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

static uint8_t tx_data[UART_TX_LEN] __aligned(4);




static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(evt);
	ARG_UNUSED(user_data);
}

volatile status_logs_t uart_status_log = { .log_name = "UART_SV" };

void uart_service_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("UART thread started\n");

	/* Start the timer which increments on the passed period time parameter*/
	k_timer_start(&uart_tick, K_MSEC(UART_PERIOD_MS), K_MSEC(UART_PERIOD_MS));

	while (1) {
		/* Get the timer count*/
		uint32_t fired = k_timer_status_sync(&uart_tick);

		/* Count above 1 means an over run happened and a timer event missed, add to overruns*/
		if (fired > 1U) {
			uart_status_log.data.overruns += fired - 1U;
		}
		/* Transmit the uart buffer*/
		if (uart_tx(uart_dev, tx_data, sizeof(tx_data), SYS_FOREVER_US) != 0) {
			uart_status_log.data.errors++;
		}
		/* increment actions log*/
		uart_status_log.data.actions++;
	}
}

int uart_service_init(void)
{
	int ret;
	

	if (!device_is_ready(uart_dev)) {
		printk("uart_dev not ready\n");
		return -ENODEV;
	}

	ret = uart_configure(uart_dev, &uart_cfg);
	if (ret != 0) {
		printk("uart_configure: %d\n", ret);
		return ret;
	}

	ret = uart_callback_set(uart_dev, uart_cb, NULL);
	if (ret != 0) {
		printk("uart_callback_set: %d\n", ret);
		return ret;
	}

	/* Fills the Uart buffer with contents of  UART_MSG repeated*/
	for (uint32_t i = 0U; i < UART_TX_LEN; i += UART_MSG_LEN) {
		uint32_t n = MIN(UART_MSG_LEN, UART_TX_LEN - i);

		memcpy(&tx_data[i], UART_MSG, n);
	}

	uart_thread_id = k_thread_create(
		&uart_thread_data,
		uart_thread_stack,
		K_THREAD_STACK_SIZEOF(uart_thread_stack),
		uart_service_entry,
		NULL, NULL, NULL,
		UART_PRIORITY,
		0,
		K_NO_WAIT);

	k_thread_name_set(uart_thread_id, "uart_thread");

	return 0;
}
