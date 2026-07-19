/******************************************************************************
 *  File:       hw_can_mocks.h
 *  Author:     HIL-RIG Firmware Team
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Minimal STM32 HAL/CMSIS model used by hw_can unit tests.
 *
 *      The helpers below emulate command and write-one-to-clear register
 *      semantics. Treating TSR, RF0R and MSR as ordinary RAM would make unsafe
 *      read-modify-write production code appear correct in host tests.
 ******************************************************************************/

#ifndef HW_CAN_MOCKS_H
#define HW_CAN_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  HAL and CMSIS Constants
 *------------------------------------------------------------------------------
 */

#define HAL_OK 0
#define HAL_ERROR 1

#define ENABLE 1U
#define DISABLE 0U

#define CAN_MODE_NORMAL 0x00000000U
#define CAN_MODE_LOOPBACK 0x40000000U
#define CAN_MODE_SILENT 0x80000000U
#define CAN_MODE_SILENT_LOOPBACK 0xC0000000U

#define CAN_SJW_1TQ 0x00000000U
#define CAN_SJW_2TQ 0x01000000U
#define CAN_SJW_3TQ 0x02000000U
#define CAN_SJW_4TQ 0x03000000U

#define CAN_BS1_1TQ 0x00000000U
#define CAN_BS1_2TQ 0x00010000U
#define CAN_BS1_3TQ 0x00020000U
#define CAN_BS1_4TQ 0x00030000U
#define CAN_BS1_5TQ 0x00040000U
#define CAN_BS1_6TQ 0x00050000U
#define CAN_BS1_7TQ 0x00060000U
#define CAN_BS1_8TQ 0x00070000U
#define CAN_BS1_9TQ 0x00080000U
#define CAN_BS1_10TQ 0x00090000U
#define CAN_BS1_11TQ 0x000A0000U
#define CAN_BS1_12TQ 0x000B0000U
#define CAN_BS1_13TQ 0x000C0000U
#define CAN_BS1_14TQ 0x000D0000U
#define CAN_BS1_15TQ 0x000E0000U
#define CAN_BS1_16TQ 0x000F0000U

#define CAN_BS2_1TQ 0x00000000U
#define CAN_BS2_2TQ 0x00100000U
#define CAN_BS2_3TQ 0x00200000U
#define CAN_BS2_4TQ 0x00300000U
#define CAN_BS2_5TQ 0x00400000U
#define CAN_BS2_6TQ 0x00500000U
#define CAN_BS2_7TQ 0x00600000U
#define CAN_BS2_8TQ 0x00700000U

#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERMODE_IDLIST 1U
#define CAN_FILTERSCALE_32BIT 1U
#define CAN_FILTER_FIFO0 0U

#define CAN_TSR_RQCP0 ( 1U << 0U )
#define CAN_TSR_TXOK0 ( 1U << 1U )
#define CAN_TSR_ALST0 ( 1U << 2U )
#define CAN_TSR_TERR0 ( 1U << 3U )
#define CAN_TSR_ABRQ0 ( 1U << 7U )
#define CAN_TSR_RQCP1 ( 1U << 8U )
#define CAN_TSR_TXOK1 ( 1U << 9U )
#define CAN_TSR_ALST1 ( 1U << 10U )
#define CAN_TSR_TERR1 ( 1U << 11U )
#define CAN_TSR_ABRQ1 ( 1U << 15U )
#define CAN_TSR_RQCP2 ( 1U << 16U )
#define CAN_TSR_TXOK2 ( 1U << 17U )
#define CAN_TSR_ALST2 ( 1U << 18U )
#define CAN_TSR_TERR2 ( 1U << 19U )
#define CAN_TSR_ABRQ2 ( 1U << 23U )
#define CAN_TSR_TME0 ( 1U << 26U )
#define CAN_TSR_TME1 ( 1U << 27U )
#define CAN_TSR_TME2 ( 1U << 28U )

#define CAN_TI0R_TXRQ ( 1U << 0U )
#define CAN_TI0R_RTR ( 1U << 1U )
#define CAN_TI0R_IDE ( 1U << 2U )
#define CAN_TI0R_EXID_Pos 3U
#define CAN_TI0R_STID_Pos 21U

#define CAN_RI0R_RTR CAN_TI0R_RTR
#define CAN_RI0R_IDE CAN_TI0R_IDE
#define CAN_RI0R_EXID_Pos CAN_TI0R_EXID_Pos
#define CAN_RI0R_STID_Pos CAN_TI0R_STID_Pos

