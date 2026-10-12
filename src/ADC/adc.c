#include "adc.h"

#include <string.h>
#include <zephyr/drivers/pinctrl.h>

#include <instances/r_adc.h>

#include "../QSPI/qspi.h"

#define ADC_NODE		DT_NODELABEL(adc0)
#define ADC_UNIT		0

#define ADC_SCAN_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6))

K_TIMER_DEFINE(adc_tick, NULL, NULL);

/* Name of the ADC thread logs*/
volatile status_logs_t adc_status_log = { .log_name = "ADC_SV_" };

/* Build time aasserts */
BUILD_ASSERT(sizeof(struct adc_record) == 16U,
	     "adc_record must stay 16 bytes");

BUILD_ASSERT((QSPI_LOG_BLOCK_SIZE % sizeof(struct adc_record)) == 0U,
	     "a QSPI log block must hold whole adc_records");
/* Build time aasserts */

PINCTRL_DT_DEFINE(ADC_NODE);

static const struct pinctrl_dev_config *const adc_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(ADC_NODE);


/* ADC gets 7 channels*/
static const adc_channel_t adc_channels[ADC_CHANNEL_COUNT] = { 
	ADC_CHANNEL_0,
	ADC_CHANNEL_1,
	ADC_CHANNEL_2,
	ADC_CHANNEL_3,
	ADC_CHANNEL_4,
	ADC_CHANNEL_5,
	ADC_CHANNEL_6,
};

static adc_instance_ctrl_t adc_ctrl;

static const adc_extended_cfg_t adc_cfg_extend = {
	.add_average_count = ADC_ADD_OFF,
	.clearing = ADC_CLEAR_AFTER_READ_ON,
	.trigger = ADC_START_SOURCE_DISABLED,
	.trigger_group_b = ADC_START_SOURCE_DISABLED,
	.double_trigger_mode = ADC_DOUBLE_TRIGGER_DISABLED,
	.adc_vref_control = ADC_VREF_CONTROL_AVCC0_AVSS0,
	.enable_adbuf = 0,
	.window_a_irq = FSP_INVALID_VECTOR,
	.window_b_irq = FSP_INVALID_VECTOR,
	.window_a_ipl = BSP_IRQ_DISABLED,
	.window_b_ipl = BSP_IRQ_DISABLED,
};

static const adc_cfg_t adc_cfg = {
	.unit = ADC_UNIT,
	.mode = ADC_MODE_CONTINUOUS_SCAN,
	.resolution = ADC_RESOLUTION_12_BIT,
	.alignment = ADC_ALIGNMENT_RIGHT,
	.trigger = ADC_TRIGGER_SOFTWARE,
	.p_callback = NULL,
	.p_context = NULL,
	.p_extend = &adc_cfg_extend,
	.scan_end_irq = FSP_INVALID_VECTOR,
	.scan_end_ipl = BSP_IRQ_DISABLED,
	.scan_end_b_irq = FSP_INVALID_VECTOR,
	.scan_end_b_ipl = BSP_IRQ_DISABLED,
};

static const adc_channel_cfg_t adc_channel_cfg = {
	.scan_mask = ADC_SCAN_MASK,
	.scan_mask_group_b = 0,
	.add_mask = 0,
	.p_window_cfg = NULL,
	.priority_group_a = ADC_GROUP_A_PRIORITY_OFF,
	.sample_hold_mask = 0,
	.sample_hold_states = 24,
};






K_THREAD_STACK_DEFINE(adc_thread_stack, ADC_STACK_SIZE); // Define the stack for the adc thread
static struct k_thread adc_thread_data; // Data for the thread
static k_tid_t adc_thread_id; // Thread id
static volatile uint32_t adc_samples;

K_MUTEX_DEFINE(adc_latest_lock); 
static uint16_t adc_latest[ADC_CHANNEL_COUNT];

/** 
 * @brief Read every adc channel with R_ADC_READ and store into the parameter buffer
 * @retval int
 */
