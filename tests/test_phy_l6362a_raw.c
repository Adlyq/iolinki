/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * examples/stm32g0_l6362a_raw/phy_l6362a_raw.c 的主机侧单元测试。
 *
 * 该示例属于板级代码，因此在 tests/mocks/stm32g0_l6362a 中
 * 对 STM32G0 的 USART/DMA/TIM/GPIO 接口进行了 mock。源码按其真实
 * 板级配置编译：ISR 泵、RX FRAME DMA 与 TX FRAME DMA。
 */
#include "main.h"
#include "phy_l6362a_raw.h"

#ifdef NDEBUG
#undef NDEBUG /* 在 Release 的 CTest 构建中保持行为检查生效。 */
#endif

#include <assert.h>
#include <string.h>

#define TEST_RX_DMA_SIZE 256U
#define TEST_GAP_BITS 13U

static const iolink_phy_api_t* s_phy;

/* 模拟硬件标志清除：UartConfigure() 在临界区内写入 ICR=RTOCF，
 * 因此被推迟的 RTOF 向量会发现该标志已被清除。 */
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
 * 该板为 32 位，因此驱动使用 (uint32_t)(uintptr_t) 将 DMA 地址
 * 存入 32 位寄存器。在 64 位主机上高位地址会丢失；
 * 需从同一镜像中相邻的 .bss 符号重建该指针。
 */
static uint8_t* rx_dma_buffer(void)
{
    uintptr_t base = (uintptr_t) &mock_dma1_channel1;
    uintptr_t addr = (base & ~(uintptr_t) 0xFFFFFFFFULL) | (uintptr_t) DMA1_Channel1->CMAR;
    return (uint8_t*) addr;
}

/** 通过 RX DMA mock 送入八位字节并移动其写索引。 */
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

/* mock NVIC 的处理函数：它们在真实向量本应运行的位置执行。 */
static uint32_t s_irq_deliveries;
static uint32_t s_irq_brr;
static uint32_t s_irq_dma_en;
static uint32_t s_irq_cr1;
static uint32_t s_irq_cr3;
static const uint8_t s_injected_rx[] = {0x5AU, 0xA5U, 0x3CU};

/* 在中断被取走之前到达的数据：在硬件上，DMA 在接收八位字节时
 * 就将其写入环形缓冲区，随后 RTOF 挂起。 */
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
    }
    else {
        IOD_UART->ISR |= USART_ISR_RTOF;
    }
    phy_l6362a_raw_rx_irq();
    s_irq_deliveries++;
}

