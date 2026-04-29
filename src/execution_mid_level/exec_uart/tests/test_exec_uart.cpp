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
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

#define TEST_EXEC_UART_RX_BUFFER_SIZE 16U

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

static bool mock_hw_rx_is_running_result[HW_UART_CHANNEL_COUNT];

static bool mock_hw_rx_stop_result;
static bool mock_hw_configure_result;
static bool mock_hw_rx_start_result;
static bool mock_hw_tx_load_result;
static bool mock_hw_tx_trigger_result;
static bool mock_hw_tx_complete_result;

static uint32_t mock_hw_rx_is_running_count;
static uint32_t mock_hw_rx_stop_count;
static uint32_t mock_hw_configure_count;
static uint32_t mock_hw_rx_start_count;
static uint32_t mock_hw_tx_load_count;
static uint32_t mock_hw_tx_trigger_count;
static uint32_t mock_hw_rx_peek_count;
static uint32_t mock_hw_rx_consume_count;
static uint32_t mock_hw_tx_complete_count;

static uint32_t mock_sequence_counter;
static uint32_t mock_hw_rx_is_running_order;
static uint32_t mock_hw_rx_stop_order;
static uint32_t mock_hw_configure_order;
static uint32_t mock_hw_rx_start_order;
static uint32_t mock_hw_tx_load_order;
static uint32_t mock_hw_tx_trigger_order;

static HwUartChannel_T mock_hw_last_channel;
static HwUartConfig_T  mock_hw_last_config;

static const uint8_t* mock_hw_last_tx_data;
static uint32_t       mock_hw_last_tx_length;

static HwUartRxSpans_T mock_hw_rx_spans;
static uint32_t        mock_hw_last_consume_count;

static uint8_t mock_rx_first_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE];
static uint8_t mock_rx_second_span_data[TEST_EXEC_UART_RX_BUFFER_SIZE];

/**-----------------------------------------------------------------------------
 *  Private Helper Functions
 *------------------------------------------------------------------------------
 */

static uint32_t TEST_EXEC_UART_Channel_To_Index( HwUartChannel_T channel )
{
    return static_cast<uint32_t>( channel );
}

static HwUartChannel_T TEST_EXEC_UART_Invalid_Channel( void )
{
    volatile uint32_t invalid_channel = HW_UART_CHANNEL_COUNT;
    return static_cast<HwUartChannel_T>( invalid_channel );
}

static HwUartConfig_T TEST_EXEC_UART_Make_Tx_Rx_Config( void )
{
    HwUartConfig_T config = {};

    config.interface_mode = HW_UART_MODE_TTL_3V3;
    config.baud_rate      = 115200U;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.stop_bits      = HW_UART_STOP_BITS_1;
    config.parity         = HW_UART_PARITY_NONE;
    config.rx_enabled     = true;
    config.tx_enabled     = true;

    return config;
}

static HwUartConfig_T TEST_EXEC_UART_Make_Tx_Only_Config( void )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    config.rx_enabled = false;
    config.tx_enabled = true;

    return config;
}

static HwUartConfig_T TEST_EXEC_UART_Make_Rx_Only_Config( void )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    config.rx_enabled = true;
    config.tx_enabled = false;

    return config;
}

static void TEST_EXEC_UART_Set_Rx_Spans( const uint8_t* first_data, uint32_t first_length,
                                         const uint8_t* second_data, uint32_t second_length )
{
    mock_hw_rx_spans.first_span.data          = first_data;
    mock_hw_rx_spans.first_span.length_bytes  = first_length;
    mock_hw_rx_spans.second_span.data         = second_data;
    mock_hw_rx_spans.second_span.length_bytes = second_length;
    mock_hw_rx_spans.total_length_bytes       = first_length + second_length;
}

