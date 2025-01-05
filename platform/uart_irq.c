#include "drivers/uart.h"
#include "platform/hw.h"

#if !defined(HOST_TEST)
void USART1_IRQHandler(void)
{
    uart_isr_rx_drain(UART1_BASE);
}

void USART2_IRQHandler(void)
{
    uart_isr_rx_drain(UART2_BASE);
}
#endif
