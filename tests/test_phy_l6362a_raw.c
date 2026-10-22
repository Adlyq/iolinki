/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host-side unit test for examples/stm32g0_l6362a_raw/phy_l6362a_raw.c.
 *
 * The example is board code, so the STM32G0 USART/DMA/TIM/GPIO surface is
 * mocked in tests/mocks/stm32g0_l6362a. The source is compiled in its real
 * board configuration: ISR pump, RX FRAME DMA and TX FRAME DMA.
 */
#include "main.h"
#include "phy_l6362a_raw.h"

#ifdef NDEBUG
#undef NDEBUG /* Keep behavioral checks active in Release CTest builds. */
#endif

#include <assert.h>
#include <string.h>

#define TEST_RX_DMA_SIZE 256U
#define TEST_GAP_BITS 13U

static const iolink_phy_api_t* s_phy;

/* Model the hardware flag clear: UartConfigure() writes ICR=RTOCF inside the
 * critical section, so a deferred RTOF vector finds the flag already gone. */
static bool s_model_icr_clear;

static uint32_t expected_brr(uint32_t baud)
{
    return (SYSCLOCK_FREQUENCY + (baud / 2U)) / baud;
}

static void boot(iolink_baudrate_t baudrate)
{
    mock_stm32g0_reset();
    s_model_icr_clear = false;
    phy_l6362a_raw_set_initial_baudrate(baudrate);
    assert(phy_l6362a_raw_hw_init() == 0);
    s_phy = phy_l6362a_raw_get();
    assert(s_phy != NULL);
}

/*
 * The board is 32-bit, so the driver stores DMA addresses in 32-bit registers
 * with (uint32_t)(uintptr_t). On a 64-bit host the high address bits are lost;
 * rebuild the pointer from a neighbouring .bss symbol in the same image.
 */
static uint8_t* rx_dma_buffer(void)
{
    uintptr_t base = (uintptr_t) &mock_dma1_channel1;
    uintptr_t addr = (base & ~(uintptr_t) 0xFFFFFFFFULL) | (uintptr_t) DMA1_Channel1->CMAR;
    return (uint8_t*) addr;
}

/** Feed octets through the RX DMA mock and move its write index. */
static void rx_deliver(uint32_t start, const uint8_t* data, uint32_t len)
{
    assert((start + len) <= TEST_RX_DMA_SIZE);
    memcpy(rx_dma_buffer() + start, data, len);
    DMA1_Channel1->CNDTR = TEST_RX_DMA_SIZE - (start + len);
}

static uint32_t expected_gap_arr(uint32_t baud)
{
    uint32_t gap_us = (TEST_GAP_BITS * 1000000U) / baud;
    return gap_us - 1U;
}

static uint32_t expected_cr1(void)
{
    return USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RTOIE |
           USART_CR1_UE;
}

/* Handlers for the mock NVIC: they run where the real vector would run. */
static uint32_t s_irq_deliveries;
static uint32_t s_irq_brr;
static uint32_t s_irq_dma_en;
static uint32_t s_irq_cr1;
static uint32_t s_irq_cr3;
static const uint8_t s_injected_rx[] = {0x5AU, 0xA5U, 0x3CU};

/* Data that arrived before the interrupt was taken: on hardware the DMA writes
 * the octets into the ring as they are received, and RTOF pends afterwards. */
static void inject_rx_bytes(void)
{
    const uint32_t start = TEST_RX_DMA_SIZE - (DMA1_Channel1->CNDTR & 0xFFFFU);
    rx_deliver(start, s_injected_rx, sizeof(s_injected_rx));
}

static void injected_rx_irq(void)
{
    s_irq_cr1 = IOD_UART->CR1;
    if (s_model_icr_clear) {
        IOD_UART->ISR &= ~USART_ISR_RTOF;
    } else {
        IOD_UART->ISR |= USART_ISR_RTOF;
    }
    phy_l6362a_raw_rx_irq();
    s_irq_deliveries++;
}

