#define thread_log_count 4 /* How many threads is being tracked for errors/overruns .etc*/

#define SPI_PRIORITY 1
#define ADC_PRIORITY 2
#define UART_PRIORITY 3
#define PWM_PRIORITY 4
#define QSPI_PRIORITY 5

#define UART_STACK_SIZE 4096
#define SPI_STACK_SIZE 4096
#define PWM_STACK_SIZE 2048
#define ADC_STACK_SIZE 2048
#define QSPI_STACK_SIZE 2048


#define SPI_PERIOD_MS	1
#define UART_PERIOD_MS	2
#define ADC_SAMPLE_PERIOD_MS	1

