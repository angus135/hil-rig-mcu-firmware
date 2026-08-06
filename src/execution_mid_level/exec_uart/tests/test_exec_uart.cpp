/******************************************************************************
 *  File:       test_exec_uart.cpp
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit test harness for the mid-level execution UART driver.
 *
 *      The low-level HW_UART API is mocked so these tests verify exec-level
 *      sequencing, lifecycle state, transmit forwarding, RX span copying, and
 *      TX completion delegation without retesting the low-level UART driver.
 *
 *  Notes:
 *      This test target includes exec_uart.c directly. Do not also compile
 *      exec_uart.c as a separate source into the same test target.
 *
 *      DUT UART TX hot path calls preserve the valid-call contract. Invalid
 *      TX payload arguments are not tested here.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "exec_uart.h"
#include "logic_expander.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

#define TEST_EXEC_UART_RX_BUFFER_SIZE 16U

using ::testing::_;
using ::testing::DoAll;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwUart
{
public:
    MOCK_METHOD( bool, Rx_Is_Running, ( int ));
    MOCK_METHOD( bool, Rx_Stop, ( int ));
    MOCK_METHOD( bool, Configure_Channel, ( int, const HwUartPeripheralConfig_T* ));
    MOCK_METHOD( bool, Deconfigure_Channel, ( int ));
    MOCK_METHOD( bool, Rx_Start, ( int ));
    MOCK_METHOD( bool, Tx_Load_Buffer, ( int, const uint8_t*, uint32_t ) );
    MOCK_METHOD( bool, Tx_Trigger, ( int ));
    MOCK_METHOD( HwUartRxSpans_T, Rx_Peek, ( int ));
    MOCK_METHOD( void, Rx_Consume, ( int, uint32_t ) );
    MOCK_METHOD( bool, Is_Tx_Complete, ( int ));
};

class MockLogicExpander
{
public:
    MOCK_METHOD( LogicExpanderStatus_T, Load_Control_Bit, ( int, int, uint8_t, bool ));
    MOCK_METHOD( LogicExpanderStatus_T, Send_Control_Bits, () );
};

static MockHwUart*        g_mock_hw       = nullptr;
static MockLogicExpander* g_mock_expander = nullptr;

/**-----------------------------------------------------------------------------
 *  Private Helper Functions
 *------------------------------------------------------------------------------
 */

static ExecUartChannel_T TEST_EXEC_UART_Invalid_Channel( void )
{
    volatile uint32_t invalid_channel = EXEC_UART_CHANNEL_COUNT;
    return static_cast<ExecUartChannel_T>( invalid_channel );
}

static ExecUartConfig_T TEST_EXEC_UART_Make_Tx_Rx_Config( void )
{
    ExecUartConfig_T config = {};

    config.interface_mode = EXEC_UART_MODE_TTL_3V3;
    config.baud_rate      = 115200U;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.stop_bits      = HW_UART_STOP_BITS_1;
    config.parity         = HW_UART_PARITY_NONE;
    config.rx_enabled     = true;
    config.tx_enabled     = true;

    return config;
}

static ExecUartConfig_T TEST_EXEC_UART_Make_Tx_Only_Config( void )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    config.rx_enabled = false;
    config.tx_enabled = true;

    return config;
}

static ExecUartConfig_T TEST_EXEC_UART_Make_Rx_Only_Config( void )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    config.rx_enabled = true;
    config.tx_enabled = false;

    return config;
}

static HwUartRxSpans_T TEST_EXEC_UART_Make_Spans( const uint8_t* first_data, uint32_t first_length,
                                                  const uint8_t* second_data,
                                                  uint32_t       second_length )
{
    HwUartRxSpans_T spans          = {};
    spans.first_span.data          = first_data;
    spans.first_span.length_bytes  = first_length;
    spans.second_span.data         = second_data;
    spans.second_span.length_bytes = second_length;
    spans.total_length_bytes       = first_length + second_length;
    return spans;
}

/**-----------------------------------------------------------------------------
 *  Link seam: HW_UART functions delegated to GMock
 *------------------------------------------------------------------------------
 */

// NOLINTBEGIN

extern "C" bool HW_UART_Rx_Is_Running( HwUartChannel_T channel )
{
    return g_mock_hw->Rx_Is_Running( channel );
}