static int adc_read_all(uint16_t *out)
{
	fsp_err_t err;

	/* For each ADC channel*/
	for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {

		/* Read the ADC result of the indexed channel*/
		err = R_ADC_Read(&adc_ctrl, adc_channels[i], &out[i]);
		/* Return an error if the ADC failed*/
		if (err != FSP_SUCCESS) {
			return -EIO; // Error during FSP ADC reading
		}
	}
	/* Return 0 if succesful*/
	return 0; 
}



/** @brief legacy unused code but a useful tool for extracting uptodate ADC results
 * 	@param out Pointer to output buffer where the ADC results are passed 
 * 	@retval int, always 0.
*/
int adc_service_get(uint16_t *out)
{
	k_mutex_lock(&adc_latest_lock, K_FOREVER);
	memcpy(out, adc_latest, sizeof(adc_latest));
	k_mutex_unlock(&adc_latest_lock);

	return 0;
}

/* @brief thread entry point, runs the forever sample loop.*/
void adc_service_entry(void *a, void *b, void *c)
{
	struct adc_record rec;
	uint16_t seq = 0U; // Holds how many adc reads has happened so far

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Creates adc tick which is a timer set to to increment on ADC sample period*/
	k_timer_start(&adc_tick, K_MSEC(ADC_SAMPLE_PERIOD_MS), K_MSEC(ADC_SAMPLE_PERIOD_MS));

	while (1) {
		/** k_timer_status_sync blocks the thread until ADC_tick increments
		 * 	ADC_tick increments on ADC_SAMPLE_PERIOD_MS (defined in thread_details.h)
		 * 	When it unblocks, it resets to 0.
		*/
		uint32_t fired = k_timer_status_sync(&adc_tick);

		/* Fired is larger than one if it did not unblock before another two calls occured, so the fired is larger than one.*/
		if (fired > 1U) {
			/* Increment the overrun timer by how many over runs occur, the timer resets to 0 on unblock so these are lost samples */
			adc_status_log.data.overruns += fired - 1U;
		}

		/* If the adc reads resulting in an error than increment errors and print adc read failed*/
		if (adc_read_all(rec.ch) != 0) {
			printk("[Err] ADC read failed\n");
			adc_status_log.data.errors++;
			continue; // Skip the rest of the while loop for this iteartion
		}

		rec.seq = seq++; /* Increment the ammount of adc samples have happened so far*/
		adc_status_log.data.actions++; /* The adc sample happened without error so increment actions*/

		qspi_service_write(&rec, sizeof(rec)); /* */

		k_mutex_lock(&adc_latest_lock, K_FOREVER);

		memcpy(adc_latest, rec.ch, sizeof(adc_latest)); /* Copy the up to date ADC results into the record object*/

		k_mutex_unlock(&adc_latest_lock);
	}
}

/**
 * @brief start the ADC in continous mode using FSP calls.
 */
static int start_ADC_FSP(void){

	fsp_err_t err;
		int ret;

		ret = pinctrl_apply_state(adc_pcfg, PINCTRL_STATE_DEFAULT);
		if (ret != 0) {
			printk("adc pinctrl: %d\n", ret);
			return ret;
		}

		err = R_ADC_Open(&adc_ctrl, &adc_cfg);
		if (err != FSP_SUCCESS) {
			printk("R_ADC_Open: %d\n", err);
			return -EIO;
		}

		err = R_ADC_ScanCfg(&adc_ctrl, &adc_channel_cfg);
		if (err != FSP_SUCCESS) {
			printk("R_ADC_ScanCfg: %d\n", err);
			return -EIO;
		}

		err = R_ADC_ScanStart(&adc_ctrl);
		if (err != FSP_SUCCESS) {
			printk("R_ADC_ScanStart: %d\n", err);
			return -EIO;
	}
	return 0;
}


int adc_service_init(void)
{


	(void) start_ADC_FSP(); // Start the ADC thread


	// Start the thread
	adc_thread_id = k_thread_create(
		&adc_thread_data,
		adc_thread_stack,
		K_THREAD_STACK_SIZEOF(adc_thread_stack),
		adc_service_entry,
		NULL, NULL, NULL,
		ADC_PRIORITY,
		0,
		K_NO_WAIT);
	
	// Set the threads name to adc_Thread
	k_thread_name_set(adc_thread_id, "adc_thread");

	printk("ADC started\n");

	return 0;
}