#define CAN_RDT0R_DLC_Pos 0U
#define CAN_RDT0R_DLC ( 0xFU << CAN_RDT0R_DLC_Pos )
#define CAN_RDT0R_FMI_Pos 8U
#define CAN_RDT0R_FMI ( 0xFFU << CAN_RDT0R_FMI_Pos )
#define CAN_RDT0R_TIME_Pos 16U
#define CAN_RDT0R_TIME ( 0xFFFFU << CAN_RDT0R_TIME_Pos )

#define CAN_RF0R_FMP0 0x3U
#define CAN_RF0R_FULL0 ( 1U << 3U )
#define CAN_RF0R_FOVR0 ( 1U << 4U )
#define CAN_RF0R_RFOM0 ( 1U << 5U )

#define CAN_IER_TMEIE ( 1U << 0U )
#define CAN_IER_FMPIE0 ( 1U << 1U )
#define CAN_IER_FFIE0 ( 1U << 2U )
#define CAN_IER_FOVIE0 ( 1U << 3U )
#define CAN_IER_EWGIE ( 1U << 8U )
#define CAN_IER_EPVIE ( 1U << 9U )
#define CAN_IER_BOFIE ( 1U << 10U )
#define CAN_IER_LECIE ( 1U << 11U )
#define CAN_IER_ERRIE ( 1U << 15U )

#define CAN_IT_TX_MAILBOX_EMPTY CAN_IER_TMEIE
#define CAN_IT_RX_FIFO0_MSG_PENDING CAN_IER_FMPIE0
#define CAN_IT_RX_FIFO0_FULL CAN_IER_FFIE0
#define CAN_IT_RX_FIFO0_OVERRUN CAN_IER_FOVIE0
#define CAN_IT_ERROR_WARNING CAN_IER_EWGIE
#define CAN_IT_ERROR_PASSIVE CAN_IER_EPVIE
#define CAN_IT_BUSOFF CAN_IER_BOFIE
#define CAN_IT_LAST_ERROR_CODE CAN_IER_LECIE
#define CAN_IT_ERROR CAN_IER_ERRIE

#define CAN_MSR_ERRI ( 1U << 2U )

#define CAN_ESR_EWGF ( 1U << 0U )
#define CAN_ESR_EPVF ( 1U << 1U )
#define CAN_ESR_BOFF ( 1U << 2U )
#define CAN_ESR_LEC_Pos 4U
#define CAN_ESR_LEC ( 0x7U << CAN_ESR_LEC_Pos )
#define CAN_ESR_TEC_Pos 16U
#define CAN_ESR_REC_Pos 24U

#define SET_BIT( reg, bits ) ( ( reg ) |= ( bits ) )
#define CLEAR_BIT( reg, bits ) ( ( reg ) &= ~( bits ) )
#define __DMB() ( ( void )0 )

/**-----------------------------------------------------------------------------
 *  Mock HAL Types
 *------------------------------------------------------------------------------
 */

typedef int     HAL_StatusTypeDef;
typedef int32_t IRQn_Type;

typedef struct CAN_TxMailBox_TypeDef
{
    uint32_t TIR;
    uint32_t TDTR;
    uint32_t TDLR;
    uint32_t TDHR;
} CAN_TxMailBox_TypeDef;

typedef struct CAN_FIFOMailBox_TypeDef
{
    uint32_t RIR;
    uint32_t RDTR;
    uint32_t RDLR;
    uint32_t RDHR;
} CAN_FIFOMailBox_TypeDef;

typedef struct CAN_TypeDef
{
    uint32_t MCR;
    uint32_t MSR;
    uint32_t TSR;
    uint32_t RF0R;
    uint32_t RF1R;
    uint32_t IER;
    uint32_t ESR;
    uint32_t BTR;

    CAN_TxMailBox_TypeDef   sTxMailBox[3];
    CAN_FIFOMailBox_TypeDef sFIFOMailBox[2];
} CAN_TypeDef;

typedef struct CAN_InitTypeDef
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

typedef struct CAN_HandleTypeDef
{
    CAN_TypeDef*    Instance;
    CAN_InitTypeDef Init;
} CAN_HandleTypeDef;

typedef struct CAN_FilterTypeDef
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
    uint32_t SlaveStartFilterBank;
} CAN_FilterTypeDef;

/**-----------------------------------------------------------------------------
 *  Mock Hardware and Command-Register State
 *------------------------------------------------------------------------------
 */

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

extern CAN_TypeDef mock_can1_regs;
extern CAN_TypeDef mock_can2_regs;

