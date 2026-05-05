/******************************************************************************
 *  File:       hw_spi.h
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Defines private DMA resource mappings, TX/RX buffers, master packet
 *      descriptors, transaction state, function-pointer dispatch hooks, and
 *      internal helper prototypes shared by the split SPI implementation files.
 *      This header is only exposed when HW_SPI_INTERNAL is defined.
 *
 *  Notes:
 *      Runtime TX/RX paths intentionally keep validation minimal. Configuration
 *      functions perform setup-time checks; ISR and hot-path functions assume
 *      the selected peripheral has already been configured correctly.
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
#include "hw_timer.h"
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

/**
 * @brief Describes one queued master-mode TX packet.
 *
 * @details
 *     Master TX must preserve packet boundaries because every packet is framed
 *     by software chip-select and transmitted as exactly one DMA transfer. The
 *     descriptor records where the packet lives inside tx_buffer and how many
 *     bytes belong to it.
 */
typedef struct SPITxPacketDescriptor_T
{
    uint16_t start_index;  ///< Byte index into tx_buffer where this packet starts.
    uint16_t size_bytes;   ///< Number of bytes in this packet; must be frame aligned.
} SPITxPacketDescriptor_T;

/**
 * @brief Tracks the electrical completion state of a master TX transaction.
 *
 * @details
 *     DMA TC is not the same as SPI bus completion. Master mode therefore uses
 *     this state machine to distinguish an idle channel, a DMA-active packet,
 *     a packet waiting for final-frame drain before CS release, and a faulted
 *     transaction that requires higher-level recovery.
 */
typedef enum HWSPI_TX_Transaction_State_T
{
    HW_SPI_TX_TRANSACTION_IDLE,              ///< No master packet is currently active.
    HW_SPI_TX_TRANSACTION_DMA_ACTIVE,        ///< One packet has been handed to TX DMA.
    HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN,  ///< DMA is complete, but SPI may still be busy.
    HW_SPI_TX_TRANSACTION_ERROR,             ///< TX transaction fault; recovery is required.
} HWSPI_TX_Transaction_State_T;

typedef struct SPIPeripheralState_T SPIPeripheralState_T;

/**
 * @brief Mode-specific TX load operation.
 *
 * @details
 *     Master mode loads one packet descriptor per public load call. Slave mode
 *     appends raw stream bytes. The public API dispatches through this pointer
 *     so runtime TX paths avoid repeated master/slave branching.
 */
typedef bool ( *HWSPI_TX_Load_Function_T )( SPIPeripheralState_T* peripheral_state,
                                            const uint8_t* data, uint32_t size );

/**
 * @brief Mode-specific TX DMA start operation.
 */
typedef bool ( *HWSPI_TX_Start_DMA_Function_T )( SPIPeripheralState_T* peripheral_state );

/**
 * @brief Mode-specific pending-work query.
 */
typedef bool ( *HWSPI_TX_Has_Pending_Function_T )( const SPIPeripheralState_T* peripheral_state );

/**
 * @brief Channel-specific helper for clearing TX DMA flags.
 */
typedef void ( *HWSPI_TX_Clear_DMA_Flags_Function_T )( DMA_TypeDef* dma );

/**
 * @brief Complete private state for one logical SPI peripheral.
 *
 * @details
 *     The state block deliberately contains both hardware resource pointers and
 *     software queue bookkeeping so hot TX/RX paths can operate without looking
 *     up mappings repeatedly. Configuration code initialises the structure; ISR
 *     and fast-path code assumes the fields are valid.
 */
struct SPIPeripheralState_T
{
    HWSPIConfig_T config;  ///< Last configuration applied to this logical channel.

    uint8_t rx_buffer[RX_BUFFER_SIZE_BYTES]
        __attribute__( ( aligned( 2 ) ) );  ///< DMA-backed circular RX buffer.
    uint32_t rx_position;  ///< Software consume index into rx_buffer, expressed in bytes.

    uint8_t tx_buffer[TX_BUFFER_SIZE_BYTES]
        __attribute__( ( aligned( 2 ) ) );  ///< Software TX queue storage.
    uint32_t tx_write_position;             ///< Next byte index to write when loading TX data.
    uint32_t tx_read_position;              ///< Next byte index to hand to DMA.
    uint32_t tx_num_bytes_pending;          ///< Bytes queued in software but not yet owned by DMA.
    uint32_t tx_num_bytes_in_transmission;  ///< Bytes currently owned by an active TX DMA transfer.

