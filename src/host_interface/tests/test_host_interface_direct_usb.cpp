/******************************************************************************
 *  File:       test_host_interface_direct_usb.cpp
 *  Description:
 *      Integration tests for the direct Application-to-USB result path.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "hil_rig_protocol/application/application.h"
#include "host_interface.h"
#include "host_interface_test_access.h"
#include "rtos_config.h"
#include "hw_usb_mocks.h"
#include "hw_usb.c"                            // NOLINT
#include "variable_result_message_producer.c"  // NOLINT

uint8_t* HOST_INSTRUCTION_HANDLER_GetSharedBuffer( void )
{
    static uint8_t s_test_shared_buffer[EXECUTION_INSTRUCTION_MAX_SIZE_BYTES];
    return s_test_shared_buffer;
}
}

namespace {

constexpr uint32_t kWorkloadTickCount = 1000U;
constexpr uint32_t kUartTotalBytes    = 4608U;
constexpr uint32_t kSpiTotalBytes     = 70312U;
constexpr uint32_t kCanTotalFrames    = 370U;

USBD_CDC_HandleTypeDef cdc_handle{};
uint8_t*               active_cdc_data   = nullptr;
uint16_t               active_cdc_length = 0U;
std::vector<uint8_t>   transmitted_bytes;
uint8_t                fake_stream_storage = 0U;
uint8_t                fake_mutex_storage  = 0U;
TickType_t             test_ticks          = 0U;
std::vector<uint8_t>   flash_result_bytes;
size_t                 flash_read_offset    = 0U;
uint32_t               flash_max_chunk_size = 0U;

uint32_t DistributedUnits( const uint32_t index, const uint32_t total_units )
{
    const uint32_t previous_total =
        ( index * total_units + kWorkloadTickCount - 1U ) / kWorkloadTickCount;
    const uint32_t current_total =
        ( ( index + 1U ) * total_units + kWorkloadTickCount - 1U ) / kWorkloadTickCount;
    return current_total - previous_total;
}

HIL_Application_Test_Id_T TestId()
{
    HIL_Application_Test_Id_T test_id{};
    for ( size_t index = 0U; index < HIL_APPLICATION_TEST_ID_SIZE; ++index )
    {
        test_id.bytes[index] = static_cast<uint8_t>( 0x40U + index );
    }
    return test_id;
}

struct VariableResultMessage
{
    std::array<uint8_t, 5U>                           uart{};
    std::array<uint8_t, 71U>                          spi{};
    std::array<uint8_t, 12U>                          can{};
    std::array<HIL_Application_Captured_Record_T, 3U> records{};
    HIL_Application_Message_T                         message{};

    explicit VariableResultMessage( const uint32_t tick )
    {
        const uint8_t uart_size = static_cast<uint8_t>( DistributedUnits( tick, kUartTotalBytes ) );
        const uint8_t spi_size  = static_cast<uint8_t>( DistributedUnits( tick, kSpiTotalBytes ) );
        const bool    has_can   = DistributedUnits( tick, kCanTotalFrames ) != 0U;

        for ( size_t index = 0U; index < uart.size(); ++index )
        {
            uart[index] = static_cast<uint8_t>( tick + index );
        }
        for ( size_t index = 0U; index < spi.size(); ++index )
        {
            spi[index] = static_cast<uint8_t>( ( tick * 3U ) + index );
        }

        can = { 0x34U,
                0x02U,
                0x04U,
                static_cast<uint8_t>( tick ),
                static_cast<uint8_t>( tick >> 8U ),
                0xBEU,
                0x00U,
                0x00U,
                0x00U,
                0x00U,
                0x00U,
                0x00U };

        records[0].peripheral_type = HIL_APPLICATION_PERIPHERAL_UART;
        records[0].channel         = 0U;
        records[0].data            = { uart.data(), uart_size };
        records[1].peripheral_type = HIL_APPLICATION_PERIPHERAL_SPI;
        records[1].channel         = 0U;
        records[1].data            = { spi.data(), spi_size };
        records[2].peripheral_type = HIL_APPLICATION_PERIPHERAL_CAN;
        records[2].channel         = 0U;
        records[2].data            = { can.data(), static_cast<uint8_t>( can.size() ) };

        message.type        = HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT;
        message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
        message.has_test_id = 1U;
        message.test_id     = TestId();
        message.body.variable_test_result.tick_number  = tick;
        message.body.variable_test_result.record_count = has_can ? 3U : 2U;
        message.body.variable_test_result.condition    = HIL_APPLICATION_RESULT_CONDITION_OK;
        message.body.variable_test_result.flags        = HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;
        message.body.variable_test_result.problem_detail = 0U;
        message.body.variable_test_result.records        = records.data();
    }
};

std::vector<uint8_t> EncodeFramed( const HIL_Application_Context_T& context,
                                   const HIL_Application_Message_T& message )
{
    size_t encoded_size = 0U;
    EXPECT_EQ( HIL_APPLICATION_STATUS_OK,
               HIL_APPLICATION_Encoded_Size( &context, &message, &encoded_size ) );

    std::vector<uint8_t> framed( encoded_size + 2U );
    size_t               used = 0U;
    EXPECT_EQ(
        HIL_APPLICATION_STATUS_OK,
        HIL_APPLICATION_Encode_Message( &context, &message, &framed[2], encoded_size, &used ) );
    EXPECT_EQ( encoded_size, used );
    framed[0] = static_cast<uint8_t>( encoded_size & 0xFFU );
    framed[1] = static_cast<uint8_t>( ( encoded_size >> 8U ) & 0xFFU );
    return framed;
}

void AppendFlashRecord( const uint32_t tick, const uint8_t peripheral_type, const uint8_t channel,
                        const uint8_t* const payload, const uint16_t payload_size )
{
    FlashManagerResultHeader_T header{};
    header.timestamp            = tick + 1U;
    header.payload_length_bytes = payload_size;
    header.peripheral_type      = peripheral_type;
    header.channel              = channel;

    const auto* const header_bytes = reinterpret_cast<const uint8_t*>( &header );
    flash_result_bytes.insert( flash_result_bytes.end(), header_bytes,
                               header_bytes + sizeof( header ) );
    flash_result_bytes.insert( flash_result_bytes.end(), payload, payload + payload_size );
}

void PopulateDistributedFlashResults()
{
    flash_result_bytes.clear();
    flash_read_offset = 0U;

    for ( uint32_t tick = 0U; tick < kWorkloadTickCount; ++tick )
    {
        std::array<uint8_t, 5U>  uart{};
        std::array<uint8_t, 71U> spi{};
        std::array<uint8_t, 12U> can = {
            0x34U,
            0x02U,
            0x04U,
            static_cast<uint8_t>( tick ),
            static_cast<uint8_t>( tick >> 8U ),
            0xBEU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
        };
        const uint16_t uart_size =
            static_cast<uint16_t>( DistributedUnits( tick, kUartTotalBytes ) );
        const uint16_t spi_size = static_cast<uint16_t>( DistributedUnits( tick, kSpiTotalBytes ) );

        for ( size_t index = 0U; index < uart.size(); ++index )
        {
            uart[index] = static_cast<uint8_t>( tick + index );
        }
        for ( size_t index = 0U; index < spi.size(); ++index )
        {
            spi[index] = static_cast<uint8_t>( ( tick * 3U ) + index );
        }

        AppendFlashRecord( tick, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE, 0U, uart.data(),
                           uart_size );
        AppendFlashRecord( tick, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE, 0U, spi.data(),
                           spi_size );
        if ( DistributedUnits( tick, kCanTotalFrames ) != 0U )
        {
            AppendFlashRecord( tick, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE, 0U, can.data(),
                               static_cast<uint16_t>( can.size() ) );
        }
    }
}

void CompleteActiveTransfer()
{
    ASSERT_NE( nullptr, active_cdc_data );
    ASSERT_GT( active_cdc_length, 0U );

    transmitted_bytes.insert( transmitted_bytes.end(), active_cdc_data,
                              active_cdc_data + active_cdc_length );
    active_cdc_data    = nullptr;
    active_cdc_length  = 0U;
    cdc_handle.TxState = 0U;
    HW_USB_Monitor_Process();
}

void DrainUsb()
{
    while ( usb_state.transmit_num_buffered > 0U )
    {
        if ( active_cdc_data == nullptr )
        {
            HW_USB_Monitor_Process();
        }
        ASSERT_NE( nullptr, active_cdc_data );
        CompleteActiveTransfer();
    }
}

void InitialisePath( const size_t initial_ring_offset )
{
    std::memset( &usb_state, 0, sizeof( usb_state ) );
    std::memset( &hUsbDeviceFS, 0, sizeof( hUsbDeviceFS ) );
    std::memset( &cdc_handle, 0, sizeof( cdc_handle ) );
    active_cdc_data   = nullptr;
    active_cdc_length = 0U;
    transmitted_bytes.clear();
    test_ticks = 0U;

    hUsbDeviceFS.dev_state  = USBD_STATE_CONFIGURED;
    hUsbDeviceFS.pClassData = &cdc_handle;
    ASSERT_TRUE( HW_USB_Init() );

    if ( initial_ring_offset > 0U )
    {
        std::vector<uint8_t> prefix( initial_ring_offset, 0xA5U );
        ASSERT_TRUE( HW_USB_Transmit( prefix.data(), static_cast<uint16_t>( prefix.size() ) ) );
        DrainUsb();
        transmitted_bytes.clear();
    }

    HOST_INTERFACE_Test_Access_Reset_Protocol();
}

void FlushDirectPath()
{
    while ( HOST_INTERFACE_Test_Access_Get_Direct_Pending_Bytes() > 0U
            || usb_state.transmit_num_buffered > 0U || active_cdc_data != nullptr )
    {
        if ( active_cdc_data != nullptr )
        {
            CompleteActiveTransfer();
        }
        else
        {
            EXPECT_FALSE( HOST_INTERFACE_Test_Access_Submit_Outgoing( nullptr ) );
        }
    }

    // Let the Host Interface reconcile the final CDC completion into its
    // outstanding result-batch ledger.
    EXPECT_FALSE( HOST_INTERFACE_Test_Access_Submit_Outgoing( nullptr ) );
}

void ExpectTransmittedBytes( const std::vector<uint8_t>& expected_bytes )
{
    EXPECT_EQ( 0U, HOST_INTERFACE_Test_Access_Get_Direct_Pending_Bytes() );
    ASSERT_EQ( expected_bytes.size(), transmitted_bytes.size() );
    const auto mismatch =
        std::mismatch( expected_bytes.begin(), expected_bytes.end(), transmitted_bytes.begin() );
    EXPECT_EQ( expected_bytes.end(), mismatch.first )
        << "first differing USB stream byte at offset "
        << std::distance( expected_bytes.begin(), mismatch.first );
}

void RunDistributedWorkload( const size_t initial_ring_offset, const uint32_t completion_interval )
{
    InitialisePath( initial_ring_offset );

    HIL_Application_Config_T  config{};
    HIL_Application_Context_T context{};
    ASSERT_EQ( HIL_APPLICATION_STATUS_OK, HIL_APPLICATION_Default_Config( &config ) );
    ASSERT_EQ( HIL_APPLICATION_STATUS_OK, HIL_APPLICATION_Init( &context, &config ) );

    std::vector<uint8_t> expected_bytes;
    for ( uint32_t tick = 0U; tick < kWorkloadTickCount; ++tick )
    {
        VariableResultMessage      result( tick );
        const std::vector<uint8_t> framed = EncodeFramed( context, result.message );
        if ( tick == 140U )
        {
            ASSERT_EQ( 137U, framed.size() );
        }
        else if ( tick == 141U )
        {
            ASSERT_EQ( 125U, framed.size() );
        }
        expected_bytes.insert( expected_bytes.end(), framed.begin(), framed.end() );

        while ( !HOST_INTERFACE_Test_Access_Submit_Outgoing( &result.message ) )
        {
            ASSERT_NE( nullptr, active_cdc_data );
            CompleteActiveTransfer();
        }

        if ( ( ( tick + 1U ) % completion_interval ) == 0U && active_cdc_data != nullptr )
        {
            CompleteActiveTransfer();
        }
        ++test_ticks;
    }

    FlushDirectPath();
    ExpectTransmittedBytes( expected_bytes );

    HostInterfaceStatus_T status{};
    HOST_INTERFACE_GetStatus( &status );
    EXPECT_EQ( kWorkloadTickCount, status.result_staged_count );
    EXPECT_EQ( kWorkloadTickCount, status.result_usb_queued_count );
    EXPECT_EQ( kWorkloadTickCount, status.result_cdc_completed_count );
    EXPECT_EQ( 0U, status.result_staged_message_count );
    EXPECT_EQ( 0U, status.result_outstanding_batch_count );
    EXPECT_EQ( HOST_INTERFACE_RESULT_TX_INVARIANT_NONE, status.result_invariant_failure );
}

void RunProducerToUsbWorkload( const uint32_t flash_chunk_size, const uint32_t completion_interval )
{
    constexpr uint32_t kExpectedTickCount = 2002U;

    InitialisePath( 0U );
    PopulateDistributedFlashResults();
    flash_max_chunk_size = flash_chunk_size;
    VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();
    VARIABLE_RESULT_MESSAGE_PRODUCER_SetExpectedTickCount( kExpectedTickCount );

    HIL_Application_Config_T  config{};
    HIL_Application_Context_T context{};
    ASSERT_EQ( HIL_APPLICATION_STATUS_OK, HIL_APPLICATION_Default_Config( &config ) );
    ASSERT_EQ( HIL_APPLICATION_STATUS_OK, HIL_APPLICATION_Init( &context, &config ) );

    std::vector<uint8_t> expected_bytes;
    for ( uint32_t expected_tick = 0U; expected_tick < kExpectedTickCount; ++expected_tick )
    {
        HIL_Application_Message_T result{};
        ASSERT_EQ( RESULT_MESSAGE_PRODUCER_STATUS_OK,
                   VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &result ) );
        ASSERT_EQ( HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT, result.type );
        ASSERT_EQ( expected_tick, result.body.variable_test_result.tick_number );

        const std::vector<uint8_t> framed = EncodeFramed( context, result );
        expected_bytes.insert( expected_bytes.end(), framed.begin(), framed.end() );

        while ( !HOST_INTERFACE_Test_Access_Submit_Outgoing( &result ) )
        {
            ASSERT_NE( nullptr, active_cdc_data );
            CompleteActiveTransfer();
        }

        if ( ( ( expected_tick + 1U ) % completion_interval ) == 0U && active_cdc_data != nullptr )
        {
            CompleteActiveTransfer();
        }
        ++test_ticks;
    }

    HIL_Application_Message_T end_message{};
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM,
               VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &end_message ) );

    FlushDirectPath();
    ExpectTransmittedBytes( expected_bytes );

    HostInterfaceStatus_T status{};
    HOST_INTERFACE_GetStatus( &status );
    EXPECT_EQ( kExpectedTickCount, status.result_staged_count );
    EXPECT_EQ( kExpectedTickCount, status.result_usb_queued_count );
    EXPECT_EQ( kExpectedTickCount, status.result_cdc_completed_count );
    EXPECT_EQ( kExpectedTickCount - 1U, status.result_last_cdc_completed_tick );
    EXPECT_EQ( 0U, status.result_outstanding_batch_count );
    EXPECT_EQ( HOST_INTERFACE_RESULT_TX_INVARIANT_NONE, status.result_invariant_failure );
}

}  // namespace

extern "C" USBD_HandleTypeDef hUsbDeviceFS = {};

extern "C" FlashManagerResultTransferStatus_T
FLASH_MANAGER_ReadResultBytes( uint8_t* const  destination,
                               const uint32_t  destination_capacity_bytes,
                               uint32_t* const bytes_read )
{
    if ( destination == nullptr || bytes_read == nullptr )
    {
        return FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT;
    }
    if ( flash_read_offset >= flash_result_bytes.size() )
    {
        *bytes_read = 0U;
        return FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM;
    }

    size_t bytes_to_copy = std::min( static_cast<size_t>( destination_capacity_bytes ),
                                     flash_result_bytes.size() - flash_read_offset );
    if ( flash_max_chunk_size > 0U )
    {
        bytes_to_copy = std::min( bytes_to_copy, static_cast<size_t>( flash_max_chunk_size ) );
    }

    std::memcpy( destination, &flash_result_bytes[flash_read_offset], bytes_to_copy );
    flash_read_offset += bytes_to_copy;
    *bytes_read = static_cast<uint32_t>( bytes_to_copy );
    return FLASH_MANAGER_RESULT_TRANSFER_OK;
}

extern "C" uint8_t CDC_Transmit_FS( uint8_t* const buffer, const uint16_t length )
{
    if ( active_cdc_data != nullptr )
    {
        return USBD_BUSY;
    }

    active_cdc_data    = buffer;
    active_cdc_length  = length;
    cdc_handle.TxState = 1U;
    return USBD_OK;
}

extern "C" void CDC_Resume_Receive_FS( void )
{
}

extern "C" StreamBufferHandle_t xStreamBufferCreate( size_t, size_t )
{
    return reinterpret_cast<StreamBufferHandle_t>( &fake_stream_storage );
}

extern "C" size_t xStreamBufferSendFromISR( StreamBufferHandle_t, const void*, size_t, BaseType_t* )
{
    return 0U;
}

extern "C" size_t xStreamBufferReceive( StreamBufferHandle_t, void*, size_t, TickType_t )
{
    return 0U;
}

extern "C" size_t xStreamBufferSpacesAvailable( StreamBufferHandle_t )
{
    return MAX_USB_RECEIVE_STREAM_BYTES;
}

extern "C" SemaphoreHandle_t xSemaphoreCreateMutexStatic( StaticSemaphore_t* )
{
    return reinterpret_cast<SemaphoreHandle_t>( &fake_mutex_storage );
}

extern "C" BaseType_t xSemaphoreTake( SemaphoreHandle_t, TickType_t )
{
    return pdTRUE;
}

extern "C" BaseType_t xSemaphoreGive( SemaphoreHandle_t )
{
    return pdTRUE;
}

extern "C" TickType_t xTaskGetTickCount( void )
{
    return test_ticks;
}

extern "C" void vTaskDelayUntil( TickType_t*, TickType_t )
{
}

extern "C" void vTaskDelay( TickType_t )
{
}

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
    return nullptr;
}

extern "C" BaseType_t xTaskNotifyWait( uint32_t, uint32_t, uint32_t* const notification_value,
                                       TickType_t )
{
    if ( notification_value != nullptr )
    {
        *notification_value = 0U;
    }
    return pdPASS;
}

extern "C" BaseType_t xTaskNotify( TaskHandle_t, uint32_t, eNotifyAction )
{
    return pdPASS;
}

/**
 * @brief The distributed UART/SPI/CAN result stream remains byte-exact across USB ring wraps.
 */