extern "C" bool HW_UART_Rx_Stop( HwUartChannel_T channel )
{
    return g_mock_hw->Rx_Stop( channel );
}

extern "C" bool HW_UART_Configure_Channel( HwUartChannel_T                 channel,
                                           const HwUartPeripheralConfig_T* config )
{
    return g_mock_hw->Configure_Channel( channel, config );
}

extern "C" bool HW_UART_Deconfigure_Channel( HwUartChannel_T channel )
{
    return g_mock_hw->Deconfigure_Channel( channel );
}

extern "C" LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander,
                                                                  LogicExpanderPort_T  port,
                                                                  uint8_t bit_index, bool value )
{
    return g_mock_expander->Load_Control_Bit( expander, port, bit_index, value );
}

extern "C" LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock_expander->Send_Control_Bits();
}

extern "C" bool HW_UART_Rx_Start( HwUartChannel_T channel )
{
    return g_mock_hw->Rx_Start( channel );
}

extern "C" bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data,
                                        uint32_t length_bytes )
{
    return g_mock_hw->Tx_Load_Buffer( channel, data, length_bytes );
}

extern "C" bool HW_UART_Tx_Trigger( HwUartChannel_T channel )
{
    return g_mock_hw->Tx_Trigger( channel );
}

extern "C" HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel )
{
    return g_mock_hw->Rx_Peek( channel );
}

extern "C" void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume )
{
    g_mock_hw->Rx_Consume( channel, bytes_to_consume );
}

extern "C" bool HW_UART_Is_Tx_Complete( HwUartChannel_T channel )
{
    return g_mock_hw->Is_Tx_Complete( channel );
}

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Implementation Under Test
 *------------------------------------------------------------------------------
 */