static void injected_tx_dma_irq(void)
{
    /* Snapshot the state the vector observes before the driver handles it. */
    s_irq_brr = IOD_UART->BRR;
    s_irq_dma_en = DMA1_Channel2->CCR & DMA_CCR_EN;
    s_irq_cr3 = IOD_UART->CR3;
    /* Writing IFCR's CTCIF2 clears the DMA ISR flag in hardware. */
    if ((DMA1->IFCR & DMA_IFCR_CTCIF2) != 0U) {
        DMA1->ISR &= ~DMA_ISR_TCIF2;
    }
    phy_l6362a_raw_tx_dma_irq();
    s_irq_deliveries++;
}

static void test_api_and_hw_init(void)
{
    boot(IOLINK_BAUDRATE_COM3);

    assert(s_phy->user == NULL);
    assert(s_phy->init != NULL);
    assert(s_phy->set_mode != NULL);
    assert(s_phy->set_baudrate != NULL);
    assert(s_phy->rx_frame != NULL);
    assert(s_phy->tx_frame != NULL);
    assert(s_phy->tx_done == NULL);
    assert(s_phy->int_active != NULL);
    assert(s_phy->request_service != NULL);
    assert(s_phy->response_timer_start != NULL);
    assert(s_phy->response_timer_stop != NULL);
    assert(s_phy->detect_wakeup != NULL);
    assert(s_phy->get_faults == NULL);

    /* Transceiver enable high, transmit enable released. */
    assert(mock_en_level == GPIO_PIN_SET);
    assert(mock_txen_level == GPIO_PIN_RESET);

    /* TIM3: 1 us tick, one-shot update interrupt. */
    assert(TIM3->PSC == ((SYSCLOCK_FREQUENCY / 1000000U) - 1U));
    assert(TIM3->DIER == TIM_DIER_UIE);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);

    /* RX DMA: circular, one byte per request, points at the RX data register. */
    assert(DMAMUX1_Channel0->CCR == DMA_REQUEST_USART2_RX);
    assert(DMA1_Channel1->CPAR == (uint32_t) (uintptr_t) &IOD_UART->RDR);
    assert(DMA1_Channel1->CMAR != 0U);
    assert(DMA1_Channel1->CNDTR == TEST_RX_DMA_SIZE);
    assert((DMA1_Channel1->CCR & (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN)) ==
           (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN));

    /* TX DMA: configured per frame, idle after init. */
    assert(DMAMUX1_Channel1->CCR == DMA_REQUEST_USART2_TX);
    assert(DMA1_Channel2->CCR == 0U);

    /* UART: 9-bit, even parity, RX timeout, RX DMA and error interrupt. */
    assert(IOD_UART->BRR == expected_brr(230400U));
    assert(IOD_UART->CR2 == USART_CR2_RTOEN);
    assert(IOD_UART->CR3 == (USART_CR3_EIE | USART_CR3_DMAR));
    assert(IOD_UART->CR1 == expected_cr1());
    assert(IOD_UART->ICR == (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF |
                             USART_ICR_TCCF | USART_ICR_RTOCF));
    assert(IOD_UART->RQR == USART_RQR_RXFRQ);

    /* UART pins are routed to the USART alternate function. */
    assert(mock_gpio_init_calls == 1U);
    assert(mock_gpio_init_pin == (UART_TX_Pin | UART_RX_Pin));
    assert(mock_gpio_init_alternate == IOD_UART_GPIO_AF);

    /* Interrupt priorities/ordering from the board bring-up. */
    assert(mock_nvic_priority[IOD_UART_IRQn] == 0U);
    assert(mock_nvic_priority[TIM3_IRQn] == 1U);
    assert(mock_nvic_priority[DMA1_Channel2_3_IRQn] == 2U);
    assert(mock_nvic_enabled[IOD_UART_IRQn] == 1U);
    assert(mock_nvic_enabled[TIM3_IRQn] == 1U);
    assert(mock_nvic_enabled[DMA1_Channel2_3_IRQn] == 1U);

    /* Timer A one-shot uses a 13 Tbit window. */
    s_phy->response_timer_start(NULL);
    assert(TIM3->CNT == 0U);
    assert(TIM3->ARR == expected_gap_arr(230400U));
    assert((TIM3->CR1 & TIM_CR1_CEN) != 0U);
    s_phy->response_timer_stop(NULL);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);
    assert((TIM3->SR & TIM_SR_UIF) == 0U);
}