/**-----------------------------------------------------------------------------
 *  Link seam: mocked function definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

extern "C" bool HW_UART_Rx_Is_Running( HwUartChannel_T channel )
{
    mock_hw_rx_is_running_count++;
    mock_hw_rx_is_running_order = ++mock_sequence_counter;
    mock_hw_last_channel        = channel;

    uint32_t index = TEST_EXEC_UART_Channel_To_Index( channel );

    if ( index >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    return mock_hw_rx_is_running_result[index];
}

extern "C" bool HW_UART_Rx_Stop( HwUartChannel_T channel )
{
    mock_hw_rx_stop_count++;
    mock_hw_rx_stop_order = ++mock_sequence_counter;
    mock_hw_last_channel  = channel;

    return mock_hw_rx_stop_result;
}

extern "C" bool HW_UART_Configure_Channel( HwUartChannel_T channel, const HwUartConfig_T* config )
{
    mock_hw_configure_count++;
    mock_hw_configure_order = ++mock_sequence_counter;
    mock_hw_last_channel    = channel;

    if ( config != nullptr )
    {
        mock_hw_last_config = *config;
    }

    return mock_hw_configure_result;
}

extern "C" bool HW_UART_Rx_Start( HwUartChannel_T channel )
{
    mock_hw_rx_start_count++;
    mock_hw_rx_start_order = ++mock_sequence_counter;
    mock_hw_last_channel   = channel;

    return mock_hw_rx_start_result;
}

extern "C" bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data,
                                        uint32_t length_bytes )
{
    mock_hw_tx_load_count++;
    mock_hw_tx_load_order  = ++mock_sequence_counter;
    mock_hw_last_channel   = channel;
    mock_hw_last_tx_data   = data;
    mock_hw_last_tx_length = length_bytes;

    return mock_hw_tx_load_result;
}

extern "C" bool HW_UART_Tx_Trigger( HwUartChannel_T channel )
{
    mock_hw_tx_trigger_count++;
    mock_hw_tx_trigger_order = ++mock_sequence_counter;
    mock_hw_last_channel     = channel;

    return mock_hw_tx_trigger_result;
}

extern "C" HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel )
{
    mock_hw_rx_peek_count++;
    mock_hw_last_channel = channel;

    return mock_hw_rx_spans;
}

extern "C" void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume )
{
    mock_hw_rx_consume_count++;
    mock_hw_last_channel       = channel;
    mock_hw_last_consume_count = bytes_to_consume;
}

extern "C" bool HW_UART_Is_Tx_Complete( HwUartChannel_T channel )
{
    mock_hw_tx_complete_count++;
    mock_hw_last_channel = channel;

    return mock_hw_tx_complete_result;
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

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup and teardown environment for all test cases.
 */
class ExecUARTTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        mock_hw_rx_is_running_result[HW_UART_CHANNEL_1] = false;
        mock_hw_rx_is_running_result[HW_UART_CHANNEL_2] = false;

        mock_hw_rx_stop_result     = true;
        mock_hw_configure_result   = true;
        mock_hw_rx_start_result    = true;
        mock_hw_tx_load_result     = true;
        mock_hw_tx_trigger_result  = true;
        mock_hw_tx_complete_result = true;

        mock_hw_rx_is_running_count = 0U;
        mock_hw_rx_stop_count       = 0U;
        mock_hw_configure_count     = 0U;
        mock_hw_rx_start_count      = 0U;
        mock_hw_tx_load_count       = 0U;
        mock_hw_tx_trigger_count    = 0U;
        mock_hw_rx_peek_count       = 0U;
        mock_hw_rx_consume_count    = 0U;
        mock_hw_tx_complete_count   = 0U;

        mock_sequence_counter       = 0U;
        mock_hw_rx_is_running_order = 0U;
        mock_hw_rx_stop_order       = 0U;
        mock_hw_configure_order     = 0U;
        mock_hw_rx_start_order      = 0U;
        mock_hw_tx_load_order       = 0U;
        mock_hw_tx_trigger_order    = 0U;

        mock_hw_last_channel = HW_UART_CHANNEL_1;
        memset( &mock_hw_last_config, 0, sizeof( mock_hw_last_config ) );

        mock_hw_last_tx_data   = nullptr;
        mock_hw_last_tx_length = 0U;

        memset( &mock_hw_rx_spans, 0, sizeof( mock_hw_rx_spans ) );
        memset( mock_rx_first_span_data, 0, sizeof( mock_rx_first_span_data ) );
        memset( mock_rx_second_span_data, 0, sizeof( mock_rx_second_span_data ) );

        mock_hw_last_consume_count = 0U;

        memset( exec_uart_channel_states, 0, sizeof( exec_uart_channel_states ) );
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecUARTTest, PrivateDisabledConfigHasCanonicalDisabledValues )
{
    HwUartConfig_T config = EXEC_UART_Get_Disabled_Config();

    EXPECT_EQ( config.interface_mode, HW_UART_MODE_DISABLED );
    EXPECT_EQ( config.baud_rate, 0U );
    EXPECT_EQ( config.word_length, HW_UART_WORD_LENGTH_8_BITS );
    EXPECT_EQ( config.stop_bits, HW_UART_STOP_BITS_1 );
    EXPECT_EQ( config.parity, HW_UART_PARITY_NONE );
    EXPECT_FALSE( config.rx_enabled );
    EXPECT_FALSE( config.tx_enabled );
}