    // Master-mode TX is always software-chip-select framed. Each queued master
    // packet is sent as one DMA transfer and one electrical SPI transaction.
    // DMA TC therefore moves the state into transaction completion rather than
    // immediately making the channel idle. Slave mode leaves this state idle.
    HWSPI_TX_Transaction_State_T tx_transaction_state;

    SPITxPacketDescriptor_T tx_packet_descriptors[TX_PACKET_QUEUE_DEPTH];  ///< Master packet queue.
    uint8_t                 tx_packet_write_position;  ///< Next descriptor slot to fill.
    uint8_t                 tx_packet_read_position;   ///< Next descriptor slot to start via DMA.
    uint8_t tx_num_packets_pending;  ///< Number of queued master packet descriptors.

    DMA_TypeDef* rx_dma;          ///< RX DMA controller instance.
    uint32_t     rx_dma_stream;   ///< RX DMA stream selection.
    DMA_TypeDef* tx_dma;          ///< TX DMA controller instance.
    uint32_t     tx_dma_stream;   ///< TX DMA stream selection.
    SPI_TypeDef* spi_peripheral;  ///< STM32 SPI peripheral instance.
    IRQn_Type    tx_dma_irqn;     ///< NVIC IRQn for the TX DMA stream.

    HWSPI_TX_Load_Function_T tx_load_function;  ///< Selected master/slave load implementation.
    HWSPI_TX_Start_DMA_Function_T
        tx_start_dma_function;  ///< Selected master/slave DMA start implementation.
    HWSPI_TX_Has_Pending_Function_T
        tx_has_pending_function;  ///< Selected master/slave pending query.
    HWSPI_TX_Clear_DMA_Flags_Function_T
        tx_clear_dma_flags_function;  ///< Channel-specific DMA flag clear helper.
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

/**
 * @name Shared configuration and conversion helpers
 * @{
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
/** @} */

/**
 * @name RX implementation hooks
 * @{
 */
bool HW_SPI_RX_Start_Passive_DMA( SPIPeripheralState_T* peripheral_state );
/** @} */

/**
 * @name TX common implementation hooks
 * @{
 */
void     HW_SPI_TX_Configure_Operations( SPIPeripheralState_T* peripheral_state );
void     HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state );
void     HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral );
void     HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral );
void     HW_SPI_TX_Master_CS_Assert( SPIPeripheralState_T* peripheral_state );
void     HW_SPI_TX_Master_CS_Deassert( SPIPeripheralState_T* peripheral_state );
uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state );
uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state );
bool     HW_SPI_TX_Program_DMA( SPIPeripheralState_T* peripheral_state, uint8_t* tx_ptr,
                                uint32_t size_bytes );
bool     HW_SPI_TX_Should_Use_Final_Drain_Timer( const SPIPeripheralState_T* peripheral_state );
Timer_T  HW_SPI_Get_Tx_Timer( const SPIPeripheralState_T* peripheral_state );
/** @} */

/**
 * @name Master-mode TX packet implementation hooks
 * @{
 */
bool HW_SPI_TX_Master_Has_Pending( const SPIPeripheralState_T* peripheral_state );
bool HW_SPI_TX_Load_Master_Packet( SPIPeripheralState_T* peripheral_state, const uint8_t* data,
                                   uint32_t size );
bool HW_SPI_TX_Start_Master_Packet_DMA( SPIPeripheralState_T* peripheral_state );
/** @} */

/**
 * @name Slave-mode TX stream implementation hooks
 * @{
 */
bool HW_SPI_TX_Slave_Has_Pending( const SPIPeripheralState_T* peripheral_state );
bool HW_SPI_TX_Load_Slave_Stream( SPIPeripheralState_T* peripheral_state, const uint8_t* data,
                                  uint32_t size );
bool HW_SPI_TX_Start_Slave_Stream_DMA( SPIPeripheralState_T* peripheral_state );
/** @} */
#endif /* HW_SPI_INTERNAL */

#ifdef __cplusplus
}
#endif

#endif /* HW_SPI_INTERNAL_H */
