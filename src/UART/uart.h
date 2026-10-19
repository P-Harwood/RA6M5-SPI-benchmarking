#ifndef UART_H_
#define UART_H_

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>


#include "../logs/log.h"

#include "../thread_details.h"

#define UART_SCI_NODE	DT_NODELABEL(sci7)	/* SCI7: P613 TX, P614 RX */
#define UART_SCI_CHANNEL	7U

#define UART_BAUD_HZ	7500000U

#define UART_MAX_LEN 1024U
#define UART_MIN_LEN 722U


/* SCI7 interrupt lines and priorities, taken from the sci7 node in the devicetree */
#define UART_RXI_IRQ		DT_IRQ_BY_NAME(UART_SCI_NODE, rxi, irq)
#define UART_TXI_IRQ		DT_IRQ_BY_NAME(UART_SCI_NODE, txi, irq)
#define UART_TEI_IRQ		DT_IRQ_BY_NAME(UART_SCI_NODE, tei, irq)
#define UART_ERI_IRQ		DT_IRQ_BY_NAME(UART_SCI_NODE, eri, irq)
#define UART_RXI_PRIORITY	DT_IRQ_BY_NAME(UART_SCI_NODE, rxi, priority)
#define UART_TXI_PRIORITY	DT_IRQ_BY_NAME(UART_SCI_NODE, txi, priority)
#define UART_TEI_PRIORITY	DT_IRQ_BY_NAME(UART_SCI_NODE, tei, priority)
#define UART_ERI_PRIORITY	DT_IRQ_BY_NAME(UART_SCI_NODE, eri, priority)

/* Every receive error FSP can report from the ERI interrupt */
#define UART_RX_ERRORS	(UART_EVENT_ERR_PARITY | UART_EVENT_ERR_FRAMING | \
			 UART_EVENT_ERR_OVERFLOW | UART_EVENT_BREAK_DETECT)

/* FSP's SCI UART interrupt handlers, defined in r_sci_uart.c */
void sci_uart_rxi_isr(void);
void sci_uart_txi_isr(void);
void sci_uart_tei_isr(void);
void sci_uart_eri_isr(void);



int uart_service_init(void);
void uart_service_entry(void *a, void *b, void *c);


extern volatile status_logs_t uart_status_log;



#endif