static void injected_tx_dma_irq(void)
{
    /* 在驱动处理之前快照该向量所观察到的状态。 */
    s_irq_brr = IOD_UART->BRR;
    s_irq_dma_en = DMA1_Channel2->CCR & DMA_CCR_EN;
    s_irq_cr3 = IOD_UART->CR3;
    /* 写入 IFCR 的 CTCIF2 会在硬件上清除 DMA ISR 标志。 */
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
    assert(s_phy->tx_busy != NULL);
    assert(s_phy->int_active != NULL);
    assert(s_phy->request_service != NULL);
    assert(s_phy->response_timer_start != NULL);
    assert(s_phy->response_timer_stop != NULL);
    assert(s_phy->detect_wakeup != NULL);
    assert(s_phy->get_faults == NULL);

    /* 收发器使能为高，发送使能释放。 */
    assert(mock_en_level == GPIO_PIN_SET);
    assert(mock_txen_level == GPIO_PIN_RESET);

    /* TIM3：1 us 节拍，单次更新中断。 */
    assert(TIM3->PSC == ((SYSCLOCK_FREQUENCY / 1000000U) - 1U));
    assert(TIM3->DIER == TIM_DIER_UIE);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);

    /* RX DMA：循环模式，每次请求一个字节，指向 RX 数据寄存器。 */
    assert(DMAMUX1_Channel0->CCR == DMA_REQUEST_USART2_RX);
    assert(DMA1_Channel1->CPAR == (uint32_t) (uintptr_t) &IOD_UART->RDR);
    assert(DMA1_Channel1->CMAR != 0U);
    assert(DMA1_Channel1->CNDTR == TEST_RX_DMA_SIZE);
    assert((DMA1_Channel1->CCR & (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN)) ==
           (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN));

    /* TX DMA：按帧配置，初始化后空闲。 */
    assert(DMAMUX1_Channel1->CCR == DMA_REQUEST_USART2_TX);
    assert(DMA1_Channel2->CCR == 0U);

    /* UART：9 位、偶校验、RX 超时、RX DMA 与错误中断。 */
    assert(IOD_UART->BRR == expected_brr(230400U));
    assert(IOD_UART->CR2 == USART_CR2_RTOEN);
    assert(IOD_UART->CR3 == (USART_CR3_EIE | USART_CR3_DMAR));
    assert(IOD_UART->CR1 == expected_cr1());
    assert(IOD_UART->ICR == (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF |
                             USART_ICR_TCCF | USART_ICR_RTOCF));
    assert(IOD_UART->RQR == USART_RQR_RXFRQ);

    /* UART 引脚被路由到 USART 复用功能。 */
    assert(mock_gpio_init_calls == 1U);
    assert(mock_gpio_init_pin == (UART_TX_Pin | UART_RX_Pin));
    assert(mock_gpio_init_alternate == IOD_UART_GPIO_AF);

    /* 板级启动时设置的中断优先级/顺序。 */
    assert(mock_nvic_priority[IOD_UART_IRQn] == 0U);
    assert(mock_nvic_priority[TIM3_IRQn] == 1U);
    assert(mock_nvic_priority[DMA1_Channel2_3_IRQn] == 2U);
    assert(mock_nvic_enabled[IOD_UART_IRQn] == 1U);
    assert(mock_nvic_enabled[TIM3_IRQn] == 1U);
    assert(mock_nvic_enabled[DMA1_Channel2_3_IRQn] == 1U);

    /* 定时器 A 的单次触发使用 13 Tbit 窗口。 */
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

    /* 正常帧：DMA 写入了四个八位字节，RTOF 界定其结束。 */
    rx_deliver(0U, frame_a, sizeof(frame_a));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    assert((IOD_UART->ICR & USART_ICR_RTOCF) != 0U);
    assert(s_phy->int_active(NULL) == 1U);
    assert(s_phy->detect_wakeup(NULL) == 1);
    assert(s_phy->int_active(NULL) == 1U); /* 唤醒检测不是读取 */

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

    /* 前一帧尚未读取时完成的帧会被丢弃。 */
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

    /* 帧期间出现接收错误会报告为 CHK，而非 DATA_READY。 */
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
    assert((chk1 - chk0) == 0U); /* CHK 计数器是 BYTE-RX 统计量 */
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

    /* 下一帧从循环缓冲区末尾附近开始并回绕。 */
    for (i = 4U; i < TEST_RX_DMA_SIZE; i++) {
        dma[i] = (uint8_t) (0x40U + (i & 0x3FU));
    }
    dma[0] = 0x30U;
    dma[1] = 0x31U;
    dma[2] = 0x32U;
    dma[3] = 0x33U;
    DMA1_Channel1->CNDTR = TEST_RX_DMA_SIZE - 3U; /* 写索引为 3 */

    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    frame = s_phy->rx_frame(NULL, &status, &len);
    assert(frame != NULL);
    assert(frame != dma); /* 已线性化到影子缓冲区 */
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

    /* 发送钩子只能从 ISR 泵运行。 */
    SCB->ICSR = SCB_ICSR_VECTACTIVE_Msk;

    assert(s_phy->tx_frame(NULL, NULL, 3U) == -1);
    assert(s_phy->tx_frame(NULL, reply, 0U) == -1);
    assert(s_phy->tx_frame(NULL, reply, 0x10000U) == -1);

    assert(s_phy->tx_frame(NULL, reply, sizeof(reply)) == 0);
    assert(mock_txen_level == GPIO_PIN_SET);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) == 0U); /* 不回采 echo */
    assert((IOD_UART->CR3 & USART_CR3_DMAT) != 0U);
    assert(DMA1_Channel2->CPAR == (uint32_t) (uintptr_t) &IOD_UART->TDR);
    assert(DMA1_Channel2->CMAR == (uint32_t) (uintptr_t) reply);
    assert(DMA1_Channel2->CNDTR == sizeof(reply));
    assert(DMA1_Channel2->CCR == (DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_EN));

    phy_l6362a_raw_get_counters(&tx1, &rx1, &chk1);
    assert((tx1 - tx0) == sizeof(reply));
    assert((rx1 - rx0) == 0U);
    assert((chk1 - chk0) == 0U);

    /* DMA 传输完成会释放 TX_EN 并重新武装接收器。 */
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

    /* 当 DMA 仍处于使能状态时，应答正在线上：新帧
     * 必须被拒绝，绝不静默拆掉正在进行的传输。 */
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

    /* 硬件已完成传输（非循环 DMA 会清除 EN），但完成
     * IRQ 仍排在这个更高优先级 ISR 之后挂起。下一帧
     * 必须完成挂起的应答（等待 TC、释放 TX_EN），然后
     * 干净地启动，而不是叠加在其之上。 */
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

    /* 模拟卡死的 UART：DMA 传输完成（TCIF）但 TC 始终不置位。
     * 有界等待必须在（自动推进的）mock 时钟上到期，并且仍然
     * 释放 TX_EN / 重新武装 RX，绝不在中断上下文中永久自旋。 */
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

    /* 正在传输的帧在 UART 重新配置之前被中止。 */
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

    /* SIO 不声明服务，并保持 RX 捕获处于武装状态。 */
    assert(s_phy->int_active(NULL) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);

    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SDCI);
    rx_deliver(0U, payload, sizeof(payload));
    IOD_UART->ISR = USART_ISR_RTOF;
    phy_l6362a_raw_rx_irq();
    assert(s_phy->int_active(NULL) == 1U);

    /* 重新进入 SIO 会冲刷陈旧帧，并丢弃在那里
     * 看到的字节；下一帧从当前写索引开始。 */
    s_phy->set_mode(NULL, IOLINK_PHY_MODE_SIO);
    assert(s_phy->int_active(NULL) == 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert(s_phy->rx_frame(NULL, &status, &len) == NULL || len == 0U);

    /* OL INT 锁存一次唤醒，且只被消费一次。 */
    assert(s_phy->detect_wakeup(NULL) == 0);
    phy_l6362a_raw_ol_irq();
    assert(s_phy->detect_wakeup(NULL) == 1);
    assert(s_phy->detect_wakeup(NULL) == 0);

    /* request_service 触发 USART IRQ，使 ISR 发送暂存的应答。 */
    mock_last_pending_irq = (IRQn_Type) -1;
    s_phy->request_service(NULL);
    assert(mock_last_pending_irq == IOD_UART_IRQn);

    /* 字节间隔更新只在 FRAME RX 模式下停止定时器。 */
    TIM3->SR = TIM_SR_UIF;
    TIM3->CR1 = TIM_CR1_CEN;
    phy_l6362a_raw_gap_irq();
    assert((TIM3->SR & TIM_SR_UIF) == 0U);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);
    assert(s_phy->int_active(NULL) == 0U);

    /* TC 被确认，但不产生帧。 */
    IOD_UART->ISR = USART_ISR_TC;
    IOD_UART->ICR = 0U;
    phy_l6362a_raw_rx_irq();
    assert((IOD_UART->ICR & USART_ICR_TCCF) != 0U);
    assert(s_phy->rx_frame(NULL, &status, &len) == NULL);

    /* OL_INT 从 GPIO mock 读取。 */
    mock_ol_pin_level = GPIO_PIN_RESET;
    assert(phy_l6362a_raw_int_line_active() == 0U);
    mock_ol_pin_level = GPIO_PIN_SET;
    assert(phy_l6362a_raw_int_line_active() == 1U);

    /* 诊断访问器可容忍 NULL 输出。 */
    phy_l6362a_raw_get_counters(NULL, NULL, NULL);
}