static void test_baudrates(void)
{
    boot(IOLINK_BAUDRATE_COM1);

    assert(IOD_UART->BRR == expected_brr(4800U));
    s_phy->response_timer_start(NULL);
    assert(TIM3->ARR == expected_gap_arr(4800U));

    s_phy->set_baudrate(NULL, IOLINK_BAUDRATE_COM2);
    assert(IOD_UART->BRR == expected_brr(38400U));
    s_phy->response_timer_start(NULL);
    assert(TIM3->ARR == expected_gap_arr(38400U));

    s_phy->set_baudrate(NULL, IOLINK_BAUDRATE_COM3);
    assert(IOD_UART->BRR == expected_brr(230400U));
    s_phy->response_timer_start(NULL);
    assert(TIM3->ARR == expected_gap_arr(230400U));
}

static void test_rx_frame(void)
{
    uint32_t tx0;
    uint32_t rx0;
    uint32_t chk0;
    uint32_t tx1;
    uint32_t rx1;
    uint32_t chk1;
    uint8_t status;
    uint8_t len;
    const uint8_t* frame;

    const uint8_t frame_a[] = {0x05U, 0x10U, 0x20U, 0x30U};
    const uint8_t frame_b[] = {0xA1U, 0xB2U};
    const uint8_t frame_c[] = {0xC1U, 0xC2U, 0xC3U};
    const uint8_t frame_d[] = {0xD1U, 0xD2U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    phy_l6362a_raw_get_counters(&tx0, &rx0, &chk0);

    assert(s_phy->int_active(NULL) == 0U);
    assert(s_phy->detect_wakeup(NULL) == 0);

    /* Normal frame: DMA wrote four octets, RTOF delimits it. */
    rx_deliver(0U, frame_a, sizeof(frame_a));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    assert((IOD_UART->ICR & USART_ICR_RTOCF) != 0U);
    assert(s_phy->int_active(NULL) == 1U);
    assert(s_phy->detect_wakeup(NULL) == 1);
    assert(s_phy->int_active(NULL) == 1U); /* wake detection is not a read */

    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(frame == rx_dma_buffer());
    assert(len == sizeof(frame_a));
    assert(status == IOLINK_PHY_STATUS_DATA_READY);
    assert(memcmp(frame, frame_a, sizeof(frame_a)) == 0);
    assert(s_phy->int_active(NULL) == 0U);

    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame == NULL);
    assert(len == 0U);
    assert(status == 0U);

    /* A frame completed while the previous one is unread is dropped. */
    rx_deliver(4U, frame_b, sizeof(frame_b));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    rx_deliver(6U, frame_c, sizeof(frame_c));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();

    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(len == sizeof(frame_b));
    assert(status == IOLINK_PHY_STATUS_DATA_READY);
    assert(memcmp(frame, frame_b, sizeof(frame_b)) == 0);

    /* Receive error during a frame is reported as CHK, not DATA_READY. */
    rx_deliver(9U, frame_d, sizeof(frame_d));
    IOD_UART->ISR = USART_ISR_RTOF | USART_ISR_PE;
    phy_l6362a_raw_rx_irq();
    assert((IOD_UART->ICR & USART_ICR_PECF) != 0U);
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(len == sizeof(frame_d));
    assert(status == IOLINK_PHY_STATUS_CHK);
    assert(memcmp(frame, frame_d, sizeof(frame_d)) == 0);

    phy_l6362a_raw_get_counters(&tx1, &rx1, &chk1);
    assert((tx1 - tx0) == 0U);
    assert((rx1 - rx0) == (sizeof(frame_a) + sizeof(frame_b) + sizeof(frame_d)));
    assert((chk1 - chk0) == 0U); /* CHK counter is a BYTE-RX statistic */
}

