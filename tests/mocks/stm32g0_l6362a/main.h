/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.h
 * @brief Host-side STM32G0/CubeMX mock for the L6362A raw-PHY unit test.
 *
 * The example source includes the generated "main.h" for its register and
 * HAL surface. This header provides the smallest faithful subset needed to
 * compile it on the host, plus the state the test inspects.
 */
#ifndef MOCK_STM32G0_MAIN_H
#define MOCK_STM32G0_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t BRR;
    volatile uint32_t RTOR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t RQR;
    volatile uint32_t TDR;
    volatile uint32_t RDR;
} USART_TypeDef;

typedef struct
{
    volatile uint32_t CCR;
} DMAMUX_Channel_TypeDef;

typedef struct
{
    volatile uint32_t ISR;
    volatile uint32_t IFCR;
} DMA_TypeDef;

typedef struct
{
    volatile uint32_t CCR;
    volatile uint32_t CNDTR;
    volatile uint32_t CPAR;
    volatile uint32_t CMAR;
} DMA_Channel_TypeDef;

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
    volatile uint32_t ARR;
} TIM_TypeDef;

typedef struct
{
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
} SysTick_Type;

typedef struct
{
    volatile uint32_t ICSR;
} SCB_Type;

typedef struct
{
    int unused;
} GPIO_TypeDef;

typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET = 1
} GPIO_PinState;

typedef int32_t IRQn_Type;

/* Clock / core -------------------------------------------------------------- */
#define SYSCLOCK_FREQUENCY 64000000U
#define SystemCoreClock SYSCLOCK_FREQUENCY

/* GPIO ---------------------------------------------------------------------- */
#define GPIO_MODE_AF_PP 0x00000002U
#define GPIO_PULLUP 0x00000001U
#define GPIO_SPEED_FREQ_HIGH 0x00000003U

#define UART_TX_Pin ((uint16_t) 0x0004U) /* PA2 */
#define UART_RX_Pin ((uint16_t) 0x0008U) /* PA3 */
#define EN_Pin ((uint16_t) 0x0001U)      /* PA0 */
#define TX_EN_Pin ((uint16_t) 0x0010U)   /* PA4 */
#define OL_INT_Pin ((uint16_t) 0x0020U)  /* PA5 */
#define IOD_UART_GPIO_AF 1U

extern GPIO_TypeDef mock_gpio_a;
#define EN_GPIO_Port (&mock_gpio_a)
#define TX_EN_GPIO_Port (&mock_gpio_a)
#define OL_INT_GPIO_Port (&mock_gpio_a)
#define UART_TX_GPIO_Port (&mock_gpio_a)
#define UART_RX_GPIO_Port (&mock_gpio_a)

/* USART --------------------------------------------------------------------- */
extern USART_TypeDef mock_usart2;
#define IOD_UART (&mock_usart2)
#define IOD_UART_IRQn ((IRQn_Type) 37)

#define USART_CR1_UE (1U << 0)
#define USART_CR1_RE (1U << 2)
#define USART_CR1_TE (1U << 3)
#define USART_CR1_RXNEIE_RXFNEIE (1U << 5)
#define USART_CR1_TCIE (1U << 6)
#define USART_CR1_TXEIE_TXFNFIE (1U << 7)
#define USART_CR1_PEIE (1U << 8)
#define USART_CR1_PCE (1U << 10)
#define USART_CR1_M0 (1U << 12)
#define USART_CR1_RTOIE (1U << 26)

#define USART_CR2_RTOEN (1U << 23)

#define USART_CR3_EIE (1U << 0)
#define USART_CR3_DMAR (1U << 6)
#define USART_CR3_DMAT (1U << 7)

#define USART_ISR_PE (1U << 0)
#define USART_ISR_FE (1U << 1)
#define USART_ISR_NE (1U << 2)
#define USART_ISR_ORE (1U << 3)
#define USART_ISR_RXNE_RXFNE (1U << 5)
#define USART_ISR_TC (1U << 6)
#define USART_ISR_TXE_TXFNF (1U << 7)
#define USART_ISR_RTOF (1U << 11)

#define USART_ICR_PECF (1U << 1)
#define USART_ICR_FECF (1U << 2)
#define USART_ICR_NECF (1U << 3)
#define USART_ICR_ORECF (1U << 4)
#define USART_ICR_TCCF (1U << 6)
#define USART_ICR_RTOCF (1U << 11)

#define USART_RQR_RXFRQ (1U << 3)

