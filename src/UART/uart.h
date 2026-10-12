#ifndef UART_H_
#define UART_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>


#include "../logs/log.h"

#include "../thread_details.h"

#define UART_NODE	DT_NODELABEL(uart7)

#define UART_BAUD_HZ	7500000U
#define UART_TX_LEN	722U

extern volatile status_logs_t uart_status_log;

int uart_service_init(void);
void uart_service_entry(void *a, void *b, void *c);

#endif