extern "C"
{
#include "exec_uart.c"
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ExecUARTTest : public ::testing::Test
{
protected:
    NiceMock<MockHwUart>        mock_hw;
    NiceMock<MockLogicExpander> mock_expander;

    static uint8_t s_first_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE];
    static uint8_t s_second_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE];

    void SetUp( void ) override
    {
        g_mock_hw       = &mock_hw;
        g_mock_expander = &mock_expander;

        ON_CALL( mock_hw, Rx_Is_Running( _ ) ).WillByDefault( Return( false ) );
        ON_CALL( mock_hw, Rx_Stop( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Configure_Channel( _, _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Deconfigure_Channel( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Rx_Start( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Tx_Load_Buffer( _, _, _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Tx_Trigger( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_hw, Rx_Peek( _ ) )
            .WillByDefault( Return( TEST_EXEC_UART_Make_Spans( nullptr, 0U, nullptr, 0U ) ) );
        ON_CALL( mock_hw, Is_Tx_Complete( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( mock_expander, Load_Control_Bit( _, _, _, _ ) )
            .WillByDefault( Return( LOGIC_EXPANDER_STATUS_OK ) );
        ON_CALL( mock_expander, Send_Control_Bits() )
            .WillByDefault( Return( LOGIC_EXPANDER_STATUS_OK ) );

        memset( s_first_span_data, 0, sizeof( s_first_span_data ) );
        memset( s_second_span_data, 0, sizeof( s_second_span_data ) );
        memset( exec_uart_channel_states, 0, sizeof( exec_uart_channel_states ) );
    }

    void TearDown( void ) override
    {
        g_mock_hw       = nullptr;
        g_mock_expander = nullptr;
    }
};

uint8_t ExecUARTTest::s_first_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE]  = {};
uint8_t ExecUARTTest::s_second_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE] = {};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecUARTTest, PrivateValidChannelAcceptsChannel1AndChannel2 )
{
    EXPECT_TRUE( EXEC_UART_Is_Valid_Channel( EXEC_UART_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_UART_Is_Valid_Channel( EXEC_UART_CHANNEL_2 ) );
}

TEST_F( ExecUARTTest, PrivateValidChannelRejectsOutOfRangeChannel )
{
    EXPECT_FALSE( EXEC_UART_Is_Valid_Channel( TEST_EXEC_UART_Invalid_Channel() ) );
}

TEST_F( ExecUARTTest, PrivateHardwareSelectionMapsChannel1Ttl3V3ToGpa4AndGpa5 )
{
    InSequence sequence;
    EXPECT_CALL( mock_expander, Load_Control_Bit( LOGIC_EXPANDER_DEVICE_UART_PWR,
                                                  LOGIC_EXPANDER_PORT_A, 4U, true ) );
    EXPECT_CALL( mock_expander, Load_Control_Bit( LOGIC_EXPANDER_DEVICE_UART_PWR,
                                                  LOGIC_EXPANDER_PORT_A, 5U, false ) );
    EXPECT_CALL( mock_expander, Send_Control_Bits() );

    EXPECT_TRUE(
        EXEC_UART_Apply_Static_Hardware_Selection( EXEC_UART_CHANNEL_1, EXEC_UART_MODE_TTL_3V3 ) );
}

TEST_F( ExecUARTTest, PrivateHardwareSelectionMapsChannel2Rs232ToGpa6AndGpa7 )
{
    InSequence sequence;
    EXPECT_CALL( mock_expander, Load_Control_Bit( LOGIC_EXPANDER_DEVICE_UART_PWR,
                                                  LOGIC_EXPANDER_PORT_A, 6U, false ) );
    EXPECT_CALL( mock_expander, Load_Control_Bit( LOGIC_EXPANDER_DEVICE_UART_PWR,
                                                  LOGIC_EXPANDER_PORT_A, 7U, true ) );
    EXPECT_CALL( mock_expander, Send_Control_Bits() );

    EXPECT_TRUE(
        EXEC_UART_Apply_Static_Hardware_Selection( EXEC_UART_CHANNEL_2, EXEC_UART_MODE_RS232 ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationRejectsNullConfig )
{
    EXPECT_CALL( mock_hw, Rx_Is_Running( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, nullptr ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationRejectsInvalidChannel )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    EXPECT_CALL( mock_hw, Rx_Is_Running( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( TEST_EXEC_UART_Invalid_Channel(), &config ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationConfiguresTxOnlyWithoutStartingRx )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Only_Config();

    EXPECT_CALL( mock_hw, Rx_Is_Running( EXEC_UART_CHANNEL_1 ) ).Times( 1 );
    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw,
                 Configure_Channel(
                     EXEC_UART_CHANNEL_1,
                     Pointee( AllOf( Field( &HwUartPeripheralConfig_T::baud_rate, 115200U ),
                                     Field( &HwUartPeripheralConfig_T::rx_enabled, false ),
                                     Field( &HwUartPeripheralConfig_T::tx_enabled, true ) ) ) ) )
        .Times( 1 );
    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).Times( 0 );

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );

    EXPECT_TRUE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured );
    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].rx_enabled );
    EXPECT_TRUE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, ApplyConfigurationStartsRxWhenRequested )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Rx_Only_Config();

    {
        InSequence seq;
        EXPECT_CALL( mock_hw, Rx_Is_Running( EXEC_UART_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw, Configure_Channel( EXEC_UART_CHANNEL_1, _ ) );
        EXPECT_CALL( mock_hw, Rx_Start( EXEC_UART_CHANNEL_1 ) );
    }

    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).Times( 0 );

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );

    EXPECT_TRUE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured );
    EXPECT_TRUE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].rx_enabled );
    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, ApplyConfigurationStopsRxBeforeReconfiguringWhenRxAlreadyRunning )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    {
        InSequence seq;
        EXPECT_CALL( mock_hw, Rx_Is_Running( EXEC_UART_CHANNEL_1 ) ).WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw, Rx_Stop( EXEC_UART_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw, Configure_Channel( EXEC_UART_CHANNEL_1, _ ) );
        EXPECT_CALL( mock_hw, Rx_Start( EXEC_UART_CHANNEL_1 ) );
    }

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfRxStopFails )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    EXPECT_CALL( mock_hw, Rx_Is_Running( EXEC_UART_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfLowLevelConfigurationFails )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );

    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfRxStartFails )
{
    ExecUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    EXPECT_CALL( mock_hw, Rx_Start( _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( EXEC_UART_CHANNEL_1, &config ) );

    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured );
}