/*
 * ISR/主循环交错 1：主循环重新进入 SIO 时 RX RTOF 到达。
 * mock 在 UART 引脚重配置点投递它，可以内联
 * （中断开放）或在模式转换临界区结束时。
 * 正在进行的 TX FRAME 必须被中止，且该帧必须作为
 * 唤醒指示保留。
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
    /* 推迟到 UartConfigure() 之后：该向量看到了重新武装的 UART。 */
    assert(s_irq_cr1 == expected_cr1());
    /* UartConfigure() 清除了 RTOCF，因此该向量本身已无法
     * 发布该帧；必须由 setter 完成。 */
    assert((IOD_UART->ISR & USART_ISR_RTOF) == 0U);

    /* SIO 转换中止了正在进行的 FRAME 应答。 */
    assert(mock_txen_level == GPIO_PIN_RESET);
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAT) == 0U);
    assert((IOD_UART->CR3 & USART_CR3_DMAR) != 0U);

    /* SIO 不声明服务，但转换期间到达的帧
     * 被保留为唤醒指示。 */
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
 * ISR/主循环交错 2：主循环更改波特率时，TX DMA 完成
 * 处于挂起状态。它不得嵌套在临界区内；它在
 * 中止/重新配置之后运行，并观察到 DMA 已关闭的新 BRR。
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
    /* 已推迟：该向量在 UART 重新配置之后运行，且 DMA
     * 中止已经完成。 */
    assert(s_irq_brr == expected_brr(4800U));
    assert(s_irq_dma_en == 0U);
    assert((s_irq_cr3 & USART_CR3_DMAT) == 0U);
    assert(IOD_UART->BRR == expected_brr(4800U));
    assert((DMA1_Channel2->CCR & DMA_CCR_EN) == 0U);
    assert(mock_txen_level == GPIO_PIN_RESET);
}

/*
 * ISR/主循环交错 3：波特率更改期间 RX 帧到达。
 * 被推迟的向量仍必须发布完整帧，并保持新的
 * 波特率/UART 状态不变。
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
    /* 该标志已被 UartConfigure() 清除；由 setter 发布了该帧。 */
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

/* OL 锁存由 EXTI 向量在主上下文处于模式转换
 * 期间写入；它必须在转换后保留且只被消费一次。 */
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
    mock_stm32g0_reset();

    /* 由 SysTick 派生的微秒时基。 */
    mock_tick_ms = 5U;
    SysTick->LOAD = SystemCoreClock / 1000U;
    SysTick->VAL = 0U;
    assert(phy_l6362a_time_us() ==
           (5ULL * 1000ULL) + (uint64_t) (SysTick->LOAD / (SystemCoreClock / 1000000U)));
    assert(phy_l6362a_time_ms() == 6U);

    SysTick->VAL = SysTick->LOAD;
    assert(phy_l6362a_time_us() == 5000ULL);
    assert(phy_l6362a_time_ms() == 5U);

    /* 临界区可嵌套，并在深度归零时恢复保存的 PRIMASK。 */
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
    iolink_critical_exit(); /* 不平衡的退出不得下溢 */
    assert(mock_primask == 1U);
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