static void test_rx_frame_wraps(void)
{
    const uint8_t first[] = {0x11U, 0x12U, 0x13U, 0x14U};
    uint8_t status;
    uint8_t len;
    const uint8_t* frame;
    uint8_t* dma = rx_dma_buffer();
    uint32_t i;

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);

    rx_deliver(0U, first, sizeof(first));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL && len == sizeof(first));

    /* Next frame starts near the end of the circular buffer and wraps. */
    for (i = 4U; i < TEST_RX_DMA_SIZE; i++) {
        dma[i] = (uint8_t) (0x40U + (i & 0x3FU));
    }
    dma[0] = 0x30U;
    dma[1] = 0x31U;
    dma[2] = 0x32U;
    dma[3] = 0x33U;
    DMA1_Channel1->CNDTR = TEST_RX_DMA_SIZE - 3U; /* write index is 3 */

    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(frame != dma); /* linearised into the shadow buffer */
    assert(len == 255U);
    assert(status == IOLINK_PHY_STATUS_DATA_READY);
    assert(frame[0] == dma[4]);
    assert(frame[251] == dma[255]);
    assert(frame[252] == dma[0]);
    assert(frame[253] == dma[1]);
    assert(frame[254] == dma[2]);
}

static void test_tx_frame_and_completion(void)
{
    const uint8_t reply[] = {0xA5U, 0x5AU, 0x13U};
    uint32_t tx0;
    uint32_t rx0;
    uint32_t chk0;
    uint32_t tx1;
    uint32_t rx1;
    uint32_t chk1;

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    phy_l6362a_raw_get_counters(&tx0, &rx0, &chk0);

    /* The transmit hooks may only run from the ISR pump. */
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;

    assert(s_phy->tx_frame(NULL, NULL, 3U) == -1);
    assert(s_phy->tx_frame(NULL, reply, 0U) == -1);
    assert(s_phy->tx_frame(NULL, reply, 0x10000U) == -1);

    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) == 0U); /* no echo capture */
    assert((IOD_UART->CR3 & USART_CR3_DMAT) != 0U);
    assert(DMA1_Channel2->CPAR == (uint32_t) (uintptr_t) &IOD_UART->TDR);
    assert(DMA1_Channel2->CMAR == (uint32_t) (uintptr_t) reply);
    assert(DMA1_Channel2->CNDTR == sizeof(reply));
    assert(DMA1_Channel2->CCR == (DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_EN));

    phy_l6362a_raw_get_counters(&tx1, &rx1, &chk1);
    assert((tx1 - tx0) == sizeof(reply));
    assert((rx1 - rx0) == 0U);
    assert((chk1 - chk0) == 0U);

    /* DMA transfer complete releases TX_EN and re-arms the receiver. */
    DMA1->ISR = DMA_ISR_TCIF2;
    IOD_UART->ISR = USART_ISR_TC;
    DMA1->IFCR = 0U;
    phy_l6362a_raw_tx_dma_irq();

    assert((DMA1->IFCR & DMA_IFCR_CTCIF2) != 0U);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAT) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert(s_phy->int_active(NULL) == 0U);
}