TEST_F( ExecUARTTest, DeconfigureRejectsInvalidChannel )
{
    EXPECT_CALL( mock_hw, Rx_Is_Running( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Deconfigure_Channel( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Deconfigure( TEST_EXEC_UART_Invalid_Channel() ) );
}

TEST_F( ExecUARTTest, DeconfigureStopsLowLevelChannelWhenRxIsNotRunning )
{
    exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured = true;
    exec_uart_channel_states[EXEC_UART_CHANNEL_1].rx_enabled    = true;
    exec_uart_channel_states[EXEC_UART_CHANNEL_1].tx_enabled    = true;

    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Deconfigure_Channel( HW_UART_CHANNEL_1 ) ).Times( 1 );

    ASSERT_TRUE( EXEC_UART_Deconfigure( EXEC_UART_CHANNEL_1 ) );

    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].is_configured );
    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].rx_enabled );
    EXPECT_FALSE( exec_uart_channel_states[EXEC_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, DeconfigureStopsRxBeforeDeconfiguringLowLevelChannel )
{
    {
        InSequence seq;
        EXPECT_CALL( mock_hw, Rx_Is_Running( EXEC_UART_CHANNEL_1 ) ).WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw, Rx_Stop( EXEC_UART_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw, Deconfigure_Channel( HW_UART_CHANNEL_1 ) );
    }

    ASSERT_TRUE( EXEC_UART_Deconfigure( EXEC_UART_CHANNEL_1 ) );
}

TEST_F( ExecUARTTest, DeconfigureReturnsFalseIfRxStopFails )
{
    EXPECT_CALL( mock_hw, Rx_Is_Running( _ ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw, Rx_Stop( _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw, Deconfigure_Channel( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Deconfigure( EXEC_UART_CHANNEL_1 ) );
}

TEST_F( ExecUARTTest, DeconfigureReturnsFalseIfLowLevelDeconfigureFails )
{
    EXPECT_CALL( mock_hw, Deconfigure_Channel( _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_UART_Deconfigure( EXEC_UART_CHANNEL_1 ) );
}

TEST_F( ExecUARTTest, TransmitLoadsBufferThenTriggersPump )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    {
        InSequence seq;
        EXPECT_CALL( mock_hw, Tx_Load_Buffer( EXEC_UART_CHANNEL_1, payload, sizeof( payload ) ) );
        EXPECT_CALL( mock_hw, Tx_Trigger( EXEC_UART_CHANNEL_1 ) );
    }

    EXPECT_TRUE( EXEC_UART_Transmit( EXEC_UART_CHANNEL_1, payload, sizeof( payload ) ) );
}

TEST_F( ExecUARTTest, TransmitReturnsFalseAndDoesNotTriggerWhenLoadFails )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    EXPECT_CALL( mock_hw, Tx_Load_Buffer( _, _, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw, Tx_Trigger( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_UART_Transmit( EXEC_UART_CHANNEL_1, payload, sizeof( payload ) ) );
}

TEST_F( ExecUARTTest, TransmitReturnsFalseWhenTriggerFails )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    EXPECT_CALL( mock_hw, Tx_Trigger( _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_UART_Transmit( EXEC_UART_CHANNEL_1, payload, sizeof( payload ) ) );
}

TEST_F( ExecUARTTest, ReadRejectsNullDestination )
{
    EXPECT_CALL( mock_hw, Rx_Peek( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Consume( _, _ ) ).Times( 0 );

    uint32_t bytes_read = 123U;

    EXPECT_FALSE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, nullptr, 4U, &bytes_read ) );
}

TEST_F( ExecUARTTest, ReadRejectsNullBytesRead )
{
    EXPECT_CALL( mock_hw, Rx_Peek( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Consume( _, _ ) ).Times( 0 );

    uint8_t dest[4] = {};

    EXPECT_FALSE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), nullptr ) );
}

TEST_F( ExecUARTTest, ReadWithZeroDestinationSizeReturnsZeroWithoutPeeking )
{
    EXPECT_CALL( mock_hw, Rx_Peek( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Rx_Consume( _, _ ) ).Times( 0 );

    uint8_t  dest[4]    = {};
    uint32_t bytes_read = 123U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, 0U, &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
}

TEST_F( ExecUARTTest, ReadReturnsZeroWhenNoDataAvailable )
{
    EXPECT_CALL( mock_hw, Rx_Peek( EXEC_UART_CHANNEL_1 ) )
        .WillOnce( Return( TEST_EXEC_UART_Make_Spans( nullptr, 0U, nullptr, 0U ) ) );
    EXPECT_CALL( mock_hw, Rx_Consume( _, _ ) ).Times( 0 );

    uint8_t  dest[4]    = {};
    uint32_t bytes_read = 123U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
}

TEST_F( ExecUARTTest, ReadCopiesSingleSpanAndConsumesCopiedBytes )
{
    s_first_span_data[0] = 10U;
    s_first_span_data[1] = 11U;
    s_first_span_data[2] = 12U;

    EXPECT_CALL( mock_hw, Rx_Peek( EXEC_UART_CHANNEL_1 ) )
        .WillOnce( Return( TEST_EXEC_UART_Make_Spans( s_first_span_data, 3U, nullptr, 0U ) ) );
    EXPECT_CALL( mock_hw, Rx_Consume( EXEC_UART_CHANNEL_1, 3U ) ).Times( 1 );

    uint8_t  dest[8]    = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 3U );
    EXPECT_EQ( dest[0], 10U );
    EXPECT_EQ( dest[1], 11U );
    EXPECT_EQ( dest[2], 12U );
}

TEST_F( ExecUARTTest, ReadCopiesOnlyDestinationSizeFromFirstSpan )
{
    s_first_span_data[0] = 10U;
    s_first_span_data[1] = 11U;
    s_first_span_data[2] = 12U;

    EXPECT_CALL( mock_hw, Rx_Peek( _ ) )
        .WillOnce( Return( TEST_EXEC_UART_Make_Spans( s_first_span_data, 3U, nullptr, 0U ) ) );
    EXPECT_CALL( mock_hw, Rx_Consume( _, 2U ) ).Times( 1 );

    uint8_t  dest[2]    = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 2U );
    EXPECT_EQ( dest[0], 10U );
    EXPECT_EQ( dest[1], 11U );
}

TEST_F( ExecUARTTest, ReadCopiesWrappedSpansInOrder )
{
    s_first_span_data[0]  = 1U;
    s_first_span_data[1]  = 2U;
    s_second_span_data[0] = 3U;
    s_second_span_data[1] = 4U;

    EXPECT_CALL( mock_hw, Rx_Peek( _ ) )
        .WillOnce(
            Return( TEST_EXEC_UART_Make_Spans( s_first_span_data, 2U, s_second_span_data, 2U ) ) );
    EXPECT_CALL( mock_hw, Rx_Consume( _, 4U ) ).Times( 1 );

    uint8_t  dest[8]    = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 4U );
    EXPECT_EQ( dest[0], 1U );
    EXPECT_EQ( dest[1], 2U );
    EXPECT_EQ( dest[2], 3U );
    EXPECT_EQ( dest[3], 4U );
}

