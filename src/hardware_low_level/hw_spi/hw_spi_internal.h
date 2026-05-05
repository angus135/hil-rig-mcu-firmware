/******************************************************************************
 *  File:       hw_spi.h
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Public interface for the low-level SPI driver used by the HIL-RIG
 *      firmware.
 *
 *      This module exposes configuration and runtime control functions for the
 *      supported SPI peripherals, along with a generic RX peek/consume API and
 *      TX load/trigger API. The interface is designed to present SPI traffic as
 *      a raw byte stream and to hide the underlying DMA and peripheral control
 *      details from higher-level software.
 *
 *      The driver supports both 8-bit and 16-bit SPI data sizes while keeping
 *      the public RX and TX interfaces byte-based. Higher-level software is
 *      responsible for protocol framing, message construction/parsing,
 *      scheduling decisions, and correct semantic use of the configured SPI
 *      channel.
 *
 *  Notes:
 *      - This is a low-level transport-style driver, not a protocol driver.
 *      - RX data is exposed as unread spans into an internal DMA-backed buffer.
 *      - TX data is copied into an internal queue before being transmitted.
 *      - The caller does not retain ownership of returned RX span storage and
 *        must not modify it.
 *      - In 16-bit SPI mode, TX buffer load sizes and RX consume sizes must be
 *        multiples of 2 bytes.
 *      - RX span lengths are always reported in bytes, including in 16-bit
 *        mode.
 *      - The driver does not define packet/message boundaries.
 *      - The driver does not perform byte swapping or data repacking for
 *        16-bit mode; higher-level software must provide data in the intended
 *        in-memory order.
 *      - A channel must be configured before it is started or used.
 ******************************************************************************/

#ifndef HW_SPI_INTERNAL_H
#define HW_SPI_INTERNAL_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef HW_SPI_INTERNAL

/**-----------------------------------------------------------------------------
 *  Internal Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_spi_mocks.h"
#else
#include "spi.h"
#include "stm32f446xx.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_it.h"
#endif
#include <stddef.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Internal Defines / Macros
 *------------------------------------------------------------------------------
 */

// TODO: Change this based on true hardware
#define SPI_CHANNEL_0_HANDLE hspi1
#define SPI_CHANNEL_1_HANDLE hspi4
#define SPI_DAC_HANDLE hspi4

#define SPI_CHANNEL_0_INSTANCE SPI1
#define SPI_CHANNEL_1_INSTANCE SPI4
#define SPI_DAC_INSTANCE SPI4

// DMA Definitions
#define SPI_CHANNEL_0_RX_DMA DMA2
#define SPI_CHANNEL_0_RX_DMA_STREAM LL_DMA_STREAM_0
#define SPI_CHANNEL_0_RX_DMA_IRQ DMA2_Stream0_IRQHandler
#define SPI_CHANNEL_0_RX_DMA_IRQN DMA2_Stream0_IRQn
#define SPI_CHANNEL_0_TX_DMA DMA2
#define SPI_CHANNEL_0_TX_DMA_STREAM LL_DMA_STREAM_5
#define SPI_CHANNEL_0_TX_DMA_IRQ DMA2_Stream5_IRQHandler
#define SPI_CHANNEL_0_TX_DMA_IRQN DMA2_Stream5_IRQn
#define SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC5
#define SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE5
#define SPI_CHANNEL_0_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC5
#define SPI_CHANNEL_0_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE5
#define SPI_CHANNEL_1_RX_DMA DMA2
#define SPI_CHANNEL_1_RX_DMA_STREAM LL_DMA_STREAM_3
#define SPI_CHANNEL_1_RX_DMA_IRQ DMA2_Stream3_IRQHandler
#define SPI_CHANNEL_1_RX_DMA_IRQN DMA2_Stream3_IRQn
#define SPI_CHANNEL_1_TX_DMA DMA2
#define SPI_CHANNEL_1_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_CHANNEL_1_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_CHANNEL_1_TX_DMA_IRQN DMA2_Stream1_IRQn
#define SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC1
#define SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE1
#define SPI_CHANNEL_1_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC1
#define SPI_CHANNEL_1_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE1
#define SPI_DAC_TX_DMA DMA2
#define SPI_DAC_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_DAC_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_DAC_TX_DMA_IRQN DMA2_Stream1_IRQn
#define SPI_DAC_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC1
#define SPI_DAC_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE1
#define SPI_DAC_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC1
#define SPI_DAC_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE1