#define CAN1 ( &mock_can1_regs )
#define CAN2 ( &mock_can2_regs )

#define CAN1_TX_IRQn ( ( IRQn_Type )19 )
#define CAN1_RX0_IRQn ( ( IRQn_Type )20 )
#define CAN1_SCE_IRQn ( ( IRQn_Type )22 )
#define CAN2_TX_IRQn ( ( IRQn_Type )63 )
#define CAN2_RX0_IRQn ( ( IRQn_Type )64 )
#define CAN2_SCE_IRQn ( ( IRQn_Type )66 )

extern uint32_t  mock_last_tsr_write;
extern uint32_t  mock_last_rf0r_write;
extern uint32_t  mock_last_msr_write;
extern IRQn_Type mock_last_pending_irq;
extern IRQn_Type mock_last_cleared_irq;

static inline void HW_CAN_Mock_Write_TSR( CAN_TypeDef* instance, uint32_t value )
{
    mock_last_tsr_write = value;

    const uint32_t request_masks[3] = {
        CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0,
        CAN_TSR_RQCP1 | CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1,
        CAN_TSR_RQCP2 | CAN_TSR_TXOK2 | CAN_TSR_ALST2 | CAN_TSR_TERR2,
    };
    const uint32_t rqcp_masks[3]  = { CAN_TSR_RQCP0, CAN_TSR_RQCP1, CAN_TSR_RQCP2 };
    const uint32_t abort_masks[3] = { CAN_TSR_ABRQ0, CAN_TSR_ABRQ1, CAN_TSR_ABRQ2 };
    const uint32_t empty_masks[3] = { CAN_TSR_TME0, CAN_TSR_TME1, CAN_TSR_TME2 };

    for ( uint32_t mailbox = 0U; mailbox < 3U; ++mailbox )
    {
        if ( ( value & rqcp_masks[mailbox] ) != 0U )
        {
            instance->TSR &= ~request_masks[mailbox];
        }

        if ( ( value & abort_masks[mailbox] ) != 0U )
        {
            instance->TSR &= ~request_masks[mailbox];
            instance->TSR |= rqcp_masks[mailbox] | empty_masks[mailbox];
        }
    }
}

static inline void HW_CAN_Mock_Write_RF0R( CAN_TypeDef* instance, uint32_t value )
{
    mock_last_rf0r_write = value;

    if ( ( value & CAN_RF0R_FULL0 ) != 0U )
    {
        instance->RF0R &= ~CAN_RF0R_FULL0;
    }

    if ( ( value & CAN_RF0R_FOVR0 ) != 0U )
    {
        instance->RF0R &= ~CAN_RF0R_FOVR0;
    }

    if ( ( value & CAN_RF0R_RFOM0 ) != 0U )
    {
        uint32_t pending = instance->RF0R & CAN_RF0R_FMP0;
        if ( pending != 0U )
        {
            instance->RF0R = ( instance->RF0R & ~CAN_RF0R_FMP0 ) | ( pending - 1U );
        }
    }
}

static inline void HW_CAN_Mock_Write_MSR( CAN_TypeDef* instance, uint32_t value )
{
    mock_last_msr_write = value;
    if ( ( value & CAN_MSR_ERRI ) != 0U )
    {
        instance->MSR &= ~CAN_MSR_ERRI;
    }
}

static inline void NVIC_SetPendingIRQ( IRQn_Type irqn )
{
    mock_last_pending_irq = irqn;
}

static inline void NVIC_ClearPendingIRQ( IRQn_Type irqn )
{
    mock_last_cleared_irq = irqn;
}

/**-----------------------------------------------------------------------------
 *  Mock HAL Function Prototypes
 *------------------------------------------------------------------------------
 */

HAL_StatusTypeDef HAL_CAN_Init( CAN_HandleTypeDef* hcan );
HAL_StatusTypeDef HAL_CAN_ConfigFilter( CAN_HandleTypeDef* hcan, CAN_FilterTypeDef* filter );
HAL_StatusTypeDef HAL_CAN_Start( CAN_HandleTypeDef* hcan );
HAL_StatusTypeDef HAL_CAN_Stop( CAN_HandleTypeDef* hcan );
HAL_StatusTypeDef HAL_CAN_ActivateNotification( CAN_HandleTypeDef* hcan, uint32_t notifications );
HAL_StatusTypeDef HAL_CAN_DeactivateNotification( CAN_HandleTypeDef* hcan, uint32_t notifications );
uint32_t          HAL_RCC_GetPCLK1Freq( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_CAN_MOCKS_H */