TEST( HostInterfaceDirectUsbTest, DistributedVariableResultsRemainExactAcrossBackpressureAndWrap )
{
    RunDistributedWorkload( 0U, 7U );
}

/**
 * @brief Different initial ring phases and completion rates preserve every direct result frame.
 */
TEST( HostInterfaceDirectUsbTest, DistributedVariableResultsRemainExactForAdversarialRingPhases )
{
    const std::array<size_t, 3U>   ring_offsets         = { 1U, 1023U, 1994U };
    const std::array<uint32_t, 3U> completion_intervals = { 1U, 5U, 17U };

    for ( const size_t ring_offset : ring_offsets )
    {
        for ( const uint32_t completion_interval : completion_intervals )
        {
            SCOPED_TRACE( testing::Message() << "ring offset " << ring_offset
                                             << ", completion interval " << completion_interval );
            RunDistributedWorkload( ring_offset, completion_interval );
        }
    }
}

/**
 * @brief Raw fragmented flash results remain sequential through producer and direct USB output.
 */
TEST( HostInterfaceDirectUsbTest, DistributedFlashResultsRemainExactThroughProducerAndUsb )
{
    RunProducerToUsbWorkload( 257U, 11U );
}

/** Verifies that a skipped result tick is latched as a firmware custody fault. */
TEST( HostInterfaceDirectUsbTest, ResultTickDiscontinuityLatchesFirmwareFault )
{
    InitialisePath( 0U );
    VariableResultMessage tick_zero( 0U );
    VariableResultMessage tick_two( 2U );

    ASSERT_TRUE( HOST_INTERFACE_Test_Access_Submit_Outgoing( &tick_zero.message ) );
    ASSERT_TRUE( HOST_INTERFACE_Test_Access_Submit_Outgoing( &tick_two.message ) );

    HostInterfaceStatus_T status{};
    HOST_INTERFACE_GetStatus( &status );
    EXPECT_TRUE( status.is_faulted );
    EXPECT_EQ( RUN_STATE_FAULT_HOST_INTERFACE_ERROR, status.last_fault_reason );
    EXPECT_EQ( HOST_INTERFACE_RESULT_TX_INVARIANT_STAGED_TICK, status.result_invariant_failure );
}