static void test_tx_frame_busy_is_refused(void)
{
    const uint8_t first[] = {0x11U, 0x22U, 0x33U};
    const uint8_t second[] = {0x44U, 0x55U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;

    assert(s_phy->tx_frame(NULL, first, sizeof(first)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U);

    /* While the DMA is still enabled the reply is on the wire: the new frame
     * must be refused, never silently tear the running transfer down. */
    assert(s_phy->tx_frame(NULL, second, sizeof(second)) == -1);
    assert(DMA1_Channel2->CMAR == (uint32_t) (uintptr_t) first);
    assert(DMA1_Channel2->CNDTR == sizeof(first));
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U);
}

static void test_tx_frame_completes_pending_hardware(void)
{
    const uint8_t first[] = {0x11U, 0x22U, 0x33U};
    const uint8_t second[] = {0x44U, 0x55U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;

    assert(s_phy->tx_frame(NULL, first, sizeof(first)) == 0);

    /* Hardware finished the transfer (non-circular DMA clears EN) but the
     * completion IRQ is still pending behind this higher-priority ISR. The next
     * frame must finish the pending reply (wait for TC, release TX_EN) and then
     * start cleanly instead of stacking on top of it. */
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    IOD_UART->ISR = USART_ISR_TC;
    assert(s_phy->tx_frame(NULL, second, sizeof(second)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert(DMA1_Channel2->CMAR == (uint32_t) (uintptr_t) second);
    assert(DMA1_Channel2->CNDTR == sizeof(second));
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U);
}

static void test_tx_dma_irq_tc_timeout_releases_line(void)
{
    const uint8_t reply[] = {0x10U, 0x20U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;
    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);

    /* Model a stuck UART: the DMA transfer completes (TCIF) but TC never sets.
     * The bounded wait must expire on the (auto-advancing) mock clock and still
     * release TX_EN / re-arm RX, never spin forever in interrupt context. */
    mock_tick_auto_advance = 1U;
    DMA1->ISR = DMA_ISR_TCIF2;
    IOD_UART->ISR = 0U;
    phy_l6362a_raw_tx_dma_irq();

    assert((DMA1->IFCR & DMA_IFCR_CTCIF2) != 0U);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAT) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
}

static void test_tx_frame_requires_isr_context(void)
{
    const uint8_t reply[] = {0x01U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);

    SCB->ICSR = 0U;
    mock_error_handler_calls = 0U;
    assert(s_phy->tx_frame(NULL, NULL, sizeof(reply)) == -1);
    assert(mock_error_handler_calls == 1U);
}

static void test_baudrate_change_aborts_tx(void)
{
    const uint8_t reply[] = {0x10U, 0x20U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;

    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U);

    DMA1->ISR = DMA_ISR_TCIF2;
    DMA1->IFCR = 0U;
    s_phy->set_baudrate(NULL, IOLINK_BAUDRATE_COM1);

    /* The in-flight frame is aborted before the UART is reprogrammed. */
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAT) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);
    assert((DMA1->IFCR & DMA_IFCR_CTCIF2) != 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert(IOD_UART->BRR == expected_brr(4800U));
}

static void test_mode_wakeup_and_irq_feeds(void)
{
    const uint8_t payload[] = {0x01U, 0x02U};
    uint8_t status;
    uint8_t len;

    boot(IOLINK_BAUDRATE_COM3);

    /* SIO claims no service and leaves RX capture armed. */
    assert(s_phy->int_active(NULL) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);

    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    rx_deliver(0U, payload, sizeof(payload));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    assert(s_phy->int_active(NULL) == 1U);

    /* Re-entering SIO flushes the stale frame and discards the bytes seen
     * there; the next frame starts at the current write index. */
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SIO);
    assert(s_phy->int_active(NULL) == 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert(s_phy->rx_frame(NULL, &status, &len) == NULL || len == 0U);

    /* OL INT latches a wake-up and is consumed once. */
    assert(s_phy->detect_wakeup(NULL) == 0);
    phy_l6362a_raw_ol_irq();
    assert(s_phy->detect_wakeup(NULL) == 1);
    assert(s_phy->detect_wakeup(NULL) == 0);

    /* request_service pokes the USART IRQ so the ISR sends a staged reply. */
    mock_last_pending_irq = (IRQn_Type) -1;
    s_phy->request_service(NULL);
    assert(mock_last_pending_irq == IOD_UART_IRQn);

    /* Byte-gap update only stops the timer in FRAME RX mode. */
    TIM3->SR = TIM_SR_UIF;
    TIM3->CR1 = TIM_CR1_CEN;
    phy_l6362a_raw_gap_irq();
    assert((TIM3->SR & TIM_SR_UIF) == 0U);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);
    assert(s_phy->int_active(NULL) == 0U);

    /* TC is acknowledged without producing a frame. */
    IOD_UART->ISR = USART_ISR_TC;
    IOD_UART->ICR = 0U;
    phy_l6362a_raw_rx_irq();
    assert((IOD_UART->ICR & USART_ICR_TCCF) != 0U);
    assert(s_phy->rx_frame(NULL, &status, &len) == NULL);

    /* OL_INT is read from the GPIO mock. */
    mock_ol_pin_level = GPIO_PIN_RESET;
    assert(phy_l6362a_raw_int_line_active() == 0U);
    mock_ol_pin_level = GPIO_PIN_SET;
    assert(phy_l6362a_raw_int_line_active() == 1U);

    /* Diagnostics accessors tolerate NULL outputs. */
    phy_l6362a_raw_get_counters(NULL, NULL, NULL);
}

