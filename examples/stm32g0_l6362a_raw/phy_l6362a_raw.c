/**
  ******************************************************************************
  * @file    phy_l6362a_raw.c
  * @brief   iolinki raw-octet PHY adapter for the 8110 L6362A transceiver.
  *
  * Pin map (main.h):
  *    EN      PA0   transceiver enable (held high)
  *    TX      PA2   USART2_TX -> L6362A C/Q input (kept on the USART AF)
  *    RX      PA3   USART2_RX <- L6362A C/Q output
  *    TX_EN   PA4   transmit-enable, asserted only while sending
  *    OL_INT  PA5   overload / wake interrupt (falling edge)
  *
  * Raw-octet synthesis (the L6362A has no SPI status register):
  *    - rx_octet    : pop one octet from the USART2 RX ring (DATA_READY); when
  *                    the ring is empty and the byte-gap timer has expired,
  *                    deliver the frame-end indication (CHK).
  *    - tx_octet    : write one octet to USART2 (first write asserts TX_EN).
  *    - tx_done     : wait for the last octet, then release TX_EN.
  *    - int_active  : there is work when a frame-end is pending, a received
  *                    octet waits, or a transmission is ready for its next
  *                    octet.
  *
  * The library ISR pump (iolink_device_process_isr) is invoked from the USART2
  * IRQ, the byte-gap timer IRQ and the OL_INT EXTI.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "phy_l6362a_raw.h"
#include "main.h"
#include "iolinki/time_utils.h"
#include "iolinki/platform.h"

/* Private defines -----------------------------------------------------------*/

/* This board hardcodes the two-pump ISR transport over USART2 DMA. Assert the
 * contract so a future config.h/build edit cannot silently change it. */
#if !defined(IOLINK_PHY_FEATURE_ISR_PUMP)
#error "config.h must define IOLINK_PHY_FEATURE_ISR_PUMP"
#endif
_Static_assert(IOLINK_PHY_FEATURE_ISR_PUMP == 1, "8110 board requires the ISR pump");
_Static_assert(IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME, "8110 board uses RX FRAME DMA");
_Static_assert(IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME, "8110 board uses TX FRAME DMA");

/* The reply transmit hooks may only run from interrupt context (the ISR pump).
 * This catches a regression that calls them from the main loop. */
#define L6362_TX_ASSERT_ISR()                                                \
    do {                                                                     \
        if ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0U) {                   \
            Error_Handler();                                                 \
        }                                                                    \
    } while (0)

/** TX/RX bounded-wait budget per octet, in microseconds. */
#define L6362_OCTET_TIMEOUT_US (200ULL)

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/** Received-byte ring size (RX BYTE). */
#define L6362_RX_RING_SIZE   (512U)
#else
/** RX FRAME DMA buffer (circular). >= 2x the max frame so a completed frame is
  * not overwritten before the main pump copies it. */
#define L6362_RX_DMA_SIZE    (256U)
#endif

/* Private variables ---------------------------------------------------------*/

