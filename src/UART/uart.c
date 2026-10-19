#include "uart.h"

#include <string.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/pinctrl.h>

#include <instances/r_sci_uart.h>
#include <instances/r_dtc.h>

/* The FSP DTC driver is only built when something selects it, here that is CONFIG_SPI_RA_DTC */
BUILD_ASSERT(IS_ENABLED(CONFIG_USE_RA_FSP_DTC), "uart.c needs the FSP DTC driver, set CONFIG_SPI_RA_DTC=y");

/* define the UART stack*/
K_THREAD_STACK_DEFINE(uart_thread_stack, UART_STACK_SIZE);
static struct k_thread uart_thread_data;
static k_tid_t uart_thread_id;


/* define the timer which controls the UART action timer*/
K_TIMER_DEFINE(uart_tick, NULL, NULL);

/* This is the looped message which fills the tx buffer, includes string termination*/
static const char UART_MSG[] = "UART TX";
/* Size of UART msg -1, -1 is so there is no string terminator*/
#define UART_MSG_LEN (sizeof(UART_MSG) - 1U)


/* buffer for rx and tx, set to be the max possible size*/
static uint8_t tx_short[UART_MAX_LEN] __aligned(4);
static uint8_t tx_long[UART_MAX_LEN] __aligned(4);
static uint8_t rx_data[UART_MAX_LEN] __aligned(4);

/* Set by the thread when a write starts, cleared by uart_cb once the last byte has left the pin */
static volatile bool uart_tx_busy;

/* Cleared by the thread when a write starts, set by uart_cb once a whole frame has been received */
static volatile bool uart_rx_done;

volatile status_logs_t uart_status_log = { .log_name = "UART_SV" };

PINCTRL_DT_DEFINE(UART_SCI_NODE);

static const struct pinctrl_dev_config *const uart_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(UART_SCI_NODE);

static sci_uart_instance_ctrl_t uart_ctrl;
static baud_setting_t uart_baud_setting;	/* Filled in by R_SCI_UART_BaudCalculate() */


/**
 * @brief FSP calls this from the UART interrupts.
 *	RX_COMPLETE: the DTC has filled rx_data, so flag it for the thread.
 *	TX_COMPLETE: the last byte of the frame has left the pin, so the thread can write again.
 *	Receive errors: overrun, framing, parity or break seen on the RX pin.
 */
static void uart_cb(uart_callback_args_t *p_args)
{
	if (p_args->event == UART_EVENT_RX_COMPLETE) {
		uart_rx_done = true;
	} else if (p_args->event == UART_EVENT_TX_COMPLETE) {
		uart_tx_busy = false;
	} else if ((p_args->event & UART_RX_ERRORS) != 0U) {
		uart_status_log.data.errors++;
	}
}


/* RX DTC: SCI7 receive register -> rx_data, one CPU interrupt once the whole read has arrived */
static dtc_instance_ctrl_t uart_rx_dtc_ctrl;

static transfer_info_t uart_rx_dtc_info = {
	.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED,
	.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION,
	.transfer_settings_word_b.irq = TRANSFER_IRQ_END,
	.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED,
	.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED,
	.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE,
	.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL,
};

static const dtc_extended_cfg_t uart_rx_dtc_cfg_extend = {
	.activation_source = UART_RXI_IRQ,
};

static const transfer_cfg_t uart_rx_dtc_cfg = {
	.p_info = &uart_rx_dtc_info,
	.p_extend = &uart_rx_dtc_cfg_extend,
};

static const transfer_instance_t uart_rx_dtc = {
	.p_ctrl = &uart_rx_dtc_ctrl,
	.p_cfg = &uart_rx_dtc_cfg,
	.p_api = &g_transfer_on_dtc,
};


/* TX DTC: the frame being sent -> SCI7 transmit register, one CPU interrupt once the whole write has gone */
static dtc_instance_ctrl_t uart_tx_dtc_ctrl;

static transfer_info_t uart_tx_dtc_info = {
	.transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED,
	.transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_SOURCE,
	.transfer_settings_word_b.irq = TRANSFER_IRQ_END,
	.transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED,
	.transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED,
	.transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE,
	.transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL,
};

static const dtc_extended_cfg_t uart_tx_dtc_cfg_extend = {
	.activation_source = UART_TXI_IRQ,
};

static const transfer_cfg_t uart_tx_dtc_cfg = {
	.p_info = &uart_tx_dtc_info,
	.p_extend = &uart_tx_dtc_cfg_extend,
};

static const transfer_instance_t uart_tx_dtc = {
	.p_ctrl = &uart_tx_dtc_ctrl,
	.p_cfg = &uart_tx_dtc_cfg,
	.p_api = &g_transfer_on_dtc,
};


/* SCI7 UART: 8 data bits, no parity, 1 stop bit, no flow control */
static const sci_uart_extended_cfg_t uart_cfg_extend = {
	.clock = SCI_UART_CLOCK_INT,
	.rx_edge_start = SCI_UART_START_BIT_FALLING_EDGE,
	.noise_cancel = SCI_UART_NOISE_CANCELLATION_DISABLE,
	.p_baud_setting = &uart_baud_setting,
	.rx_fifo_trigger = SCI_UART_RX_FIFO_TRIGGER_1,	/* FSP ignores this while RX uses the DTC */
	.flow_control = SCI_UART_FLOW_CONTROL_RTS,	/* No RTS pin is set up, so no flow control */
	.flow_control_pin = (bsp_io_port_pin_t)UINT16_MAX,
};