#define RX_BUFFER_SIZE_BYTES 1024U
#define TX_BUFFER_SIZE_BYTES 1024U
#define TX_PACKET_QUEUE_DEPTH 16U

#if RX_BUFFER_SIZE_BYTES % 2 != 0
#error "RX Buffer Must be a size that is a multiple of 2"
#endif

#if TX_BUFFER_SIZE_BYTES % 2 != 0
#error "TX Buffer Must be a size that is a multiple of 2"
#endif

/**-----------------------------------------------------------------------------
 *  Internal Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct SPITxPacketDescriptor_T
{
    uint16_t start_index;
    uint16_t size_bytes;
} SPITxPacketDescriptor_T;

typedef struct SPIPeripheralState_T SPIPeripheralState_T;
typedef bool ( *HWSPI_TX_Load_Function_T )( SPIPeripheralState_T* peripheral_state,
                                            const uint8_t* data, uint32_t size );
typedef bool ( *HWSPI_TX_Start_DMA_Function_T )( SPIPeripheralState_T* peripheral_state );
typedef bool ( *HWSPI_TX_Has_Pending_Function_T )( const SPIPeripheralState_T* peripheral_state );
typedef void ( *HWSPI_TX_Clear_DMA_Flags_Function_T )( DMA_TypeDef* dma );

struct SPIPeripheralState_T
{
    HWSPIConfig_T config;

    uint8_t  rx_buffer[RX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t rx_position;

    uint8_t  tx_buffer[TX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t tx_write_position;
    uint32_t tx_read_position;
    uint32_t tx_num_bytes_pending;
    uint32_t tx_num_bytes_in_transmission;

    SPITxPacketDescriptor_T tx_packet_descriptors[TX_PACKET_QUEUE_DEPTH];
    uint8_t                 tx_packet_write_position;
    uint8_t                 tx_packet_read_position;
    uint8_t                 tx_num_packets_pending;

    DMA_TypeDef* rx_dma;
    uint32_t     rx_dma_stream;
    DMA_TypeDef* tx_dma;
    uint32_t     tx_dma_stream;
    SPI_TypeDef* spi_peripheral;
    IRQn_Type    tx_dma_irqn;

    HWSPI_TX_Load_Function_T            tx_load_function;
    HWSPI_TX_Start_DMA_Function_T       tx_start_dma_function;
    HWSPI_TX_Has_Pending_Function_T     tx_has_pending_function;
    HWSPI_TX_Clear_DMA_Flags_Function_T tx_clear_dma_flags_function;
};

/**-----------------------------------------------------------------------------
 *  Internal Global State
 *------------------------------------------------------------------------------
 */

extern SPIPeripheralState_T  channel_0_state_struct;
extern SPIPeripheralState_T  channel_1_state_struct;
extern SPIPeripheralState_T  dac_state_struct;
extern SPIPeripheralState_T* channel_0_state;
extern SPIPeripheralState_T* channel_1_state;
extern SPIPeripheralState_T* dac_state;

/**-----------------------------------------------------------------------------
 *  Internal Function Prototypes
 *------------------------------------------------------------------------------
 */

SPIPeripheralState_T* HW_SPI_Get_State( SPIPeripheral_T peripheral );
uint32_t              HW_SPI_Get_Frame_Size_Bytes( const SPIPeripheralState_T* peripheral_state );
uint16_t              HW_SPI_Bytes_To_DMA_Elements( const SPIPeripheralState_T* peripheral_state,
                                                    uint32_t                    size_bytes );
uint32_t              HW_SPI_DMA_Elements_To_Bytes( const SPIPeripheralState_T* peripheral_state,
                                                    uint32_t                    num_elements );
bool                  HW_SPI_Is_Frame_Aligned_Size( const SPIPeripheralState_T* peripheral_state,
                                                    uint32_t                    size_bytes );
void                  HW_SPI_Configure_DMA_Data_Widths( SPIPeripheralState_T* peripheral_state );

bool HW_SPI_RX_Start_Passive_DMA( SPIPeripheralState_T* peripheral_state );

void     HW_SPI_TX_Configure_Operations( SPIPeripheralState_T* peripheral_state );
void     HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state );
void     HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral );
void     HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral );
uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state );

#endif /* HW_SPI_INTERNAL */

#ifdef __cplusplus
}
#endif

#endif /* HW_SPI_INTERNAL_H */