TEST_F( ExecUARTTest, PrivateValidChannelAcceptsChannel1AndChannel2 )
{
    EXPECT_TRUE( EXEC_UART_Is_Valid_Channel( HW_UART_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_UART_Is_Valid_Channel( HW_UART_CHANNEL_2 ) );
}

TEST_F( ExecUARTTest, PrivateValidChannelRejectsOutOfRangeChannel )
{
    EXPECT_FALSE( EXEC_UART_Is_Valid_Channel( TEST_EXEC_UART_Invalid_Channel() ) );
}

TEST_F( ExecUARTTest, ApplyConfigurationRejectsNullConfig )
{
    EXPECT_FALSE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, nullptr ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 0U );
    EXPECT_EQ( mock_hw_rx_start_count, 0U );
}

TEST_F( ExecUARTTest, ApplyConfigurationRejectsInvalidChannel )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( TEST_EXEC_UART_Invalid_Channel(), &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 0U );
    EXPECT_EQ( mock_hw_rx_start_count, 0U );
}

TEST_F( ExecUARTTest, ApplyConfigurationConfiguresTxOnlyWithoutStartingRx )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Only_Config();

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
    EXPECT_EQ( mock_hw_rx_start_count, 0U );

    EXPECT_EQ( mock_hw_last_config.interface_mode, HW_UART_MODE_TTL_3V3 );
    EXPECT_EQ( mock_hw_last_config.baud_rate, 115200U );
    EXPECT_FALSE( mock_hw_last_config.rx_enabled );
    EXPECT_TRUE( mock_hw_last_config.tx_enabled );

    EXPECT_TRUE( exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured );
    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].rx_enabled );
    EXPECT_TRUE( exec_uart_channel_states[HW_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, ApplyConfigurationStartsRxWhenRequested )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Rx_Only_Config();

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
    EXPECT_EQ( mock_hw_rx_start_count, 1U );

    EXPECT_LT( mock_hw_configure_order, mock_hw_rx_start_order );

    EXPECT_TRUE( exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured );
    EXPECT_TRUE( exec_uart_channel_states[HW_UART_CHANNEL_1].rx_enabled );
    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, ApplyConfigurationStopsRxBeforeReconfiguringWhenRxAlreadyRunning )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    mock_hw_rx_is_running_result[HW_UART_CHANNEL_1] = true;

    ASSERT_TRUE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
    EXPECT_EQ( mock_hw_rx_start_count, 1U );

    EXPECT_LT( mock_hw_rx_is_running_order, mock_hw_rx_stop_order );
    EXPECT_LT( mock_hw_rx_stop_order, mock_hw_configure_order );
    EXPECT_LT( mock_hw_configure_order, mock_hw_rx_start_order );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfRxStopFails )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    mock_hw_rx_is_running_result[HW_UART_CHANNEL_1] = true;
    mock_hw_rx_stop_result                          = false;

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 0U );
    EXPECT_EQ( mock_hw_rx_start_count, 0U );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfLowLevelConfigurationFails )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    mock_hw_configure_result = false;

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
    EXPECT_EQ( mock_hw_rx_start_count, 0U );

    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured );
}

TEST_F( ExecUARTTest, ApplyConfigurationReturnsFalseIfRxStartFails )
{
    HwUartConfig_T config = TEST_EXEC_UART_Make_Tx_Rx_Config();

    mock_hw_rx_start_result = false;

    EXPECT_FALSE( EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
    EXPECT_EQ( mock_hw_rx_start_count, 1U );

    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured );
}

TEST_F( ExecUARTTest, DeconfigureRejectsInvalidChannel )
{
    EXPECT_FALSE( EXEC_UART_Deconfigure( TEST_EXEC_UART_Invalid_Channel() ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 0U );
    EXPECT_EQ( mock_hw_rx_stop_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 0U );
}

TEST_F( ExecUARTTest, DeconfigureAppliesDisabledConfigWhenRxIsNotRunning )
{
    exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured = true;
    exec_uart_channel_states[HW_UART_CHANNEL_1].rx_enabled    = true;
    exec_uart_channel_states[HW_UART_CHANNEL_1].tx_enabled    = true;

    ASSERT_TRUE( EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 0U );
    EXPECT_EQ( mock_hw_configure_count, 1U );

    EXPECT_EQ( mock_hw_last_config.interface_mode, HW_UART_MODE_DISABLED );
    EXPECT_EQ( mock_hw_last_config.baud_rate, 0U );
    EXPECT_FALSE( mock_hw_last_config.rx_enabled );
    EXPECT_FALSE( mock_hw_last_config.tx_enabled );

    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].is_configured );
    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].rx_enabled );
    EXPECT_FALSE( exec_uart_channel_states[HW_UART_CHANNEL_1].tx_enabled );
}

