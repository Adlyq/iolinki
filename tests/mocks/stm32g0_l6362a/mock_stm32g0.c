/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file mock_stm32g0.c
 * @brief Host-side register/HAL mock for the L6362A raw-PHY unit test.
 */
#include "main.h"

#include <string.h>

USART_TypeDef mock_usart2;
DMA_TypeDef mock_dma1;
DMA_Channel_TypeDef mock_dma1_channel1;
DMA_Channel_TypeDef mock_dma1_channel2;
DMAMUX_Channel_TypeDef mock_dmamux_channel0;
DMAMUX_Channel_TypeDef mock_dmamux_channel1;
TIM_TypeDef mock_tim3;
SysTick_Type mock_systick;
SCB_Type mock_scb;
GPIO_TypeDef mock_gpio_a;

uint32_t mock_tick_ms;
uint32_t mock_tick_auto_advance;
static uint32_t s_tick_calls;
uint32_t mock_primask;
GPIO_PinState mock_en_level = GPIO_PIN_RESET;
GPIO_PinState mock_txen_level = GPIO_PIN_RESET;
GPIO_PinState mock_ol_pin_level = GPIO_PIN_SET;
uint32_t mock_gpio_init_calls;
uint32_t mock_gpio_init_pin;
uint32_t mock_gpio_init_alternate;
uint32_t mock_nvic_priority[64];
uint8_t mock_nvic_enabled[64];
IRQn_Type mock_last_pending_irq;
uint32_t mock_error_handler_calls;

static mock_irq_fn s_irq_handler[MOCK_IRQ_KIND_COUNT];
static mock_irq_fn s_inject_prepare;
static uint32_t s_irq_pending;
static mock_trigger_t s_inject_trigger;
static mock_irq_t s_inject_irq;

static void deliver_pending_irqs(void)
{
    for (uint32_t kind = (uint32_t) MOCK_IRQ_NONE + 1U; kind < (uint32_t) MOCK_IRQ_KIND_COUNT;
         kind++) {
        const uint32_t bit = (1U << kind);
        if ((s_irq_pending & bit) != 0U) {
            s_irq_pending &= ~bit;
            if (s_irq_handler[kind] != NULL) {
                s_irq_handler[kind]();
            }
        }
    }
}

static void run_injection_trigger(mock_trigger_t trigger)
{
    if (s_inject_trigger != trigger) {
        return;
    }
    const mock_irq_t irq = s_inject_irq;
    s_inject_trigger = MOCK_TRIGGER_NONE;
    s_inject_irq = MOCK_IRQ_NONE;
    if (s_inject_prepare != NULL) {
        s_inject_prepare();
    }
    mock_request_irq(irq);
}

void mock_set_irq_handler(mock_irq_t irq, mock_irq_fn fn)
{
    if ((irq > MOCK_IRQ_NONE) && (irq < MOCK_IRQ_KIND_COUNT)) {
        s_irq_handler[irq] = fn;
    }
}

void mock_set_inject_prepare(mock_irq_fn fn)
{
    s_inject_prepare = fn;
}

void mock_inject_irq(mock_trigger_t trigger, mock_irq_t irq)
{
    s_inject_trigger = trigger;
    s_inject_irq = irq;
}

void mock_request_irq(mock_irq_t irq)
{
    if ((irq == MOCK_IRQ_NONE) || (irq >= MOCK_IRQ_KIND_COUNT)) {
        return;
    }
    if (mock_primask != 0U) {
        s_irq_pending |= (1U << (uint32_t) irq);
        return;
    }
    if (s_irq_handler[irq] != NULL) {
        s_irq_handler[irq]();
    }
}

void mock_stm32g0_reset(void)
{
    memset(&mock_usart2, 0, sizeof(mock_usart2));
    memset(&mock_dma1, 0, sizeof(mock_dma1));
    memset(&mock_dma1_channel1, 0, sizeof(mock_dma1_channel1));
    memset(&mock_dma1_channel2, 0, sizeof(mock_dma1_channel2));
    memset(&mock_dmamux_channel0, 0, sizeof(mock_dmamux_channel0));
    memset(&mock_dmamux_channel1, 0, sizeof(mock_dmamux_channel1));
    memset(&mock_tim3, 0, sizeof(mock_tim3));
    memset(&mock_scb, 0, sizeof(mock_scb));
    memset(mock_nvic_priority, 0, sizeof(mock_nvic_priority));
    memset(mock_nvic_enabled, 0, sizeof(mock_nvic_enabled));
    memset(s_irq_handler, 0, sizeof(s_irq_handler));
    s_inject_prepare = NULL;
    s_irq_pending = 0U;
    s_inject_trigger = MOCK_TRIGGER_NONE;
    s_inject_irq = MOCK_IRQ_NONE;

    mock_systick.LOAD = SystemCoreClock / 1000U;
    mock_systick.VAL = mock_systick.LOAD;

    mock_tick_ms = 0U;
    mock_tick_auto_advance = 0U;
    s_tick_calls = 0U;
    mock_primask = 0U;
    mock_en_level = GPIO_PIN_RESET;
    mock_txen_level = GPIO_PIN_RESET;
    mock_ol_pin_level = GPIO_PIN_SET;
    mock_gpio_init_calls = 0U;
    mock_gpio_init_pin = 0U;
    mock_gpio_init_alternate = 0U;
    mock_last_pending_irq = (IRQn_Type) -1;
    mock_error_handler_calls = 0U;
}

void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init)
{
    (void) port;
    mock_gpio_init_calls++;
    mock_gpio_init_pin = init->Pin;
    mock_gpio_init_alternate = init->Alternate;
    run_injection_trigger(MOCK_TRIGGER_GPIO_INIT);
}

void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    (void) port;
    if (pin == EN_Pin) {
        mock_en_level = state;
    }
    else if (pin == TX_EN_Pin) {
        mock_txen_level = state;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin)
{
    (void) port;
    (void) pin;
    return mock_ol_pin_level;
}

void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t priority, uint32_t subpriority)
{
    (void) subpriority;
    if ((irq >= 0) && (irq < (IRQn_Type) 64)) {
        mock_nvic_priority[irq] = priority;
    }
}

void HAL_NVIC_EnableIRQ(IRQn_Type irq)
{
    if ((irq >= 0) && (irq < (IRQn_Type) 64)) {
        mock_nvic_enabled[irq] = 1U;
    }
}

void NVIC_SetPendingIRQ(IRQn_Type irq)
{
    mock_last_pending_irq = irq;
}

uint32_t HAL_GetTick(void)
{
    if (mock_tick_auto_advance != 0U) {
        /* Leave the two reads inside one iolink_time_get_us() sample coherent
         * (they compare equal) but let each successive sample observe a later
         * millisecond, so a driver's bounded wait can actually expire. */
        const uint32_t value = mock_tick_ms + (s_tick_calls / 2U);
        s_tick_calls++;
        return value;
    }
    return mock_tick_ms;
}

void Error_Handler(void)
{
    mock_error_handler_calls++;
}

uint32_t mock_get_primask(void)
{
    return mock_primask;
}

void mock_disable_irq(void)
{
    mock_primask = 1U;
}

void mock_set_primask(uint32_t value)
{
    mock_primask = value;
    if (value == 0U) {
        run_injection_trigger(MOCK_TRIGGER_UNMASK);
        deliver_pending_irqs();
    }
}
