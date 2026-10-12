#ifndef ADC_SERVICE_H_
#define ADC_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>


#include "../thread_details.h"

#include "../logs/log.h"

#include <errno.h>
#include <stdint.h>


#define ADC_CHANNEL_COUNT		7

#define ADC_VREF_MV		3300U // Relies on MCU running on 3.3v (This demo doesnt actually measure anything)
#define ADC_FULL_SCALE		4096U // RA6M5 Has a 12bit ADC, so scales to 4096


/** ADC record is a 128 bit struct when there are 7 ADC channels (16 bit + 7 * 16bit)
 * 	So 112 bits + 16bits for the Record id, 128 bits. With a qspi transfer size of 1024 bytes:
 * 	1024 bytes / 128 bits = 64 records per QSPI write. It should be expected in the print logs that
 * 	The ADC actions divided by 64 will equal the QSPI ammount.
 * 
*/
struct adc_record {
	uint16_t seq; // Record ID
	uint16_t ch[ADC_CHANNEL_COUNT]; // Array which holds the ADC Results
};



extern volatile status_logs_t adc_status_log;

int adc_service_init(void);


int adc_service_get(uint16_t *out);


void adc_service_entry(void *a, void *b, void *c);

#endif