TEST_F( ExecUARTTest, DeconfigureStopsRxBeforeApplyingDisabledConfigWhenRxIsRunning )
{
    mock_hw_rx_is_running_result[HW_UART_CHANNEL_1] = true;

    ASSERT_TRUE( EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 1U );

    EXPECT_LT( mock_hw_rx_is_running_order, mock_hw_rx_stop_order );
    EXPECT_LT( mock_hw_rx_stop_order, mock_hw_configure_order );
}

TEST_F( ExecUARTTest, DeconfigureReturnsFalseIfRxStopFails )
{
    mock_hw_rx_is_running_result[HW_UART_CHANNEL_1] = true;
    mock_hw_rx_stop_result                          = false;

    EXPECT_FALSE( EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_rx_stop_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 0U );
}

TEST_F( ExecUARTTest, DeconfigureReturnsFalseIfDisabledConfigFails )
{
    mock_hw_configure_result = false;

    EXPECT_FALSE( EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( mock_hw_rx_is_running_count, 1U );
    EXPECT_EQ( mock_hw_configure_count, 1U );
}

TEST_F( ExecUARTTest, TransmitLoadsBufferThenTriggersPump )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    ASSERT_TRUE( EXEC_UART_Transmit( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    EXPECT_EQ( mock_hw_tx_load_count, 1U );
    EXPECT_EQ( mock_hw_tx_trigger_count, 1U );
    EXPECT_EQ( mock_hw_last_channel, HW_UART_CHANNEL_1 );
    EXPECT_EQ( mock_hw_last_tx_data, payload );
    EXPECT_EQ( mock_hw_last_tx_length, sizeof( payload ) );

    EXPECT_LT( mock_hw_tx_load_order, mock_hw_tx_trigger_order );
}

TEST_F( ExecUARTTest, TransmitReturnsFalseAndDoesNotTriggerWhenLoadFails )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    mock_hw_tx_load_result = false;

    EXPECT_FALSE( EXEC_UART_Transmit( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    EXPECT_EQ( mock_hw_tx_load_count, 1U );
    EXPECT_EQ( mock_hw_tx_trigger_count, 0U );
}

TEST_F( ExecUARTTest, TransmitReturnsFalseWhenTriggerFails )
{
    uint8_t payload[3] = { 1U, 2U, 3U };

    mock_hw_tx_trigger_result = false;

    EXPECT_FALSE( EXEC_UART_Transmit( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    EXPECT_EQ( mock_hw_tx_load_count, 1U );
    EXPECT_EQ( mock_hw_tx_trigger_count, 1U );
}

TEST_F( ExecUARTTest, ReadRejectsNullDestination )
{
    uint32_t bytes_read = 123U;

    EXPECT_FALSE( EXEC_UART_Read( HW_UART_CHANNEL_1, nullptr, 4U, &bytes_read ) );

    EXPECT_EQ( mock_hw_rx_peek_count, 0U );
    EXPECT_EQ( mock_hw_rx_consume_count, 0U );
}

TEST_F( ExecUARTTest, ReadRejectsNullBytesRead )
{
    uint8_t dest[4] = {};

    EXPECT_FALSE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), nullptr ) );

    EXPECT_EQ( mock_hw_rx_peek_count, 0U );
    EXPECT_EQ( mock_hw_rx_consume_count, 0U );
}

TEST_F( ExecUARTTest, ReadWithZeroDestinationSizeReturnsZeroWithoutPeeking )
{
    uint8_t  dest[4]    = {};
    uint32_t bytes_read = 123U;

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, 0U, &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
    EXPECT_EQ( mock_hw_rx_peek_count, 0U );
    EXPECT_EQ( mock_hw_rx_consume_count, 0U );
}

TEST_F( ExecUARTTest, ReadReturnsZeroWhenNoDataAvailable )
{
    uint8_t  dest[4]    = {};
    uint32_t bytes_read = 123U;

    TEST_EXEC_UART_Set_Rx_Spans( nullptr, 0U, nullptr, 0U );

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
    EXPECT_EQ( mock_hw_rx_peek_count, 1U );
    EXPECT_EQ( mock_hw_rx_consume_count, 0U );
}

TEST_F( ExecUARTTest, ReadCopiesSingleSpanAndConsumesCopiedBytes )
{
    uint8_t  dest[8]    = {};
    uint32_t bytes_read = 0U;

    mock_rx_first_span_data[0] = 10U;
    mock_rx_first_span_data[1] = 11U;
    mock_rx_first_span_data[2] = 12U;

    TEST_EXEC_UART_Set_Rx_Spans( mock_rx_first_span_data, 3U, nullptr, 0U );

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 3U );
    EXPECT_EQ( dest[0], 10U );
    EXPECT_EQ( dest[1], 11U );
    EXPECT_EQ( dest[2], 12U );
    EXPECT_EQ( mock_hw_rx_consume_count, 1U );
    EXPECT_EQ( mock_hw_last_consume_count, 3U );
}

TEST_F( ExecUARTTest, ReadCopiesOnlyDestinationSizeFromFirstSpan )
{
    uint8_t  dest[2]    = {};
    uint32_t bytes_read = 0U;

    mock_rx_first_span_data[0] = 10U;
    mock_rx_first_span_data[1] = 11U;
    mock_rx_first_span_data[2] = 12U;

    TEST_EXEC_UART_Set_Rx_Spans( mock_rx_first_span_data, 3U, nullptr, 0U );

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 2U );
    EXPECT_EQ( dest[0], 10U );
    EXPECT_EQ( dest[1], 11U );
    EXPECT_EQ( mock_hw_rx_consume_count, 1U );
    EXPECT_EQ( mock_hw_last_consume_count, 2U );
}

