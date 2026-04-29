/******************************************************************************
 *  File:       test_hw_usb.cpp
 *  Author:     Angus Corr
 *  Created:    29-Apr-2026
 *
 *  Description:
 *      Unit tests for the hw_usb module using GoogleTest and GoogleMock.
 *      This file validates the public API and internal state transitions of
 *      hw_usb.c by including the C implementation directly in this test
 *      translation unit.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - hw_usb.c is included directly so static state/functions are visible.
 *      - External CDC and FreeRTOS dependencies are replaced by test doubles.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C"
{
#include "rtos_config.h"
#include "hw_usb_mocks.h"
#include "hw_usb.c"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint32_t EXPECTED_MAX_USB_TRANSMIT_BYTES          = 512U;
static constexpr uint32_t EXPECTED_MAX_USB_RECEIVE_STREAM_BYTES    = 1024U;
static constexpr size_t   EXPECTED_USB_RECEIVE_TRIGGER_LEVEL_BYTES = 1U;

static_assert( MAX_USB_TRANSMIT_BYTES == EXPECTED_MAX_USB_TRANSMIT_BYTES,
               "Update tests if MAX_USB_TRANSMIT_BYTES changes." );
static_assert( MAX_USB_RECEIVE_STREAM_BYTES == EXPECTED_MAX_USB_RECEIVE_STREAM_BYTES,
               "Update tests if MAX_USB_RECEIVE_STREAM_BYTES changes." );
static_assert( USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES == EXPECTED_USB_RECEIVE_TRIGGER_LEVEL_BYTES,
               "Update tests if USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES changes." );

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

class MockHWUSB
{
public:
    MOCK_METHOD( uint8_t, CDCTransmitFS, ( uint8_t * buffer, uint16_t length ), () );

    MOCK_METHOD( StreamBufferHandle_t, xStreamBufferCreate,
                 ( size_t buffer_size_bytes, size_t trigger_level_bytes ), () );

    MOCK_METHOD( size_t, StreamBufferSendFromISR,
                 ( StreamBufferHandle_t stream_buffer, const void* data, size_t length,
                   BaseType_t* higher_priority_task_woken ),
                 () );

    MOCK_METHOD( size_t, xStreamBufferReceive,
                 ( StreamBufferHandle_t stream_buffer, void* destination, size_t max_length,
                   TickType_t ticks_to_wait ),
                 () );

    MOCK_METHOD( size_t, StreamBufferSpacesAvailable, ( StreamBufferHandle_t stream_buffer ), () );

};

static MockHWUSB* g_mock = nullptr;

extern "C" USBD_HandleTypeDef hUsbDeviceFS = {};

extern "C" uint8_t CDC_Transmit_FS( uint8_t* Buf, uint16_t Len )
{
    return g_mock->CDCTransmitFS( Buf, Len );
}

extern "C" StreamBufferHandle_t xStreamBufferCreate( size_t xBufferSizeBytes,
                                                     size_t xTriggerLevelBytes )
{
    return g_mock->xStreamBufferCreate( xBufferSizeBytes, xTriggerLevelBytes );
}

extern "C" size_t xStreamBufferSendFromISR( StreamBufferHandle_t xStreamBuffer,
                                            const void* pvTxData, size_t xDataLengthBytes,
                                            BaseType_t* pxHigherPriorityTaskWoken )
{
    return g_mock->StreamBufferSendFromISR( xStreamBuffer, pvTxData, xDataLengthBytes,
                                            pxHigherPriorityTaskWoken );
}

extern "C" size_t xStreamBufferReceive( StreamBufferHandle_t xStreamBuffer, void* pvRxData,
                                        size_t xBufferLengthBytes, TickType_t xTicksToWait )
{
    return g_mock->xStreamBufferReceive( xStreamBuffer, pvRxData, xBufferLengthBytes,
                                         xTicksToWait );
}

extern "C" size_t xStreamBufferSpacesAvailable( StreamBufferHandle_t xStreamBuffer )
{
    return g_mock->StreamBufferSpacesAvailable( xStreamBuffer );
}

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for hw_usb module tests.
 *
 * Resets the included module's static USB state and the mocked ST USB device
 * state before every test case.
 */
class HWUSBTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        g_mock = &mock;

        std::memset( &usb_state, 0, sizeof( usb_state ) );
        std::memset( &hUsbDeviceFS, 0, sizeof( hUsbDeviceFS ) );
        std::memset( &cdc_handle, 0, sizeof( cdc_handle ) );

        hUsbDeviceFS.pClassData = &cdc_handle;
        fake_stream             = reinterpret_cast<StreamBufferHandle_t>( &fake_stream_storage );
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    testing::StrictMock<MockHWUSB> mock;
    USBD_CDC_HandleTypeDef         cdc_handle          = {};
    uint8_t                        fake_stream_storage = 0U;
    StreamBufferHandle_t           fake_stream         = nullptr;
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWUSBTest, InitCreatesReceiveStreamAndClearsDroppedCount )
{
    usb_state.receive_stream_bytes_dropped = 123U;

    EXPECT_CALL( mock, xStreamBufferCreate( MAX_USB_RECEIVE_STREAM_BYTES,
                                            USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES ) )
        .WillOnce( testing::Return( fake_stream ) );

    EXPECT_TRUE( HW_USB_Init() );
    EXPECT_EQ( fake_stream, usb_state.receive_stream );
    EXPECT_EQ( 0U, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, InitReturnsFalseWhenReceiveStreamCreationFails )
{
    usb_state.receive_stream_bytes_dropped = 55U;

    EXPECT_CALL( mock, xStreamBufferCreate( MAX_USB_RECEIVE_STREAM_BYTES,
                                            USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES ) )
        .WillOnce( testing::Return( nullptr ) );

    EXPECT_FALSE( HW_USB_Init() );
    EXPECT_EQ( nullptr, usb_state.receive_stream );
    EXPECT_EQ( 55U, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, ReceiveFromISRReturnsWithoutActionForInvalidArguments )
{
    uint8_t  data[] = { 1U, 2U, 3U };
    uint32_t size   = sizeof( data );

    HW_USB_Receive_From_ISR( nullptr, &size );
    HW_USB_Receive_From_ISR( data, nullptr );

    size = 0U;
    HW_USB_Receive_From_ISR( data, &size );

    EXPECT_EQ( 0U, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, ReceiveFromISRCountsEntirePacketAsDroppedWhenStreamIsNotCreated )
{
    uint8_t  data[] = { 1U, 2U, 3U, 4U, 5U };
    uint32_t size   = sizeof( data );

    HW_USB_Receive_From_ISR( data, &size );

    EXPECT_EQ( size, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, ReceiveFromISRCopiesAllBytesToReceiveStreamAndYields )
{
    uint8_t  data[] = { 0x10U, 0x20U, 0x30U, 0x40U };
    uint32_t size   = sizeof( data );

    usb_state.receive_stream = fake_stream;

    EXPECT_CALL( mock, StreamBufferSendFromISR( fake_stream, testing::_, size, testing::_ ) )
        .WillOnce(
            testing::Invoke( [&]( StreamBufferHandle_t, const void* sent_data, size_t sent_length,
                                  BaseType_t* higher_priority_task_woken ) -> size_t {
                EXPECT_EQ( 0, std::memcmp( sent_data, data, sent_length ) );
                *higher_priority_task_woken = pdTRUE;
                return sent_length;
            } ) );

    HW_USB_Receive_From_ISR( data, &size );

    EXPECT_EQ( 0U, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, ReceiveFromISRCountsBytesNotWrittenToStreamAsDropped )
{
    uint8_t  data[] = { 1U, 2U, 3U, 4U, 5U, 6U };
    uint32_t size   = sizeof( data );

    usb_state.receive_stream               = fake_stream;
    usb_state.receive_stream_bytes_dropped = 10U;

    EXPECT_CALL( mock, StreamBufferSendFromISR( fake_stream, testing::_, size, testing::_ ) )
        .WillOnce( testing::Invoke( []( StreamBufferHandle_t, const void*, size_t,
                                        BaseType_t* higher_priority_task_woken ) -> size_t {
            *higher_priority_task_woken = pdFALSE;
            return 2U;
        } ) );

    HW_USB_Receive_From_ISR( data, &size );

    EXPECT_EQ( 14U, usb_state.receive_stream_bytes_dropped );
}

TEST_F( HWUSBTest, ReceiveReturnsZeroForInvalidArgumentsOrUninitialisedStream )
{
    uint8_t destination[4] = {};

    EXPECT_EQ( 0U, HW_USB_Receive( nullptr, sizeof( destination ) ) );
    EXPECT_EQ( 0U, HW_USB_Receive( destination, 0U ) );
    EXPECT_EQ( 0U, HW_USB_Receive( destination, sizeof( destination ) ) );
}

TEST_F( HWUSBTest, ReceiveReadsAvailableBytesWithoutBlocking )
{
    uint8_t destination[4] = {};
    uint8_t expected[]     = { 0xA1U, 0xB2U, 0xC3U };

    usb_state.receive_stream = fake_stream;

    EXPECT_CALL( mock, xStreamBufferReceive( fake_stream, destination, sizeof( destination ), 0U ) )
        .WillOnce( testing::Invoke(
            [&]( StreamBufferHandle_t, void* receive_destination, size_t, TickType_t ) -> size_t {
                std::memcpy( receive_destination, expected, sizeof( expected ) );
                return sizeof( expected );
            } ) );

    EXPECT_EQ( sizeof( expected ), HW_USB_Receive( destination, sizeof( destination ) ) );
    EXPECT_EQ( 0, std::memcmp( destination, expected, sizeof( expected ) ) );
}

TEST_F( HWUSBTest, ReceiveStreamDiagnosticFunctionsReturnZeroWhenStreamIsNotCreated )
{
    usb_state.receive_stream_bytes_dropped = 77U;

    EXPECT_EQ( 0U, HW_USB_Get_Receive_Stream_Used_Bytes() );
    EXPECT_EQ( 0U, HW_USB_Get_Receive_Stream_Free_Bytes() );
    EXPECT_EQ( 77U, HW_USB_Get_Receive_Stream_Dropped_Bytes() );
}

TEST_F( HWUSBTest, ReceiveStreamDiagnosticFunctionsUseFreeRTOSSpaceAvailable )
{
    usb_state.receive_stream               = fake_stream;
    usb_state.receive_stream_bytes_dropped = 12U;

    EXPECT_CALL( mock, StreamBufferSpacesAvailable( fake_stream ) )
        .WillOnce( testing::Return( 900U ) );
    EXPECT_EQ( 124U, HW_USB_Get_Receive_Stream_Used_Bytes() );

    EXPECT_CALL( mock, StreamBufferSpacesAvailable( fake_stream ) )
        .WillOnce( testing::Return( 900U ) );
    EXPECT_EQ( 900U, HW_USB_Get_Receive_Stream_Free_Bytes() );

    EXPECT_EQ( 12U, HW_USB_Get_Receive_Stream_Dropped_Bytes() );
}

TEST_F( HWUSBTest, TransmitReturnsFalseForNullPointer )
{
    EXPECT_FALSE( HW_USB_Transmit( nullptr, 1U ) );
}

TEST_F( HWUSBTest, TransmitTreatsZeroLengthAsSuccessfulNoOp )
{
    uint8_t data[] = { 1U };

    EXPECT_TRUE( HW_USB_Transmit( data, 0U ) );

    EXPECT_EQ( 0U, usb_state.transmit_num_buffered );
    EXPECT_EQ( 0U, usb_state.transmit_num_in_transmission );
}

TEST_F( HWUSBTest, TransmitCopiesDataIntoInternalBufferAndStartsTransferWhenIdle )
{
    uint8_t data[] = { 0x11U, 0x22U, 0x33U };

    EXPECT_CALL( mock, CDCTransmitFS( testing::_, sizeof( data ) ) )
        .WillOnce( testing::Invoke( [&]( uint8_t* transmit_buffer, uint16_t length ) -> uint8_t {
            EXPECT_EQ( &usb_state.transmit_buffer[0], transmit_buffer );
            EXPECT_EQ( 0, std::memcmp( transmit_buffer, data, length ) );
            return USBD_OK;
        } ) );

    EXPECT_TRUE( HW_USB_Transmit( data, sizeof( data ) ) );

    data[0] = 0x99U;
    EXPECT_EQ( 0x11U, usb_state.transmit_buffer[0] );
    EXPECT_EQ( 0x22U, usb_state.transmit_buffer[1] );
    EXPECT_EQ( 0x33U, usb_state.transmit_buffer[2] );

    EXPECT_EQ( sizeof( data ), usb_state.transmit_waiting_end );
    EXPECT_EQ( sizeof( data ), usb_state.transmit_num_buffered );
    EXPECT_EQ( sizeof( data ), usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 0U, usb_state.transmit_live_start );
    EXPECT_EQ( sizeof( data ), usb_state.transmit_live_end );
}

TEST_F( HWUSBTest, TransmitReturnsFalseWhenThereIsNotEnoughBufferSpace )
{
    uint8_t data[] = { 1U, 2U };

    usb_state.transmit_num_buffered = MAX_USB_TRANSMIT_BYTES - 1U;
    usb_state.transmit_waiting_end  = 100U;

    EXPECT_FALSE( HW_USB_Transmit( data, sizeof( data ) ) );

    EXPECT_EQ( MAX_USB_TRANSMIT_BYTES - 1U, usb_state.transmit_num_buffered );
    EXPECT_EQ( 100U, usb_state.transmit_waiting_end );
}

TEST_F( HWUSBTest, TransmitLeavesDataQueuedWhenCDCTransmitReturnsBusy )
{
    uint8_t data[] = { 0x01U, 0x02U, 0x03U, 0x04U };

    EXPECT_CALL( mock, CDCTransmitFS( testing::_, sizeof( data ) ) )
        .WillOnce( testing::Return( USBD_BUSY ) );

    EXPECT_TRUE( HW_USB_Transmit( data, sizeof( data ) ) );

    EXPECT_EQ( sizeof( data ), usb_state.transmit_num_buffered );
    EXPECT_EQ( 0U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 0U, usb_state.transmit_live_start );
    EXPECT_EQ( sizeof( data ), usb_state.transmit_waiting_end );
}

TEST_F( HWUSBTest, TransmitWrapsWriteIndexAndCopiesSecondPartToStartOfRing )
{
    std::array<uint8_t, 20U> data = {};

    for ( uint32_t i = 0U; i < data.size(); i++ )
    {
        data[i] = static_cast<uint8_t>( i + 1U );
    }

    usb_state.transmit_live_start  = MAX_USB_TRANSMIT_BYTES - 8U;
    usb_state.transmit_waiting_end = MAX_USB_TRANSMIT_BYTES - 8U;

    EXPECT_CALL( mock,
                 CDCTransmitFS( &usb_state.transmit_buffer[MAX_USB_TRANSMIT_BYTES - 8U], 8U ) )
        .WillOnce( testing::Return( USBD_OK ) );

    EXPECT_TRUE( HW_USB_Transmit( data.data(), data.size() ) );

    EXPECT_EQ( 0, std::memcmp( &usb_state.transmit_buffer[MAX_USB_TRANSMIT_BYTES - 8U], data.data(),
                               8U ) );
    EXPECT_EQ( 0, std::memcmp( &usb_state.transmit_buffer[0], &data[8], data.size() - 8U ) );
    EXPECT_EQ( data.size() - 8U, usb_state.transmit_waiting_end );
    EXPECT_EQ( 8U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( data.size(), usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, MonitorProcessDoesNothingWhenNoDataIsQueued )
{
    HW_USB_Monitor_Process();

    EXPECT_EQ( 0U, usb_state.transmit_num_buffered );
    EXPECT_EQ( 0U, usb_state.transmit_num_in_transmission );
}

TEST_F( HWUSBTest, MonitorProcessDoesNotAdvanceActiveTransferWhileCDCIsBusy )
{
    cdc_handle.TxState = 1U;

    usb_state.transmit_live_start          = 0U;
    usb_state.transmit_live_end            = 4U;
    usb_state.transmit_num_in_transmission = 4U;
    usb_state.transmit_num_buffered        = 10U;

    HW_USB_Monitor_Process();

    EXPECT_EQ( 0U, usb_state.transmit_live_start );
    EXPECT_EQ( 4U, usb_state.transmit_live_end );
    EXPECT_EQ( 4U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 10U, usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, MonitorProcessDoesNotAdvanceActiveTransferWhenCDCClassIsNotInitialised )
{
    hUsbDeviceFS.pClassData = nullptr;

    usb_state.transmit_num_in_transmission = 4U;
    usb_state.transmit_num_buffered        = 4U;

    HW_USB_Monitor_Process();

    EXPECT_EQ( 4U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 4U, usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, MonitorProcessAdvancesCompletedTransferAndStartsNextQueuedTransfer )
{
    for ( uint32_t i = 0U; i < 10U; i++ )
    {
        usb_state.transmit_buffer[i] = static_cast<uint8_t>( i );
    }

    cdc_handle.TxState = 0U;

    usb_state.transmit_live_start          = 0U;
    usb_state.transmit_live_end            = 4U;
    usb_state.transmit_waiting_end         = 10U;
    usb_state.transmit_num_in_transmission = 4U;
    usb_state.transmit_num_buffered        = 10U;

    EXPECT_CALL( mock, CDCTransmitFS( &usb_state.transmit_buffer[4], 6U ) )
        .WillOnce( testing::Return( USBD_OK ) );

    HW_USB_Monitor_Process();

    EXPECT_EQ( 4U, usb_state.transmit_live_start );
    EXPECT_EQ( 10U, usb_state.transmit_live_end );
    EXPECT_EQ( 6U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 6U, usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, MonitorProcessOnlyStartsContiguousTransferWhenQueuedDataWraps )
{
    testing::InSequence sequence;

    usb_state.transmit_live_start   = MAX_USB_TRANSMIT_BYTES - 12U;
    usb_state.transmit_waiting_end  = 8U;
    usb_state.transmit_num_buffered = 20U;

    EXPECT_CALL( mock,
                 CDCTransmitFS( &usb_state.transmit_buffer[MAX_USB_TRANSMIT_BYTES - 12U], 12U ) )
        .WillOnce( testing::Return( USBD_OK ) );

    HW_USB_Monitor_Process();

    EXPECT_EQ( MAX_USB_TRANSMIT_BYTES - 12U, usb_state.transmit_live_start );
    EXPECT_EQ( 0U, usb_state.transmit_live_end );
    EXPECT_EQ( 12U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 20U, usb_state.transmit_num_buffered );

    EXPECT_CALL( mock, CDCTransmitFS( &usb_state.transmit_buffer[0], 8U ) )
        .WillOnce( testing::Return( USBD_OK ) );

    cdc_handle.TxState = 0U;
    HW_USB_Monitor_Process();

    EXPECT_EQ( 0U, usb_state.transmit_live_start );
    EXPECT_EQ( 8U, usb_state.transmit_live_end );
    EXPECT_EQ( 8U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 8U, usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, MonitorProcessLeavesQueuedDataWhenStartingTransferFails )
{
    usb_state.transmit_live_start   = 20U;
    usb_state.transmit_waiting_end  = 30U;
    usb_state.transmit_num_buffered = 10U;

    EXPECT_CALL( mock, CDCTransmitFS( &usb_state.transmit_buffer[20], 10U ) )
        .WillOnce( testing::Return( USBD_FAIL ) );

    HW_USB_Monitor_Process();

    EXPECT_EQ( 20U, usb_state.transmit_live_start );
    EXPECT_EQ( 0U, usb_state.transmit_live_end );
    EXPECT_EQ( 0U, usb_state.transmit_num_in_transmission );
    EXPECT_EQ( 10U, usb_state.transmit_num_buffered );
}

TEST_F( HWUSBTest, TransmitIsCompleteReturnsFalseWhenCDCClassIsNotInitialised )
{
    hUsbDeviceFS.pClassData = nullptr;

    EXPECT_FALSE( HW_USB_Transmit_Is_Complete() );
}

TEST_F( HWUSBTest, TransmitIsCompleteReflectsCDCTxState )
{
    cdc_handle.TxState = 1U;
    EXPECT_FALSE( HW_USB_Transmit_Is_Complete() );

    cdc_handle.TxState = 0U;
    EXPECT_TRUE( HW_USB_Transmit_Is_Complete() );
}