TEST_F( ExecUARTTest, ReadCopiesPartialWrappedSecondSpanWhenDestinationIsLimited )
{
    s_first_span_data[0]  = 1U;
    s_first_span_data[1]  = 2U;
    s_second_span_data[0] = 3U;
    s_second_span_data[1] = 4U;

    EXPECT_CALL( mock_hw, Rx_Peek( _ ) )
        .WillOnce(
            Return( TEST_EXEC_UART_Make_Spans( s_first_span_data, 2U, s_second_span_data, 2U ) ) );
    EXPECT_CALL( mock_hw, Rx_Consume( _, 3U ) ).Times( 1 );

    uint8_t  dest[3]    = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( EXEC_UART_Read( EXEC_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 3U );
    EXPECT_EQ( dest[0], 1U );
    EXPECT_EQ( dest[1], 2U );
    EXPECT_EQ( dest[2], 3U );
}

TEST_F( ExecUARTTest, IsTxCompleteDelegatesToLowLevelDriver )
{
    EXPECT_CALL( mock_hw, Is_Tx_Complete( EXEC_UART_CHANNEL_2 ) ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_UART_Is_Tx_Complete( EXEC_UART_CHANNEL_2 ) );
}

TEST_F( ExecUARTTest, IsTxCompleteReturnsFalseWhenLowLevelDriverReportsIncomplete )
{
    EXPECT_CALL( mock_hw, Is_Tx_Complete( EXEC_UART_CHANNEL_1 ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_UART_Is_Tx_Complete( EXEC_UART_CHANNEL_1 ) );
}
