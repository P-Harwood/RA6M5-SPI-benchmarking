#include "qspi.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>

BUILD_ASSERT((QSPI_SECTOR_SIZE % QSPI_LOG_BLOCK_SIZE) == 0U, "Log block size must be a divisble factor of the Flash erase sector size");

static void qspi_thread(void *a, void *b, void *c);

static const struct device *const flash = DEVICE_DT_GET(DT_NODELABEL(mx25l25645g));


/* Block queue. fill_* belong to the ADC thread, write_idx to qspi_thread, the semaphores
 * carry the ownership of a block between them. */
static uint8_t blocks[QSPI_LOG_QUEUE_BLOCKS][QSPI_LOG_BLOCK_SIZE] __aligned(4);
static K_SEM_DEFINE(sem_free, QSPI_LOG_QUEUE_BLOCKS, QSPI_LOG_QUEUE_BLOCKS);
static K_SEM_DEFINE(sem_ready, 0, QSPI_LOG_QUEUE_BLOCKS);
static uint32_t fill_idx;
static uint32_t fill_len;
static uint32_t write_idx;

static uint32_t flash_addr = QSPI_LOG_START;	/* where the next block goes */



volatile status_logs_t qspi_status_log = { .log_name = "QSPI_SV" };


K_THREAD_STACK_DEFINE(qspi_stack, QSPI_STACK_SIZE);
static struct k_thread qspi_thread_data;


int qspi_service_init(void)
{
	if (!device_is_ready(flash)) {
		printk("qspi: flash device not ready\n");
		return -ENODEV;
	}

	k_tid_t tid = k_thread_create(&qspi_thread_data, qspi_stack, K_THREAD_STACK_SIZEOF(qspi_stack),
				      qspi_thread, NULL, NULL, NULL, QSPI_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tid, "qspi_thread");

	printk("QSPI thread started\n");

	return 0;
}

/* This is the QSPI thread */
static void qspi_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("QSPI thread started\n");

	/* Forever loop */
	while (1) {
		k_sem_take(&sem_ready, K_FOREVER);	/* Sleeps until a semaphore is given to the thread (passed by ADC thread) */

        int ret = 0;
		uint32_t t0 = k_cycle_get_32(); // Iniital comparison time is recorded here

		/** Check if flash addr is at the end of the current sector,
		 *  if so erase the next block, which starts at the current flash_addr (end of previous sector)
		 * */
		if ((flash_addr % QSPI_SECTOR_SIZE) == 0U) {
			/* Erase the next block so new data can be written into it*/
			ret = flash_erase(flash, flash_addr, QSPI_SECTOR_SIZE);
		}

		/* If the block erase option went well (or ret is still default as no delete was required)*/
		if (ret == 0) {
			/* Write the block of data into the QSPI flash*/
			ret = flash_write(flash, flash_addr, blocks[write_idx], QSPI_LOG_BLOCK_SIZE);
		}

		/* Record the time taken to complete the erase/write operations by deducting the initial comparison time */
		uint32_t us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);


		/* If the previous write opeartion went well then increment the actions log field*/
		if (ret == 0) {
			qspi_status_log.data.actions++;
		} else {
			/* If the write command failed then increment the errors field*/
			qspi_status_log.data.errors++;
		}

		/* Add the block size to the flash_Addr variable so the next write goes to a new location */
		flash_addr += QSPI_LOG_BLOCK_SIZE;

		/* If the new flash address is larger than Log size then wrap around to the start of the log*/
		if (flash_addr >= (QSPI_LOG_START + QSPI_LOG_SIZE)) {
			flash_addr = QSPI_LOG_START;
		}

		/* The next given write_idx in the ring buffer is incremented by 1, and if the queue blocks limit is reached it wraps around to 0*/
		write_idx = (write_idx + 1U) % QSPI_LOG_QUEUE_BLOCKS;
		/* Operation has completed give the free sempahore back to qspi_service_write  */
		k_sem_give(&sem_free);
	}
}

int qspi_service_write(const void *data, size_t len)
{
	/* If the length of the data is larger than the space left in the block then throw an error*/
	if (len > (QSPI_LOG_BLOCK_SIZE - fill_len)) {
		return -EINVAL;		/* records must not straddle blocks, adc.c asserts this */
	}

	/* If it is the start of a new block check to see if the QSPI is free, if it is not then increment the QSPI overruns*/
	if ((fill_len == 0U) && (k_sem_take(&sem_free, K_NO_WAIT) != 0)) {
		qspi_status_log.data.overruns++; /* Increment QSPI overruns*/
		return -ENOBUFS;/* Return err*/
	}

	/**	(Blocks is a SRAM array of records waiting to be written)
	 * 	Select the current blocks index, and offset it by which record in this block is being written
	 * 	Write the ADC data to this memory location in the SRAM buffer*/
	memcpy(&blocks[fill_idx][fill_len], data, len);

	/* Increment fill length so the next ADC record is in the location inside the SRAM buffer*/
	fill_len += len;

	/* If the next record is going to be written outside of the current block memory allocation then close this block and reinitialise*/
	if (fill_len == QSPI_LOG_BLOCK_SIZE) {
		fill_len = 0U;		/* Reset the write to location for the new block to be 0, so the next 256b block starts at 0*/
		fill_idx = (fill_idx + 1U) % QSPI_LOG_QUEUE_BLOCKS; /* Increment (or reset if exceeding the blocks limit) the ring buffer id*/
		k_sem_give(&sem_ready);	/* Tell QSPI to transmit the buffer */
	}

	return 0;
}

/** @brief Wrapper function to clal the flash_read function 
 * 	@retval int refelcting the status of the flash_read call
*/
int qspi_service_read(uint32_t offset, void *out, size_t len)
{
	return flash_read(flash, offset, out, len);	/* the driver serialises against the writer */
}