TEST_F( ExecUARTTest, ReadCopiesWrappedSpansInOrder )
{
    uint8_t  dest[8]    = {};
    uint32_t bytes_read = 0U;

    mock_rx_first_span_data[0]  = 1U;
    mock_rx_first_span_data[1]  = 2U;
    mock_rx_second_span_data[0] = 3U;
    mock_rx_second_span_data[1] = 4U;

    TEST_EXEC_UART_Set_Rx_Spans( mock_rx_first_span_data, 2U, mock_rx_second_span_data, 2U );

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 4U );
    EXPECT_EQ( dest[0], 1U );
    EXPECT_EQ( dest[1], 2U );
    EXPECT_EQ( dest[2], 3U );
    EXPECT_EQ( dest[3], 4U );
    EXPECT_EQ( mock_hw_rx_consume_count, 1U );
    EXPECT_EQ( mock_hw_last_consume_count, 4U );
}

TEST_F( ExecUARTTest, ReadCopiesPartialWrappedSecondSpanWhenDestinationIsLimited )
{
    uint8_t  dest[3]    = {};
    uint32_t bytes_read = 0U;

    mock_rx_first_span_data[0]  = 1U;
    mock_rx_first_span_data[1]  = 2U;
    mock_rx_second_span_data[0] = 3U;
    mock_rx_second_span_data[1] = 4U;

    TEST_EXEC_UART_Set_Rx_Spans( mock_rx_first_span_data, 2U, mock_rx_second_span_data, 2U );

    ASSERT_TRUE( EXEC_UART_Read( HW_UART_CHANNEL_1, dest, sizeof( dest ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 3U );
    EXPECT_EQ( dest[0], 1U );
    EXPECT_EQ( dest[1], 2U );
    EXPECT_EQ( dest[2], 3U );
    EXPECT_EQ( mock_hw_rx_consume_count, 1U );
    EXPECT_EQ( mock_hw_last_consume_count, 3U );
}

TEST_F( ExecUARTTest, IsTxCompleteDelegatesToLowLevelDriver )
{
    mock_hw_tx_complete_result = true;

    EXPECT_TRUE( EXEC_UART_Is_Tx_Complete( HW_UART_CHANNEL_2 ) );

    EXPECT_EQ( mock_hw_tx_complete_count, 1U );
    EXPECT_EQ( mock_hw_last_channel, HW_UART_CHANNEL_2 );
}

TEST_F( ExecUARTTest, IsTxCompleteReturnsFalseWhenLowLevelDriverReportsIncomplete )
{
    mock_hw_tx_complete_result = false;

    EXPECT_FALSE( EXEC_UART_Is_Tx_Complete( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( mock_hw_tx_complete_count, 1U );
    EXPECT_EQ( mock_hw_last_channel, HW_UART_CHANNEL_1 );
}