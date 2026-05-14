/******************************************************************************
 *  File:       hw_can_mocks.h
 *  Author:     timothy vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_can module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_CAN_MOCKS_H
#define HW_CAN_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN
/**-----------------------------------------------------------------------------
 * Includes
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 * Public Defines / Macros
 *----------------------------------------------------------------------------*/

#define HAL_OK     (0)
#define HAL_ERROR  (1)

#define ENABLE     (1U)
#define DISABLE    (0U)

#define CAN_PACKET_SIZE (8U)

/* CAN modes */
#define CAN_MODE_NORMAL    (0U)
#define CAN_MODE_LOOPBACK  (1U)

/* SJW */
#define CAN_SJW_1TQ        (1U)

/* BS1 */
#define CAN_BS1_1TQ   (1U)
#define CAN_BS1_2TQ   (2U)
#define CAN_BS1_3TQ   (3U)
#define CAN_BS1_4TQ   (4U)
#define CAN_BS1_5TQ   (5U)
#define CAN_BS1_6TQ   (6U)
#define CAN_BS1_7TQ   (7U)
#define CAN_BS1_8TQ   (8U)
#define CAN_BS1_9TQ   (9U)
#define CAN_BS1_10TQ  (10U)
#define CAN_BS1_11TQ  (11U)
#define CAN_BS1_12TQ  (12U)
#define CAN_BS1_13TQ  (13U)
#define CAN_BS1_14TQ  (14U)
#define CAN_BS1_15TQ  (15U)
#define CAN_BS1_16TQ  (16U)

/* BS2 */
#define CAN_BS2_1TQ   (1U)
#define CAN_BS2_2TQ   (2U)
#define CAN_BS2_3TQ   (3U)
#define CAN_BS2_4TQ   (4U)
#define CAN_BS2_5TQ   (5U)
#define CAN_BS2_6TQ   (6U)
#define CAN_BS2_7TQ   (7U)
#define CAN_BS2_8TQ   (8U)

/* Filters */
#define CAN_FILTERMODE_IDMASK   (0U)
#define CAN_FILTERSCALE_32BIT   (0U)
#define CAN_FILTER_FIFO0        (0U)

/* Notifications */
#define CAN_IT_RX_FIFO0_MSG_PENDING   (1U << 0)
#define CAN_IT_TX_MAILBOX_EMPTY       (1U << 1)

/* Register bits */
#define CAN_TSR_TME0      (1U << 26)
#define CAN_TI0R_TXRQ     (1U << 0)
#define CAN_RF0R_FMP0     (0x3U)
#define CAN_RF0R_RFOM0    (1U << 5)
#define CAN_IER_TMEIE     (1U << 0)
#define CAN_TSR_RQCP0     (1U << 0)

/* Helpers */
#define SET_BIT(REG, BIT)    ((REG) |= (BIT))
#define CLEAR_BIT(REG, BIT)  ((REG) &= ~(BIT))

/* RCC macros */
#define __HAL_RCC_CAN1_FORCE_RESET()
#define __HAL_RCC_CAN1_RELEASE_RESET()
#define __HAL_RCC_CAN1_CLK_ENABLE()

/**-----------------------------------------------------------------------------
 * Public Typedefs / Structures
 *----------------------------------------------------------------------------*/

typedef int HAL_StatusTypeDef;

typedef struct
{
    uint32_t TIR;
    uint32_t TDTR;
    uint32_t TDLR;
    uint32_t TDHR;
} CAN_TxMailBox_TypeDef;

typedef struct
{
    uint32_t RIR;
    uint32_t RDTR;
    uint32_t RDLR;
    uint32_t RDHR;
} CAN_FIFOMailBox_TypeDef;

typedef struct
{
    uint32_t MCR;
    uint32_t MSR;
    uint32_t TSR;
    uint32_t RF0R;
    uint32_t RF1R;
    uint32_t IER;

    CAN_TxMailBox_TypeDef   sTxMailBox[3];
    CAN_FIFOMailBox_TypeDef sFIFOMailBox[2];

} CAN_TypeDef;

typedef struct
{
    uint32_t Prescaler;
    uint32_t Mode;
    uint32_t SyncJumpWidth;
    uint32_t TimeSeg1;
    uint32_t TimeSeg2;

    uint32_t TimeTriggeredMode;
    uint32_t AutoBusOff;
    uint32_t AutoWakeUp;
    uint32_t AutoRetransmission;
    uint32_t ReceiveFifoLocked;
    uint32_t TransmitFifoPriority;

} CAN_InitTypeDef;

typedef struct
{
    CAN_TypeDef* Instance;
    CAN_InitTypeDef Init;

} CAN_HandleTypeDef;

typedef struct
{
    uint32_t FilterBank;
    uint32_t FilterMode;
    uint32_t FilterScale;

    uint32_t FilterIdHigh;
    uint32_t FilterIdLow;

    uint32_t FilterMaskIdHigh;
    uint32_t FilterMaskIdLow;

    uint32_t FilterFIFOAssignment;
    uint32_t FilterActivation;

} CAN_FilterTypeDef;

/**-----------------------------------------------------------------------------
 * Public Variables
 *----------------------------------------------------------------------------*/

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

extern CAN_TypeDef mock_can1_regs;
extern CAN_TypeDef mock_can2_regs;

#define CAN1 (&mock_can1_regs)
#define CAN2 (&mock_can2_regs)

/**-----------------------------------------------------------------------------
 * Public Function Prototypes
 *----------------------------------------------------------------------------*/

HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef* hcan);

HAL_StatusTypeDef HAL_CAN_ConfigFilter(
    CAN_HandleTypeDef* hcan,
    CAN_FilterTypeDef* filter);

HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef* hcan);

HAL_StatusTypeDef HAL_CAN_ActivateNotification(
    CAN_HandleTypeDef* hcan,
    uint32_t notifications);

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