static iolink_baudrate_t s_baudrate;
static uint32_t          s_baud_hz;
static iolink_baudrate_t s_initial_baudrate = IOLINK_BAUDRATE_COM3;
static uint32_t          s_gap_us; /* frame-end byte-gap time-out (RX BYTE) */
static iolink_phy_mode_t s_mode;

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static volatile uint8_t  s_rx_ring[L6362_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint8_t  s_frame_end; /* byte-gap expired -> CHK */
#else
static uint8_t           s_rx_dma[L6362_RX_DMA_SIZE];
static uint8_t           s_rx_linear[L6362_RX_DMA_SIZE]; /* only on buffer wrap */
static volatile uint16_t s_rx_last_index; /**< DMA write index at the last IDLE */
static volatile uint8_t  s_rx_ready;      /**< a whole frame is waiting */
static volatile uint8_t  s_rx_err;        /**< USART error during the frame */
static const uint8_t*    s_rx_frame;      /**< the frame just completed */
static uint8_t           s_rx_frame_len;
#endif

static volatile uint8_t s_tx_in_progress;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static volatile uint8_t s_tx_tc_seen; /**< TC observed since the last octet write */
#endif
static volatile uint8_t s_wake_latched;

static uint32_t s_tx_bytes;
static uint32_t s_rx_bytes;
static uint32_t s_rx_chk;


/* Critical section state. */
static uint32_t s_crit_depth;
static uint32_t s_crit_primask;

/* Private function prototypes -----------------------------------------------*/
static int     l6362_init(void* user);
static void    l6362_set_mode(void* user, iolink_phy_mode_t mode);
static void    l6362_set_baudrate(void* user, iolink_baudrate_t baudrate);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int     l6362_rx_octet(void* user, uint8_t* data, uint8_t* status);
#else
static const uint8_t* l6362_rx_frame(void* user, uint8_t* status, uint8_t* len);
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
static int     l6362_tx_frame(void* user, const uint8_t* data, size_t len);
#else
static int     l6362_tx_octet(void* user, uint8_t data);
static void    l6362_tx_done(void* user);
#endif
static uint8_t l6362_int_active(void* user);
static void    l6362_request_service(void* user);
static int     l6362_detect_wakeup(void* user);
static void    phy_rx_set(bool on);
static bool    phy_txen_get(void);
static void    phy_txen_set(bool on);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
static void    RxPublishFrame(bool err);
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
static void    TxFrameComplete(void);
#endif

/* Private functions ---------------------------------------------------------*/

static void UartPinsAf(void) {
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin       = UART_TX_Pin | UART_RX_Pin;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = IOD_UART_GPIO_AF;
    HAL_GPIO_Init(UART_TX_GPIO_Port, &gpio);
}

static void UartConfigure(uint32_t baud) {
    IOD_UART->CR1 = 0U;
    UartPinsAf();
    IOD_UART->BRR = (SYSCLOCK_FREQUENCY + (baud / 2U)) / baud;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* RX by DMA: the receiver-timeout (RTO) interrupt delimits the frame. RTO
     * counts in bit times after the last stop bit; 1 bit (~4.3 us at 230.4k)
     * keeps t_A within 10 bit times, unlike IDLE (~11 bit times). */
    IOD_UART->RTOR = 1U;
    IOD_UART->CR2  = USART_CR2_RTOEN; /* one stop bit + receiver timeout */
#else
    IOD_UART->CR2 = 0U; /* one stop bit */
#endif
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    IOD_UART->CR3 = USART_CR3_EIE | USART_CR3_DMAR;
#else
    IOD_UART->CR3 = USART_CR3_EIE;
#endif
    IOD_UART->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF |
                    USART_ICR_TCCF | USART_ICR_RTOCF;
    IOD_UART->RQR = USART_RQR_RXFRQ;
    /* 9-bit word (8 data + even parity), TX+RX. */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* RX by DMA: RTOF delimits the frame (no per-octet RXNE, no IDLE wait). */
    IOD_UART->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE |
                    USART_CR1_RTOIE | USART_CR1_UE;
#else
    IOD_UART->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE |
                    USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE | USART_CR1_UE;
#endif
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/** Publish the DMA octets received since the previous frame end.
 *
 * Called from the RTOF vector and from the mode/baudrate setters. The setters
 * run inside a PRIMASK critical section and UartConfigure() clears RTOCF, so a
 * frame that completed while interrupts were masked would otherwise be lost when
 * the deferred RTOF vector finds the flag already clear. */
static void RxPublishFrame(bool err) {
    uint16_t cur = (uint16_t)(L6362_RX_DMA_SIZE - (DMA1_Channel1->CNDTR & 0xFFFFU));
    uint16_t last = s_rx_last_index;

    if (cur == last) {
        return;
    }
    s_rx_last_index = cur;
    if (s_rx_ready != 0U) {
        return; /* previous frame unread: drop this one */
    }
    if (cur > last) {
        s_rx_frame     = &s_rx_dma[last];
        s_rx_frame_len = (uint8_t)(cur - last);
    } else {
        /* Wrapped across the buffer end: linearise. */
        uint16_t n1 = (uint16_t)(L6362_RX_DMA_SIZE - last);
        uint16_t n2 = cur;
        uint16_t i;
        for (i = 0U; i < n1; i++) {
            s_rx_linear[i] = s_rx_dma[last + i];
        }
        for (i = 0U; i < n2; i++) {
            s_rx_linear[n1 + i] = s_rx_dma[i];
        }
        s_rx_frame     = s_rx_linear;
        s_rx_frame_len = (uint8_t)(n1 + n2);
    }
    s_rx_err   = (err) ? 1U : 0U;
    s_rx_ready = 1U;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static void RxReset(void) {
    s_rx_head = 0U;
    s_rx_tail = 0U;
}

static void RxPush(uint8_t byte) {
    uint16_t next = (uint16_t)((s_rx_head + 1U) % L6362_RX_RING_SIZE);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = byte;
        s_rx_head            = next;
    }
}

static int RxPop(uint8_t* byte) {
    if (s_rx_tail == s_rx_head) {
        return 0;
    }
    *byte     = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % L6362_RX_RING_SIZE);
    return 1;
}

/** Arm (restart) the one-shot byte-gap timer; the next received octet resets
  *  the window. Only meaningful in SDCI (the frame is delimited by the gap). */
static void GapTimerRestart(void) {
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CNT = 0U;
    TIM3->ARR = (s_gap_us != 0U) ? (s_gap_us - 1U) : 0U;
    TIM3->SR  = 0U;
    TIM3->CR1 |= TIM_CR1_CEN;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/* Raw-octet PHY API ---------------------------------------------------------*/

static int l6362_init(void* user) {
    (void)user;
    HAL_GPIO_WritePin(EN_GPIO_Port, EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TX_EN_GPIO_Port, TX_EN_Pin, GPIO_PIN_RESET);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    RxReset();
    s_frame_end = 0U;
#else
    s_rx_ready      = 0U;
    s_rx_last_index = 0U;
#endif
    s_tx_in_progress = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    s_tx_tc_seen     = 0U;
#endif
    s_wake_latched   = 0U;
    s_mode           = IOLINK_PHY_MODE_SIO;
    return 0;
}

static void l6362_set_mode(void* user, iolink_phy_mode_t mode) {
    (void)user;
    /* The mode transition reconfigures the UART and resets RX/TX state. Keep the
     * ISR feeds out until the new baseline is complete, like set_baudrate(). */
    iolink_critical_enter();
    s_mode = mode;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* The UART + DMA stay armed in both modes; the IDLE interrupt delimits frames
     * and also serves as the wake indicator. */
    if (mode == IOLINK_PHY_MODE_SIO) {
        /* Fresh baseline on (re)entering SIO so the next wake-up frame is
         * delimited cleanly and no stale reply/echo state survives a fallback. */
        s_rx_ready       = 0U;
        s_rx_last_index  = (uint16_t)(L6362_RX_DMA_SIZE - (DMA1_Channel1->CNDTR & 0xFFFFU));
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        /* int_active() claims no service in SIO: an in-flight FRAME reply
         * would never finish, so stop its DMA before releasing TX_EN. */
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        IOD_UART->CR3 &= ~USART_CR3_DMAT;
        IOD_UART->ICR = USART_ICR_TCCF;
        DMA1->IFCR = DMA_IFCR_CTCIF2;
#endif
        phy_txen_set(false);
    }
    UartConfigure(s_baud_hz);
    /* UartConfigure() cleared RTOCF inside this critical section. Publish a
     * frame that completed meanwhile so the deferred RTOF vector (which now
     * finds no flag) cannot make the wake indication disappear. */
    RxPublishFrame(false);
#else
    if (mode == IOLINK_PHY_MODE_SDCI) {
        s_frame_end = 0U;
        UartConfigure(s_baud_hz); /* (re)arm the receiver for SDCI */
        /* A frame may have been buffered while in SIO; arm the byte-gap timer so
     * it is completed (the next RX would otherwise never be able to close it). */
        if (s_rx_tail != s_rx_head) {
            GapTimerRestart();
        }
    } else if (mode == IOLINK_PHY_MODE_SIO) {
        /* Keep the UART listening so the master's wake-up / first octets are
         * captured; no frame delimiting until SDCI. */
        TIM3->CR1 &= ~TIM_CR1_CEN;
        /* int_active() claims no service in SIO, so an in-flight BYTE reply
         * can never finish: abort it and release TX_EN instead of holding the
         * line high forever. */
        IOD_UART->CR1 &= ~(USART_CR1_TCIE | USART_CR1_TXEIE_TXFNFIE);
        phy_txen_set(false);
        UartConfigure(s_baud_hz);
    } else {
        TIM3->CR1 &= ~TIM_CR1_CEN;
    }
#endif
    iolink_critical_exit();
}

static void l6362_set_baudrate(void* user, iolink_baudrate_t baudrate) {
    (void)user;
    uint32_t new_hz;
    switch (baudrate) {
    case IOLINK_BAUDRATE_COM1: new_hz = 4800U;
        break;
    case IOLINK_BAUDRATE_COM2: new_hz = 38400U;
        break;
    case IOLINK_BAUDRATE_COM3:
    default: new_hz = 230400U;
        break;
    }

    /* Changing the UART while a BYTE reply is in flight would clear TXEIE/TCIE
     * (UartConfigure writes CR1=0) and leave TX_EN asserted with no interrupt
     * left to drive the remaining octets. Abort the reply first; the DLL
     * abandons its matching ISR TX state. */
    iolink_critical_enter();
    if ((new_hz != s_baud_hz) && (s_tx_in_progress != 0U)) {
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        IOD_UART->CR1 &= ~(USART_CR1_TCIE | USART_CR1_TXEIE_TXFNFIE);
        IOD_UART->ICR = USART_ICR_TCCF;
        s_tx_tc_seen = 0U;
#else
        /* FRAME TX: stop the DMA before the UART is reconfigured. */
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        IOD_UART->CR3 &= ~USART_CR3_DMAT;
        IOD_UART->ICR = USART_ICR_TCCF;
        DMA1->IFCR = DMA_IFCR_CTCIF2;
#endif
        phy_txen_set(false);
    }
    s_baudrate = baudrate;
    s_baud_hz = new_hz;
    /* Frame-end time-out ~ 13 bit times (matches the ST single-octet engine). */
    s_gap_us = (13U * 1000000U) / s_baud_hz;
    UartConfigure(s_baud_hz);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* Same as in set_mode(): UartConfigure() cleared RTOCF, so a frame that
     * completed inside the critical section must be published here. A frame
     * captured at the previous baudrate is still handed to the DLL, which
     * validates it (CRC) and discards it if it does not decode. */
    RxPublishFrame(false);
#endif
    iolink_critical_exit();
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int l6362_rx_octet(void* user, uint8_t* data, uint8_t* status) {
    (void)user;
    uint8_t byte;

    /* Deliver pending octets first; the frame-end indication follows once the
   * ring is drained (so the accumulated frame is complete). */
    if (RxPop(&byte) != 0) {
        if (data != NULL) {
            *data = byte;
        }
        if (status != NULL) {
            *status = IOLINK_PHY_STATUS_DATA_READY;
        }
        s_rx_bytes++;
        return 1;
    }

    if (s_frame_end != 0U) {
        s_frame_end = 0U;
        if (status != NULL) {
            *status = IOLINK_PHY_STATUS_CHK;
        }
        s_rx_chk++;
        return 0;
    }

    if (status != NULL) {
        *status = 0U;
    }
    return 0;
}
#else
/** @brief Return the whole frame the DMA just completed (zero-copy). */
static const uint8_t* l6362_rx_frame(void* user, uint8_t* status, uint8_t* len) {
    (void)user;
    if (s_rx_ready == 0U) {
        if (status != NULL) {
            *status = 0U;
        }
        if (len != NULL) {
            *len = 0U;
        }
        return NULL;
    }
    s_rx_ready = 0U;
    s_rx_bytes += s_rx_frame_len;
    if (status != NULL) {
        *status = (s_rx_err != 0U) ? IOLINK_PHY_STATUS_CHK : IOLINK_PHY_STATUS_DATA_READY;
    }
    if (len != NULL) {
        *len = s_rx_frame_len;
    }
    return s_rx_frame;
}
#endif

/** Stop RX capture and discard the transceiver echo of our own transmission
 *  (FRAME RX only: the BYTE path has no DMA buffer to protect). */
static void TxSuppressEcho(void) {
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    IOD_UART->CR3 &= ~USART_CR3_DMAR;
#else
    /* BYTE RX has no DMA gate: mask the RX interrupt so the transceiver's echo
     * of our own transmission never enters the ring or restarts the gap timer. */
    IOD_UART->CR1 &= ~USART_CR1_RXNEIE_RXFNEIE;
#endif
    IOD_UART->RQR = USART_RQR_RXFRQ;
}

/** Discard any echo/overrun and re-arm RX capture after a reply. */
static void TxRestoreRx(void) {
    IOD_UART->ICR = USART_ICR_ORECF;
    IOD_UART->RQR = USART_RQR_RXFRQ;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    IOD_UART->CR3 |= USART_CR3_DMAR;
#else
    /* Drop any echo octets that slipped into the ring; the master cannot have
     * sent a new frame while we were driving C/Q. */
    RxReset();
    s_frame_end = 0U;
    IOD_UART->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
#endif
}

/* RX capture gate (setter): FRAME gates the DMA request, BYTE gates the RX
 * interrupt. Never call TxSuppressEcho/TxRestoreRx directly. */
static void phy_rx_set(bool on) {
    if (on) {
        TxRestoreRx();
    } else {
        TxSuppressEcho();
    }
}

/* TX state (getter). */
static bool phy_txen_get(void) {
    return (s_tx_in_progress != 0U);
}

/* Coupled TX_EN <-> RX gate: the single place that drives TX_EN. Asserting it
 * disables RX capture (no transceiver echo); releasing it re-arms RX. */
static void phy_txen_set(bool on) {
    if (on) {
        phy_rx_set(false);
        HAL_GPIO_WritePin(TX_EN_GPIO_Port, TX_EN_Pin, GPIO_PIN_SET);
        s_tx_in_progress = 1U;
    } else {
        HAL_GPIO_WritePin(TX_EN_GPIO_Port, TX_EN_Pin, GPIO_PIN_RESET);
        phy_rx_set(true);
        s_tx_in_progress = 0U;
    }
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static int l6362_tx_octet(void* user, uint8_t data) {
    (void)user;
    L6362_TX_ASSERT_ISR();

    if (s_tx_in_progress == 0U) {
        /* Frame start: assert TX_EN and stop RX capture together. */
        s_tx_tc_seen = 0U;
        IOD_UART->ICR = USART_ICR_TCCF; /* drop a stale TC from the last frame */
        IOD_UART->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        phy_txen_set(true);
    }

    /* Common case: TXE is already set, write immediately. Only pay for
     * iolink_time_get_us() (SysTick + HAL_GetTick + division) when TDR is
     * still full; it must not sit between TX_EN and the first start bit
     * (it cost ~5 us in BYTE TX; FRAME does not call it on this path). */
    if ((IOD_UART->ISR & USART_ISR_TXE_TXFNF) == 0U) {
        const uint64_t deadline = iolink_time_get_us() + L6362_OCTET_TIMEOUT_US;
        while (((IOD_UART->ISR & USART_ISR_TXE_TXFNF) == 0U) &&
               (iolink_time_get_us() < deadline)) {
        }
    }
    IOD_UART->TDR = data;
    /* Forget the previous octet's TC: from here on s_tx_tc_seen must describe
     * THIS octet only. A non-TC interrupt (RTOF/EXTI/TIM3) may pump the DLL and
     * reach tx_done() before this octet has left the shift register; a stale
     * flag would drop TX_EN mid-octet. */
    s_tx_tc_seen = 0U;
    IOD_UART->ICR = USART_ICR_TCCF;
    /* Feed the NEXT octet as soon as TDR is empty (TXE): the UART then chains
     * octets back-to-back with no inter-octet gap. Waiting for TC per octet
     * instead made the gap equal to the ISR latency and jittery (10 us median,
     * 40-50 us outliers at COM3). TC is awaited only by tx_done() for the last
     * octet, because TX_EN must not drop while it is still shifting. */
    IOD_UART->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    s_tx_bytes++;
    return 0;
}

static void l6362_tx_done(void* user) {
    (void)user;
    const uint64_t deadline = iolink_time_get_us() + L6362_OCTET_TIMEOUT_US;

    IOD_UART->CR1 &= ~(USART_CR1_TCIE | USART_CR1_TXEIE_TXFNFIE);
    /* Wait until the last octet has left the shift register: either the TC IRQ
     * already observed it (s_tx_tc_seen) or TC is still pending (poll). The
     * per-octet clear in tx_octet makes the flag unambiguous. */
    while ((s_tx_tc_seen == 0U) && ((IOD_UART->ISR & USART_ISR_TC) == 0U) &&
           (iolink_time_get_us() < deadline)) {
    }
    s_tx_tc_seen = 0U;
    IOD_UART->ICR = USART_ICR_TCCF; /* no stale TC for the next frame */
    phy_txen_set(false);
}
#else
/* TX FRAME: hand the whole reply to DMA1 channel 2 (USART2_TX). The transfer
 * complete interrupt releases TX_EN and re-arms RX capture. */
/** Finish a FRAME reply once its DMA transfer has ended.
 *
 * The non-circular DMA clears CCR.EN in hardware at the end of the transfer,
 * even when its completion interrupt is still pending behind a higher-priority
 * ISR. Clearing the flag here makes a later completion vector a no-op. */
static void TxFrameComplete(void) {
    uint64_t deadline;

    DMA1->IFCR = DMA_IFCR_CTCIF2;
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    IOD_UART->CR3 &= ~USART_CR3_DMAT;
    /* Wait for the last octet to leave the shift register. */
    deadline = iolink_time_get_us() + L6362_OCTET_TIMEOUT_US;
    while (((IOD_UART->ISR & USART_ISR_TC) == 0U) && (iolink_time_get_us() < deadline)) {
    }
    phy_txen_set(false);
}

static int l6362_tx_frame(void* user, const uint8_t* data, size_t len) {
    (void)user;
    L6362_TX_ASSERT_ISR();
    if ((data == NULL) || (len == 0U) || (len > 0xFFFFU)) {
        return -1;
    }
    /* A previous reply must be finished before the DMA is reprogrammed. If its
     * completion interrupt is pending behind this (higher-priority) ISR, the
     * hardware has already cleared EN: finish it here. If EN is still set, the
     * reply is on the wire - refuse the new one instead of tearing it down. */
    if (phy_txen_get()) {
        if ((DMA1_Channel2->CCR & DMA_CCR_EN) != 0U) {
            return -1;
        }
        TxFrameComplete();
    }

    /* Disable RX capture and drive C/Q together: no echo may slip in. */
    phy_txen_set(true);

    DMA1_Channel2->CCR   = 0U; /* disable while reconfiguring */
    DMA1_Channel2->CPAR  = (uint32_t)(uintptr_t)&IOD_UART->TDR;
    DMA1_Channel2->CMAR  = (uint32_t)(uintptr_t)data;
    DMA1_Channel2->CNDTR = (uint32_t)len;
    DMA1_Channel2->CCR   = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_EN;
    IOD_UART->CR3 |= USART_CR3_DMAT;
    s_tx_bytes += (uint32_t)len;
    return 0;
}
#endif

/** @brief Poke the USART2 interrupt so the ISR pump sends a staged reply. */
static void l6362_request_service(void* user) {
    (void)user;
    phy_l6362a_raw_request_pump();
}

/* Timer A hook: 13 Tbit one-shot (the BYTE byte-gap watchdog). */
static void l6362_response_timer_start(void* user) {
    (void)user;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CNT = 0U;
    TIM3->ARR = (s_gap_us != 0U) ? (s_gap_us - 1U) : 0U;
    TIM3->SR  = 0U;
    TIM3->CR1 |= TIM_CR1_CEN;
}

/** Stop Timer A when the reply starts (t_A window closed). */
static void l6362_response_timer_stop(void* user) {
    (void)user;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->SR  = 0U;
}

static uint8_t l6362_int_active(void* user) {
    (void)user;

    /* The M-sequence ISR pump only runs in SDCI; in SIO the wake-up path is
   * driven by detect_wakeup() from the main pump, so claim no service. */
    if (s_mode != IOLINK_PHY_MODE_SDCI) {
        return 0U;
    }
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    if (s_rx_ready != 0U) {
        return 1U;
    }
#else
    if (s_frame_end != 0U) {
        return 1U;
    }
    if (s_rx_tail != s_rx_head) {
        return 1U;
    }
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    if (phy_txen_get() &&
        ((IOD_UART->ISR & (USART_ISR_TXE_TXFNF | USART_ISR_TC)) != 0U)) {
        return 1U;
    }
#endif
    return 0U;
}

static int l6362_detect_wakeup(void* user) {
    (void)user;

    /* The L6362A pass-through delivers the master's wake-up / first octets on
   * USART2 but does not reliably pulse INT on this board, so UART activity is
   * the reliable wake indicator; the OL latch is kept as a secondary source. */
    if (s_wake_latched != 0U) {
        s_wake_latched = 0U;
        return 1;
    }
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return (s_rx_ready != 0U) ? 1 : 0;
#else
    return (s_rx_tail != s_rx_head) ? 1 : 0;
#endif
}

/* Platform hooks ------------------------------------------------------------*/

void iolink_critical_enter(void) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (s_crit_depth == 0U) {
        s_crit_primask = mask;
    }
    s_crit_depth++;
}

void iolink_critical_exit(void) {
    if (s_crit_depth != 0U) {
        s_crit_depth--;
        if (s_crit_depth == 0U) {
            __set_PRIMASK(s_crit_primask);
        }
    }
}
uint64_t iolink_time_get_us(void)
{
  /* Microsecond timestamp derived from the running SysTick (1 ms period,
   * configured by HAL_Init). The intra-millisecond count is read from
   * SysTick->VAL, giving ~1 us resolution without a dedicated timer (the
   * STM32G070 has no TIM2). */
  uint32_t load = SysTick->LOAD;
  uint32_t clk_per_us = SystemCoreClock / 1000000U;
  uint32_t tick0;
  uint32_t tick1;
  uint32_t elapsed;

  if (clk_per_us == 0U)
  {
    clk_per_us = 1U;
  }

  /* Re-read until the millisecond tick is stable across the sample. */
  do
  {
    tick0 = HAL_GetTick();
    elapsed = load - SysTick->VAL;
    tick1 = HAL_GetTick();
  } while (tick0 != tick1);

  return ((uint64_t)tick0 * 1000ULL) + (uint64_t)(elapsed / clk_per_us);
}
uint32_t iolink_time_get_ms(void) {
    return (uint32_t)(iolink_time_get_us() / 1000U);
}

int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len) {
    (void)offset;
    (void)data;
    (void)len;
    return -1;
}

int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len) {
    (void)offset;
    (void)data;
    (void)len;
    return -1;
}

/* Public functions ----------------------------------------------------------*/

const iolink_phy_api_t* phy_l6362a_raw_get(void) {
    static const iolink_phy_api_t api = {
        .user          = NULL,
        .init          = l6362_init,
        .set_mode      = l6362_set_mode,
        .set_baudrate  = l6362_set_baudrate,
        /* ISR-path PHY: the library owns framing/protocol and calls these. */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
        .rx_frame      = l6362_rx_frame,
#else
        .rx_octet      = l6362_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        .tx_frame      = l6362_tx_frame,
        .tx_done       = NULL,
#else
        .tx_octet      = l6362_tx_octet,
        .tx_done       = l6362_tx_done,
#endif
        .int_active       = l6362_int_active,
        .request_service  = l6362_request_service,
        /* Timer A: 13 Tbit one-shot t_A watchdog (both transports; the BYTE
         * gap uses the same TIM3, the FRAME path gets the t_A deadline). */
        .response_timer_start = l6362_response_timer_start,
        .response_timer_stop  = l6362_response_timer_stop,
        .detect_wakeup    = l6362_detect_wakeup,
        .get_faults    = NULL,
    };
    return &api;
}

void phy_l6362a_raw_request_pump(void) {
    NVIC_SetPendingIRQ(IOD_UART_IRQn);
}

uint8_t phy_l6362a_raw_int_line_active(void) {
    return (HAL_GPIO_ReadPin(OL_INT_GPIO_Port, OL_INT_Pin) != GPIO_PIN_RESET) ? 1U : 0U;
}

void phy_l6362a_raw_get_counters(uint32_t* tx_bytes, uint32_t* rx_bytes,
                                 uint32_t* rx_chk) {
    if (tx_bytes != NULL) { *tx_bytes = s_tx_bytes; }
    if (rx_bytes != NULL) { *rx_bytes = s_rx_bytes; }
    if (rx_chk != NULL) { *rx_chk = s_rx_chk; }
}
void phy_l6362a_raw_set_initial_baudrate(iolink_baudrate_t baudrate) {
    s_initial_baudrate = baudrate;
}

int phy_l6362a_raw_hw_init(void)
{
  /* The microsecond timebase is derived from SysTick (no dedicated timer: the
   * STM32G070 has no TIM2). */

  /* TIM3: Timer A (13 Tbit one-shot); in BYTE RX it is also the byte gap.
   * Priority 1: below USART2 in FRAME mode, equal in BYTE mode (no nesting). */
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->PSC  = (uint32_t)((SYSCLOCK_FREQUENCY / 1000000U) - 1U);
    TIM3->ARR  = 0xFFFFU;
    TIM3->EGR  = TIM_EGR_UG;
    TIM3->SR   = 0U;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1  = 0U;
    HAL_NVIC_SetPriority(TIM3_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
  /* DMA1 channel 1 <- USART2_RX: circular, one byte per request into s_rx_dma. */
    __HAL_RCC_DMA1_CLK_ENABLE(); /* DMAMUX1 shares the DMA1 clock gate */
    DMAMUX1_Channel0->CCR = DMA_REQUEST_USART2_RX;
    DMA1_Channel1->CPAR   = (uint32_t)(uintptr_t)&IOD_UART->RDR;
    DMA1_Channel1->CMAR   = (uint32_t)(uintptr_t)s_rx_dma;
    DMA1_Channel1->CNDTR  = (uint32_t)L6362_RX_DMA_SIZE;
    DMA1_Channel1->CCR    = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
  /* DMA1 channel 2 -> USART2_TX, one shot per reply frame. */
    __HAL_RCC_DMA1_CLK_ENABLE();
    DMAMUX1_Channel1->CCR = DMA_REQUEST_USART2_TX;
    DMA1_Channel2->CCR    = 0U;
    HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
#endif

    /* Board bring-up baudrate: the application selects the operating COM via
     * phy_l6362a_raw_set_initial_baudrate() before this call. */
    l6362_set_baudrate(NULL, s_initial_baudrate);
    (void)l6362_init(NULL);

    /* USART2 interrupt priority: same as TIM3 so the ISR pump is never preempted
   * by the byte-gap handler (they must not nest). */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    HAL_NVIC_SetPriority(IOD_UART_IRQn, 0U, 0U); /* above SysTick for t_A */
#else
    HAL_NVIC_SetPriority(IOD_UART_IRQn, 1U, 0U); /* same as TIM3, below SysTick */
#endif
    HAL_NVIC_EnableIRQ(IOD_UART_IRQn);
    return 0;
}

/* Interrupt feeds -----------------------------------------------------------*/

void phy_l6362a_raw_gap_irq(void) {
    if ((TIM3->SR & TIM_SR_UIF) != 0U) {
        TIM3->SR  = (uint32_t)~TIM_SR_UIF;
        TIM3->CR1 &= ~TIM_CR1_CEN; /* one-shot: stop after the update */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
        s_frame_end = 1U;
#endif
    }
}

/** @brief DMA1 channel-2 transfer complete: finish a TX FRAME reply. */
void phy_l6362a_raw_tx_dma_irq(void) {
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if ((DMA1->ISR & DMA_ISR_TCIF2) != 0U) {
        TxFrameComplete();
    }
#endif
}

void phy_l6362a_raw_rx_irq(void) {
    uint32_t status = IOD_UART->ISR;
    uint32_t errors = status & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);

    if ((status & USART_ISR_TC) != 0U) {
        IOD_UART->ICR = USART_ICR_TCCF;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        s_tx_tc_seen = 1U; /* the octet has left the shift register */
#endif
    }

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    if ((status & USART_ISR_RTOF) != 0U) {
        IOD_UART->ICR = USART_ICR_RTOCF;
        RxPublishFrame(errors != 0U);
    }
#else
    if ((status & USART_ISR_RXNE_RXFNE) != 0U) {
        uint8_t byte = (uint8_t)IOD_UART->RDR;
        /* While our own reply is on the line the transceiver echoes it back on
         * RX. Masking RXNEIE is not enough: this handler still runs for the
         * per-octet TC interrupt, so drain the echo octet here without pushing
         * it (otherwise the all-zero PD-in reply is parsed as the next master
         * frame and trips the CRC/fallback path). */
        if (!phy_txen_get() && (errors == 0U)) {
            RxPush(byte);
            if (s_mode == IOLINK_PHY_MODE_SDCI) {
                GapTimerRestart();
            }
        }
    }
#endif

    if (errors != 0U) {
        IOD_UART->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    }
}

void phy_l6362a_raw_ol_irq(void) {
    s_wake_latched = 1U;
}