/*
 * ISR/main interleaving 1: an RX RTOF arrives while main is re-entering SIO.
 * The mock delivers it at the UART pin reconfiguration point, either inline
 * (interrupts open) or when the mode-transition critical section ends.
 * The in-flight TX FRAME must be aborted and the frame must survive as the
 * wake indication.
 */
static void test_isr_rx_during_set_mode(void)
{
    const uint8_t reply[] = {0xA0U, 0x0AU};
    uint8_t status;
    uint8_t len;
    const uint8_t* frame;

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;
    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U);

    s_irq_deliveries = 0U;
    s_model_icr_clear = true;
    mock_set_irq_handler(MOCK_IRQ_RX, injected_rx_irq);
    mock_set_inject_prepare(inject_rx_bytes);
    mock_inject_irq(MOCK_TRIGGER_GPIO_INIT, MOCK_IRQ_RX);

    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SIO);
    assert(s_irq_deliveries == 1U);
    /* Deferred until after UartConfigure(): the vector saw the re-armed UART. */
    assert(s_irq_cr1 == expected_cr1());
    /* UartConfigure() cleared RTOCF, so the vector itself could no longer
     * publish the frame; the setter must have done it. */
    assert((IOD_UART->ISR & USART_ISR_RTOF) == 0U);

    /* The SIO transition aborted the in-flight FRAME reply. */
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAT) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);

    /* SIO claims no service, but the frame that arrived during the transition
     * is retained as the wake indicator. */
    assert(s_phy->int_active(NULL) == 0U);
    assert(s_phy->detect_wakeup(NULL) == 1);

    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    assert(s_phy->int_active(NULL) == 1U);
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(len == sizeof(s_injected_rx));
    assert(status == IOLINK_PHY_STATUS_DATA_READY);
    assert(memcmp(frame, s_injected_rx, sizeof(s_injected_rx)) == 0);
    assert(s_irq_deliveries == 1U);
}

/*
 * ISR/main interleaving 2: a TX DMA completion is pending while main changes
 * the baudrate. It must not nest inside the critical section; it runs after
 * the abort/reprogramming and observes the new BRR with the DMA already off.
 */
static void test_isr_tx_complete_deferred_by_baudrate(void)
{
    const uint8_t reply[] = {0x10U, 0x20U, 0x30U};

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;
    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);

    DMA1->ISR = DMA_ISR_TCIF2;
    IOD_UART->ISR = USART_ISR_TC;
    DMA1->IFCR = 0U;

    s_irq_deliveries = 0U;
    s_irq_brr = 0U;
    s_irq_dma_en = 0xFFFFFFFFU;
    s_irq_cr3 = 0U;
    mock_set_irq_handler(MOCK_IRQ_TX_DMA, injected_tx_dma_irq);
    mock_inject_irq(MOCK_TRIGGER_GPIO_INIT, MOCK_IRQ_TX_DMA);

    s_phy->set_baudrate(NULL, IOLINK_BAUDRATE_COM1);

    assert(s_irq_deliveries == 1U);
    /* Deferred: the vector ran after the UART was reprogrammed and the DMA
     * abort had already completed. */
    assert(s_irq_brr == expected_brr(4800U));
    assert(s_irq_dma_en == 0U);
    assert((s_irq_cr3 & USART_CR3_DMAT) == 0U);
    assert(IOD_UART->BRR == expected_brr(4800U));
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
}

/*
 * ISR/main interleaving 3: an RX frame arrives during a baudrate change.
 * The deferred vector must still publish a complete frame and leave the new
 * baudrate/UART state intact.
 */
