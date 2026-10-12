#ifndef SPI_H_
#define SPI_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#include "../thread_details.h"
#include "string.h"
#include "../logs/log.h"

#define SPI_NODE	DT_NODELABEL(spi0)

#define SPI_FREQ_HZ	17000000U
#define WORD_BITS	16U
#define TRANSF_LEN	1024U


extern volatile status_logs_t spi_status_log;

int spi_service_init(void);
void spi_service_entry(void *a, void *b, void *c);

#endif /* SPI_H_ */