#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/irq_dma.h"

#define DMA1_BASE 0x40020000u
#define DMA1_ISR (DMA1_BASE + 0x00u)
#define DMA1_IFCR (DMA1_BASE + 0x04u)
#define DMA1_CH2_BASE (DMA1_BASE + 0x1Cu)
#define DMA1_CH3_BASE (DMA1_BASE + 0x30u)

#define DMA_ISR_TCIF2 (1u << 5)
#define DMA_ISR_TCIF3 (1u << 9)

#define DMA_CCR(ch) ((ch) + 0x00u)

volatile uint8_t g_spi_dma_rx_done;
volatile uint8_t g_spi_dma_tx_done;

#if !defined(HOST_TEST)
void DMA1_Channel2_IRQHandler(void)
{
    if ((mmio_read32(DMA1_ISR) & DMA_ISR_TCIF2) == 0u)
        return;

    /* Clear TCIF2 only (OEM writes 0x20). */
    mmio_write32(DMA1_IFCR, DMA_ISR_TCIF2);

    /* Disable SPI1 and DMA1 CH2, clear TCIE. */
    uint32_t cr1 = mmio_read32(SPI1_BASE + 0x00u);
    mmio_write32(SPI1_BASE + 0x00u, cr1 & ~0x40u);

    uint32_t ccr = mmio_read32(DMA_CCR(DMA1_CH2_BASE));
    ccr &= ~1u;
    ccr &= ~2u;
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), ccr);

    /* Deassert SPI flash CS (PA4 high). */
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (1u << 4));
    g_spi_dma_rx_done = 1u;
}

void DMA1_Channel3_IRQHandler(void)
{
    if ((mmio_read32(DMA1_ISR) & DMA_ISR_TCIF3) == 0u)
        return;

    /* Clear TCIF3 only (OEM writes 0x200). */
    mmio_write32(DMA1_IFCR, DMA_ISR_TCIF3);

    /* Wait until SPI1 not busy. */
    while (mmio_read32(SPI1_BASE + 0x08u) & 0x80u)
    {
    }

    /* Disable DMA1 CH3, clear TCIE. */
    uint32_t ccr = mmio_read32(DMA_CCR(DMA1_CH3_BASE));
    ccr &= ~1u;
    ccr &= ~2u;
    mmio_write32(DMA_CCR(DMA1_CH3_BASE), ccr);

    /* Deassert SPI flash CS (PA4 high). */
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (1u << 4));
    g_spi_dma_tx_done = 1u;
}
#endif