static void test_isr_rx_deferred_by_baudrate(void)
{
    uint8_t status;
    uint8_t len;
    const uint8_t* frame;

    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);

    s_irq_deliveries = 0U;
    s_model_icr_clear = true;
    mock_set_irq_handler(MOCK_IRQ_RX, injected_rx_irq);
    mock_set_inject_prepare(inject_rx_bytes);
    mock_inject_irq(MOCK_TRIGGER_GPIO_INIT, MOCK_IRQ_RX);

    s_phy->set_baudrate(NULL, IOLINK_BAUDRATE_COM1);

    assert(s_irq_deliveries == 1U);
    assert(s_irq_cr1 == expected_cr1());
    assert(IOD_UART->BRR == expected_brr(4800U));
    /* The flag was cleared by UartConfigure(); the setter published the frame. */
    assert((IOD_UART->ISR & USART_ISR_RTOF) == 0U);
    assert(s_phy->int_active(NULL) == 1U);
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(len == sizeof(s_injected_rx));
    assert(status == IOLINK_PHY_STATUS_DATA_READY);
    assert(memcmp(frame, s_injected_rx, sizeof(s_injected_rx)) == 0);
}

static void injected_ol_irq(void)
{
    phy_l6362a_raw_ol_irq();
    s_irq_deliveries++;
}

/* The OL latch is written from the EXTI vector while the main context is inside
 * a mode transition; it must survive the transition and be consumed once. */
static void test_ol_irq_during_mode_change(void)
{
    boot(IOLINK_BAUDRATE_COM3);
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);

    s_irq_deliveries = 0U;
    mock_set_irq_handler(MOCK_IRQ_OL, injected_ol_irq);
    mock_inject_irq(MOCK_TRIGGER_GPIO_INIT, MOCK_IRQ_OL);

    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SIO);

    assert(s_irq_deliveries == 1U);
    assert(s_phy->detect_wakeup(NULL) == 1);
    assert(s_phy->detect_wakeup(NULL) == 0);
}

static void test_platform_hooks(void)
{
    uint8_t byte = 0U;

    mock_stm32g0_reset();

    /* SysTick-derived microsecond timebase. */
    mock_tick_ms = 5U;
    SysTick->LOAD = SystemCoreClock / 1000U;
    SysTick->VAL = 0U;
    assert(iolink_time_get_us() ==
           (5ULL * 1000ULL) + (uint64_t) (SysTick->LOAD / (SystemCoreClock / 1000000U)));
    assert(iolink_time_get_ms() == 6U);

    SysTick->VAL = SysTick->LOAD;
    assert(iolink_time_get_us() == 5000ULL);
    assert(iolink_time_get_ms() == 5U);

    /* Critical sections nest and restore the saved PRIMASK at depth zero. */
    mock_primask = 0U;
    iolink_critical_enter();
    assert(mock_primask == 1U);
    iolink_critical_enter();
    mock_primask = 0U;
    iolink_critical_exit();
    assert(mock_primask == 0U);
    iolink_critical_exit();
    assert(mock_primask == 0U);

    mock_primask = 1U;
    iolink_critical_enter();
    iolink_critical_exit();
    assert(mock_primask == 1U);
    iolink_critical_exit(); /* unbalanced exit must not underflow */
    assert(mock_primask == 1U);

    /* No NVM on this board: both hooks report "not implemented". */
    assert(iolink_nvm_read(0U, &byte, 1U) == -1);
    assert(iolink_nvm_write(0U, &byte, 1U) == -1);
}

int main(void)
{
    test_api_and_hw_init();
    test_baudrates();
    test_rx_frame();
    test_rx_frame_wraps();
    test_tx_frame_and_completion();
    test_tx_frame_busy_is_refused();
    test_tx_frame_completes_pending_hardware();
    test_tx_dma_irq_tc_timeout_releases_line();
    test_tx_frame_requires_isr_context();
    test_baudrate_change_aborts_tx();
    test_mode_wakeup_and_irq_feeds();
    test_isr_rx_during_set_mode();
    test_isr_tx_complete_deferred_by_baudrate();
    test_isr_rx_deferred_by_baudrate();
    test_ol_irq_during_mode_change();
    test_platform_hooks();
    return 0;
}