static const uart_cfg_t uart_cfg = {
	.channel = UART_SCI_CHANNEL,
	.data_bits = UART_DATA_BITS_8,
	.parity = UART_PARITY_OFF,
	.stop_bits = UART_STOP_BITS_1,
	.rxi_ipl = UART_RXI_PRIORITY,
	.rxi_irq = UART_RXI_IRQ,
	.txi_ipl = UART_TXI_PRIORITY,
	.txi_irq = UART_TXI_IRQ,
	.tei_ipl = UART_TEI_PRIORITY,
	.tei_irq = UART_TEI_IRQ,
	.eri_ipl = UART_ERI_PRIORITY,
	.eri_irq = UART_ERI_IRQ,
	.p_transfer_rx = &uart_rx_dtc,
	.p_transfer_tx = &uart_tx_dtc,
	.p_callback = uart_cb,
	.p_context = NULL,
	.p_extend = &uart_cfg_extend,
};

/** @brief return pointer to the frame to transmit */
static const uint8_t *uart_next_frame(const uint8_t *frame)
{
	return (frame == tx_short) ? tx_long : tx_short;
}

/**
 * @brief True when a whole frame has been received since the last write, and it matches the frame that was sent.
 */
static bool uart_rx_frame_ok(const uint8_t *frame)
{
	return uart_rx_done && (memcmp(rx_data, frame, UART_MAX_LEN) == 0);
}

/**
 * @brief Throw away what the current read holds and then start the uart listening
 */
static bool uart_prepare_rx(void)
{
	/* Terminate uart and discard current buffer*/
	(void)R_SCI_UART_Abort(&uart_ctrl, UART_DIR_RX);
	uart_rx_done = false;

	/* UART starts listening to rx (Async)*/
	if (R_SCI_UART_Read(&uart_ctrl, rx_data, UART_MAX_LEN) != FSP_SUCCESS) {
		uart_status_log.data.errors++;
		return false;
	}

	return true;
}

/**
 * @brief Start sending a frame.
 * @retval bool, true if the write started.
 */
static bool uart_tx_frame(const uint8_t *frame)
{
	uart_tx_busy = true;

	if (R_SCI_UART_Write(&uart_ctrl, frame, UART_MAX_LEN) != FSP_SUCCESS) {
		uart_tx_busy = false;
		uart_status_log.data.errors++;
		return false;
	}

	return true;
}


void uart_service_entry(void *a, void *b, void *c)
{
	const uint8_t *frame = tx_long;
	bool frame_sent = false;	/* A frame went out last period, so it should be back on RX by now */

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

		if (uart_tx_busy) {
			/* The last frame is still going out, leave it to finish */
			uart_status_log.data.errors++;
		} else {
			/* The frame sent last period should have come back on RX, check it before sending the next */
			if (frame_sent && !uart_rx_frame_ok(frame)) {
				uart_status_log.data.errors++;
			}

			/* Change which frame to transmit*/
			frame = uart_next_frame(frame);

			/* UART listens async, interrupt when new message*/
			frame_sent = uart_prepare_rx();

			/* Transmit the uart buffer*/
			if (frame_sent) {
				frame_sent = uart_tx_frame(frame);
			}
		}

		/* increment actions log*/
		uart_status_log.data.actions++;
	}
}


/**
 * @brief Open SCI7 as a UART using FSP calls, with the DTC moving the bytes both ways.
 */
static int start_UART_FSP(void)
{
	fsp_err_t err;
	int ret;

	ret = pinctrl_apply_state(uart_pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		printk("uart pinctrl: %d\n", ret);
		return ret;
	}

	/* Work out the SCI7 baud rate register values for UART_BAUD_HZ, allowing up to 5% error */
	err = R_SCI_UART_BaudCalculate(UART_BAUD_HZ, true, 5000U, &uart_baud_setting);
	if (err != FSP_SUCCESS) {
		printk("R_SCI_UART_BaudCalculate: %d\n", err);
		return -EIO;
	}

	/* Route each SCI7 event to its interrupt line */
	R_ICU->IELSR[UART_RXI_IRQ] = ELC_EVENT_SCI7_RXI;
	R_ICU->IELSR[UART_TXI_IRQ] = ELC_EVENT_SCI7_TXI;
	R_ICU->IELSR[UART_TEI_IRQ] = ELC_EVENT_SCI7_TEI;
	R_ICU->IELSR[UART_ERI_IRQ] = ELC_EVENT_SCI7_ERI;

	/* Hook FSP's handler to each line, R_SCI_UART_Open() enables them */
	IRQ_CONNECT(UART_RXI_IRQ, UART_RXI_PRIORITY, sci_uart_rxi_isr, NULL, 0);
	IRQ_CONNECT(UART_TXI_IRQ, UART_TXI_PRIORITY, sci_uart_txi_isr, NULL, 0);
	IRQ_CONNECT(UART_TEI_IRQ, UART_TEI_PRIORITY, sci_uart_tei_isr, NULL, 0);
	IRQ_CONNECT(UART_ERI_IRQ, UART_ERI_PRIORITY, sci_uart_eri_isr, NULL, 0);

	/* Also opens the two DTC instances */
	err = R_SCI_UART_Open(&uart_ctrl, &uart_cfg);
	if (err != FSP_SUCCESS) {
		printk("R_SCI_UART_Open: %d\n", err);
		return -EIO;
	}

	return 0;
}


int uart_service_init(void)
{
	int ret;

	/* Fills the Uart buffer with contents of  UART_MSG repeated*/
	for (uint32_t i = 0U; i < UART_MAX_LEN; i += UART_MSG_LEN) {
		uint32_t n = MIN(UART_MSG_LEN, UART_MAX_LEN - i);

		memcpy(&tx_long[i], UART_MSG, n);
	}

	memcpy(tx_short, tx_long, UART_MIN_LEN);

	ret = start_UART_FSP();
	if (ret != 0) {
		return ret;
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