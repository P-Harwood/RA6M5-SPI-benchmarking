#ifndef QSPI_H_
#define QSPI_H_

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>

#include "../thread_details.h"
#include "../logs/log.h"
#define QSPI_SECTOR_SIZE	4096U		/* Sector erase size for MX25L25645G*/

#define QSPI_CLK_DIV		2

#define QSPI_LOG_BLOCK_SIZE	1024U		/* bytes collected in RAM per flash write */
#define QSPI_LOG_QUEUE_BLOCKS	16U		/* blocks that can wait while the flash erases */
#define QSPI_LOG_START		0x000000U	/* log ring in the flash, sector aligned */
#define QSPI_LOG_SIZE		(4U * 1024U * 1024U)


typedef struct qspi_timing_t {
    uint32_t max_len_us;
    uint32_t total_len_us;
} qspi_timing_t;


extern volatile status_logs_t qspi_status_log;

int qspi_service_init(void);

/* Append to the log. Called from the ADC thread only, never blocks */
int qspi_service_write(const void *data, size_t len);

/* Read the log back. Waits for the writer if it is busy */
int qspi_service_read(uint32_t offset, void *out, size_t len);


#endif