/** Verifies that a CDC-owned result span cannot remain silently stuck. */
TEST( HostInterfaceDirectUsbTest, StalledCDCCompletionLatchesFirmwareFault )
{
    InitialisePath( 0U );
    VariableResultMessage tick_zero( 0U );
    ASSERT_TRUE( HOST_INTERFACE_Test_Access_Submit_Outgoing( &tick_zero.message ) );
    ASSERT_NE( nullptr, active_cdc_data );

    test_ticks = pdMS_TO_TICKS( 5001U );
    EXPECT_FALSE( HOST_INTERFACE_Test_Access_Submit_Outgoing( nullptr ) );

    HostInterfaceStatus_T status{};
    HOST_INTERFACE_GetStatus( &status );
    EXPECT_TRUE( status.is_faulted );
    EXPECT_EQ( HOST_INTERFACE_RESULT_TX_INVARIANT_CDC_COMPLETION_TIMEOUT,
               status.result_invariant_failure );
}

/** Verifies that multi-chunk results for the same tick preserve custody without tripping
 * invariants. */
TEST( HostInterfaceDirectUsbTest, MultiChunkResultPreservesCustodyWithoutFault )
{
    InitialisePath( 0U );
    VariableResultMessage chunk_zero( 0U );
    chunk_zero.message.body.variable_test_result.flags =
        HIL_APPLICATION_RESULT_FLAG_HAS_MORE_CHUNKS;

    VariableResultMessage chunk_one( 0U );
    chunk_one.message.body.variable_test_result.flags = HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;

    ASSERT_TRUE( HOST_INTERFACE_Test_Access_Submit_Outgoing( &chunk_zero.message ) );
    DrainUsb();

    ASSERT_TRUE( HOST_INTERFACE_Test_Access_Submit_Outgoing( &chunk_one.message ) );
    FlushDirectPath();

    HostInterfaceStatus_T status{};
    HOST_INTERFACE_GetStatus( &status );
    EXPECT_FALSE( status.is_faulted );
    EXPECT_EQ( HOST_INTERFACE_RESULT_TX_INVARIANT_NONE, status.result_invariant_failure );
    EXPECT_EQ( 2U, status.result_staged_count );
    EXPECT_EQ( 2U, status.result_cdc_completed_count );
    EXPECT_EQ( 0U, status.result_last_cdc_completed_tick );
}