/* DMA ----------------------------------------------------------------------- */
extern DMA_TypeDef mock_dma1;
extern DMA_Channel_TypeDef mock_dma1_channel1;
extern DMA_Channel_TypeDef mock_dma1_channel2;
extern DMAMUX_Channel_TypeDef mock_dmamux_channel0;
extern DMAMUX_Channel_TypeDef mock_dmamux_channel1;

#define DMA1 (&mock_dma1)
#define DMA1_Channel1 (&mock_dma1_channel1)
#define DMA1_Channel2 (&mock_dma1_channel2)
#define DMAMUX1_Channel0 (&mock_dmamux_channel0)
#define DMAMUX1_Channel1 (&mock_dmamux_channel1)

#define DMA_CCR_EN (1U << 0)
#define DMA_CCR_TCIE (1U << 1)
#define DMA_CCR_DIR (1U << 4)
#define DMA_CCR_CIRC (1U << 5)
#define DMA_CCR_MINC (1U << 10)

#define DMA_ISR_TCIF2 (1U << 4)
#define DMA_IFCR_CTCIF2 (1U << 4)

#define DMA_REQUEST_USART2_RX 1U
#define DMA_REQUEST_USART2_TX 2U

#define DMA1_Channel2_3_IRQn ((IRQn_Type) 11)

/* TIM ----------------------------------------------------------------------- */
extern TIM_TypeDef mock_tim3;
#define TIM3 (&mock_tim3)
#define TIM3_IRQn ((IRQn_Type) 16)

#define TIM_CR1_CEN (1U << 0)
#define TIM_DIER_UIE (1U << 0)
#define TIM_SR_UIF (1U << 0)
#define TIM_EGR_UG (1U << 0)

/* Core ---------------------------------------------------------------------- */
extern SysTick_Type mock_systick;
extern SCB_Type mock_scb;
#define SysTick (&mock_systick)
#define SCB (&mock_scb)
#define SCB_ICSR_VECTACTIVE_Msk 0x000001FFU

#define __HAL_RCC_TIM3_CLK_ENABLE() ((void) 0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void) 0)

/* HAL / NVIC surface -------------------------------------------------------- */
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void NVIC_SetPendingIRQ(IRQn_Type irq);
uint32_t HAL_GetTick(void);
void Error_Handler(void);

uint32_t mock_get_primask(void);
void mock_disable_irq(void);
void mock_set_primask(uint32_t value);
#define __get_PRIMASK() mock_get_primask()
#define __disable_irq() mock_disable_irq()
#define __set_PRIMASK(v) mock_set_primask(v)

/* Test-visible mock state --------------------------------------------------- */
extern uint32_t mock_tick_ms;
extern uint32_t mock_primask;
extern GPIO_PinState mock_en_level;
extern GPIO_PinState mock_txen_level;
extern GPIO_PinState mock_ol_pin_level;
extern uint32_t mock_gpio_init_calls;
extern uint32_t mock_gpio_init_pin;
extern uint32_t mock_gpio_init_alternate;
extern uint32_t mock_nvic_priority[64];
extern uint8_t mock_nvic_enabled[64];
extern IRQn_Type mock_last_pending_irq;
extern uint32_t mock_error_handler_calls;

/*
 * Minimal NVIC/PRIMASK model for ISR-vs-main interleaving tests.
 *
 * A test arms an injection trigger and registers the handler that the real
 * interrupt vector would run. When the trigger fires, the request is delivered
 * immediately if PRIMASK is clear, otherwise it stays pending until the driver
 * unmasks interrupts. This models an interrupt arriving in the middle of a
 * critical section without needing host threads.
 */
typedef enum
{
    MOCK_IRQ_NONE = 0,
    MOCK_IRQ_RX,
    MOCK_IRQ_TX_DMA,
    MOCK_IRQ_OL,
    MOCK_IRQ_GAP,
    MOCK_IRQ_KIND_COUNT
} mock_irq_t;

typedef enum
{
    MOCK_TRIGGER_NONE = 0,
    MOCK_TRIGGER_GPIO_INIT,
    MOCK_TRIGGER_UNMASK
} mock_trigger_t;

typedef void (*mock_irq_fn)(void);

void mock_set_irq_handler(mock_irq_t irq, mock_irq_fn fn);
void mock_inject_irq(mock_trigger_t trigger, mock_irq_t irq);
void mock_request_irq(mock_irq_t irq);

void mock_stm32g0_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_STM32G0_MAIN_H */
