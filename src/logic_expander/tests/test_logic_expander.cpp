/******************************************************************************
 *  Unit tests for transaction-queued MCP23017 logic-expander behavior.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <limits>

extern "C"
{
#include "logic_expander.h"
#include "../../hardware_low_level/hw_i2c/hw_i2c.h"
#include <stdbool.h>
#include <stdint.h>

#include "../logic_expander.c" /* Module under test */  // NOLINT
}

class MockHWI2C
{
public:
    MOCK_METHOD( HWI2CStatus_T, ConfigureInternal, ( uint16_t own_address_7bit ), () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterTransmit,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, const uint8_t* payload,
                   uint16_t payload_length ),
                 () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterReceive,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, uint16_t expected_length ),
                 () );
    MOCK_METHOD( void, ServiceTransactionQueue, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, IsTransactionQueueComplete, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, GetAndClearTransferResult, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, RecoverChannel, ( HWI2CChannel_T channel ), () );
};

static MockHWI2C* g_mock_hw_i2c = nullptr;
static uint8_t    g_fake_mutex_storage;
static uint32_t   g_mutex_create_calls = 0U;
static uint32_t   g_mutex_take_calls   = 0U;
static uint32_t   g_mutex_give_calls   = 0U;
static TickType_t g_current_tick       = 0U;

extern "C"
{
HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit )
{
    return g_mock_hw_i2c->ConfigureInternal( own_address_7bit );
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Transmit( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                              const uint8_t* payload, uint16_t payload_length )
{
    return g_mock_hw_i2c->EnqueueMasterTransmit( channel, device_address_7bit, payload,
                                                 payload_length );
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    return g_mock_hw_i2c->EnqueueMasterReceive( channel, device_address_7bit, expected_length );
}

void HW_I2C_Service_Transaction_Queue( HWI2CChannel_T channel )
{
    g_mock_hw_i2c->ServiceTransactionQueue( channel );
}

bool HW_I2C_Is_Transaction_Queue_Complete( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->IsTransactionQueueComplete( channel );
}

HWI2CStatus_T HW_I2C_Get_And_Clear_Transfer_Result( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->GetAndClearTransferResult( channel );
}

HWI2CStatus_T HW_I2C_Recover_Channel( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->RecoverChannel( channel );
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic( StaticSemaphore_t* mutex_buffer )
{
    ( void )mutex_buffer;
    g_mutex_create_calls++;
    return reinterpret_cast<SemaphoreHandle_t>( &g_fake_mutex_storage );
}

BaseType_t xSemaphoreTake( SemaphoreHandle_t semaphore, TickType_t ticks_to_wait )
{
    EXPECT_EQ( semaphore, logic_expander_mutex );
    EXPECT_EQ( ticks_to_wait, portMAX_DELAY );
    g_mutex_take_calls++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive( SemaphoreHandle_t semaphore )
{
    EXPECT_EQ( semaphore, logic_expander_mutex );
    g_mutex_give_calls++;
    return pdTRUE;
}

TickType_t xTaskGetTickCount( void )
{
    return g_current_tick;
}
}

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

class LogicExpanderTest : public ::testing::Test
{
protected:
    StrictMock<MockHWI2C> mock_hw_i2c;

    void SetUp( void ) override
    {
        g_mock_hw_i2c = &mock_hw_i2c;
        std::memset( logic_expander_state, 0, sizeof( logic_expander_state ) );
        std::memset( logic_expander_submitted_state, 0, sizeof( logic_expander_submitted_state ) );
        logic_expander_ready                  = false;
        logic_expander_active_bitmask         = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;
        logic_expander_dirty_bitmask          = 0U;
        logic_expander_pending_bitmask        = 0U;
        logic_expander_retry_bitmask          = 0U;
        logic_expander_config_state           = LOGIC_EXPANDER_CONFIG_NOT_STARTED;
        logic_expander_config_index           = 0U;
        logic_expander_config_write           = 0U;
        logic_expander_deadline_active        = false;
        logic_expander_transaction_start_tick = 0U;
        logic_expander_mutex = reinterpret_cast<SemaphoreHandle_t>( &g_fake_mutex_storage );
        g_mutex_create_calls = 0U;
        g_mutex_take_calls   = 0U;
        g_mutex_give_calls   = 0U;
        g_current_tick       = 0U;
    }

    void TearDown( void ) override
    {
        g_mock_hw_i2c = nullptr;
    }

    void ExpectAllConfigurationWrites( void )
    {
        logic_expander_active_bitmask = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;

        for ( uint16_t address = 0x20U; address <= 0x26U; ++address )
        {
            EXPECT_CALL( mock_hw_i2c,
                         EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, address, _, _ ) )
                .Times( 8 )
                .WillRepeatedly( Return( HW_I2C_STATUS_OK ) );
        }
    }
};

TEST_F( LogicExpanderTest, FunctionalIndexValuesMatchAddressTableIndices )
{
    EXPECT_EQ( LOGIC_EXPANDER_DI_1, 0 );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_AO, 6 );
    EXPECT_EQ( LOGIC_EXPANDER_COUNT, 7 );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_DI_1], 0x20U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_DI_2], 0x21U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_DO_1], 0x22U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_DO_2], 0x23U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_PWM_SPI], 0x24U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_UART_PWR], 0x25U );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_I2C_AO], 0x26U );
}

TEST_F( LogicExpanderTest, DefaultConfigurationMarksSevenExpandersActive )
{
    logic_expander_active_bitmask = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;

    EXPECT_EQ( LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK, 0x7FU );

    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        EXPECT_TRUE( LOGIC_EXPANDER_Index_Is_Active( ( LogicExpanderIndex_T )idx ) );
    }
}

TEST_F( LogicExpanderTest, InitCreatesMutexOnceBeforeTaskAccess )
{
    logic_expander_mutex = nullptr;

    EXPECT_TRUE( LOGIC_EXPANDER_Init() );
    EXPECT_TRUE( LOGIC_EXPANDER_Init() );
    EXPECT_EQ( g_mutex_create_calls, 1U );
}

TEST_F( LogicExpanderTest, PublicStateAccessTakesAndReleasesMutex )
{
    LogicExpanderStateSnapshot_T snapshot = {};

    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot( LOGIC_EXPANDER_DI_1, &snapshot ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( g_mutex_take_calls, 1U );
    EXPECT_EQ( g_mutex_give_calls, 1U );
}

TEST_F( LogicExpanderTest, SelfConfigWaitsForPhysicalCompletionBeforeReady )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) ).Times( 2 );
    ExpectAllConfigurationWrites();
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( false ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_FALSE( logic_expander_ready );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DI_1].olat_a, 0x00U );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DI_1].olat_b, 0x00U );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_TRUE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, SelfConfigAppliesSafeDefaultOutputState )
{
    const std::array<uint8_t, 3U> expected_default = { MCP23017_REG_OLATA, 0x00U, 0x00U };

    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );

    {
        InSequence sequence;

        for ( uint16_t address = 0x20U; address <= 0x26U; ++address )
        {
            EXPECT_CALL( mock_hw_i2c,
                         EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, address, _, _ ) )
                .Times( 7 )
                .WillRepeatedly( Return( HW_I2C_STATUS_OK ) );
            EXPECT_CALL( mock_hw_i2c,
                         EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, address, _, 3U ) )
                .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
                    EXPECT_EQ( std::memcmp( data, expected_default.data(), length ), 0 );
                    return HW_I2C_STATUS_OK;
                } );
        }
    }

    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( false ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DI_1].olat_a, 0x00U );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DI_1].olat_b, 0x00U );
}

TEST_F( LogicExpanderTest, ProcessDoesNotServiceI2CUntilConfigurationStarts )
{
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( _ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_NOT_READY );
}

TEST_F( LogicExpanderTest, SelfConfigPhysicalErrorNeverMarksReady )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    ExpectAllConfigurationWrites();
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, SelfConfigRecoversWhenCompletionDeadlineExpires )
{
    g_current_tick = 10U;

    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) ).Times( 2 );
    ExpectAllConfigurationWrites();
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .Times( 2 )
        .WillRepeatedly( Return( false ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    g_current_tick = 10U + pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS ) - 1U;
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_TRUE( logic_expander_deadline_active );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    g_current_tick = 10U + pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_EQ( logic_expander_config_state, LOGIC_EXPANDER_CONFIG_FAILED );
    EXPECT_FALSE( logic_expander_ready );
    EXPECT_FALSE( logic_expander_deadline_active );
}

TEST_F( LogicExpanderTest, SelfConfigDeadlineRefreshesWhenQueueSubmissionMakesProgress )
{
    g_current_tick = 10U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .WillOnce( Return( HW_I2C_STATUS_BUSY ) );

        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .WillOnce( Return( HW_I2C_STATUS_BUSY ) );

        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .WillOnce( Return( HW_I2C_STATUS_BUSY ) );
        EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_transaction_start_tick, 10U );

    /* The original batch deadline has now elapsed, but accepting another
     * configuration write proves that the queue is still making progress. */
    g_current_tick = 10U + pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_transaction_start_tick, g_current_tick );
    EXPECT_EQ( logic_expander_config_state, LOGIC_EXPANDER_CONFIG_QUEUING );

    /* A genuine 100 ms period with no further accepted transaction still
     * trips recovery. */
    g_current_tick += pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_EQ( logic_expander_config_state, LOGIC_EXPANDER_CONFIG_FAILED );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, SelfConfigIsIdempotentAfterReady )
{
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_ready         = true;
    logic_expander_dirty_bitmask = 0x05U;
    logic_expander_state[0]      = { 0x5AU, 0xA5U };

    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( _, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( _ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_TRUE( logic_expander_ready );
    EXPECT_EQ( logic_expander_config_state, LOGIC_EXPANDER_CONFIG_READY );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x05U );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x5AU );
    EXPECT_EQ( logic_expander_state[0].olat_b, 0xA5U );
}

TEST_F( LogicExpanderTest, SelfConfigRetriesFailedConfigurationThroughRecovery )
{
    logic_expander_config_state = LOGIC_EXPANDER_CONFIG_FAILED;
    logic_expander_ready        = false;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
        EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
        EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
        ExpectAllConfigurationWrites();
        EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( false ) );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_config_state, LOGIC_EXPANDER_CONFIG_WAITING_FOR_COMPLETION );
}

TEST_F( LogicExpanderTest, SelfConfigPropagatesConfigurationBusyWithoutQueueing )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_BUSY ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( _, _, _, _ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, InternalTransmitAndReceiveUseAtomicMasterQueueApis )
{
    const std::array<uint8_t, 2U> payload = { 0xA1U, 0xB2U };
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x33U, payload.data(),
                                                     payload.size() ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_TRUE( LOGIC_EXPANDER_Master_Transmit_Internal( 0x33U, payload.data(), payload.size() ) );

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterReceive( HW_I2C_CHANNEL_FMPI2C1, 0x33U, 12U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_TRUE( LOGIC_EXPANDER_Start_Master_Receive_Internal( 0x33U, 12U ) );
}

TEST_F( LogicExpanderTest, LoadControlBitMarksDirtyOnlyWhenShadowChanges )
{
    EXPECT_EQ(
        LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 8U, true ),
        LOGIC_EXPANDER_STATUS_INVALID_PARAM );

    EXPECT_EQ(
        LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 3U, false ),
        LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );

    EXPECT_EQ(
        LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 3U, true ),
        LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x08U );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );

    EXPECT_EQ(
        LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 3U, true ),
        LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, SendControlBitsReturnsNotReadyBeforePhysicalConfiguration )
{
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_NOT_READY );
}

TEST_F( LogicExpanderTest, SendControlBitsEnqueuesOnlyDirtyExpanders )
{
    logic_expander_ready                           = true;
    logic_expander_dirty_bitmask                   = 0x01U;
    logic_expander_state[0]                        = { 0x5AU, 0xA5U };
    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x5AU, 0xA5U };

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
            EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
    EXPECT_EQ( logic_expander_submitted_state[0].olat_a, 0x5AU );
    EXPECT_EQ( logic_expander_submitted_state[0].olat_b, 0xA5U );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
}

TEST_F( LogicExpanderTest, SendControlBitsCanAddressAllActiveExpanders )
{
    logic_expander_ready          = true;
    logic_expander_active_bitmask = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;
    logic_expander_dirty_bitmask  = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;

    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        logic_expander_state[idx]                      = { 0xFFU, 0xFFU };
        const uint16_t                address          = ( uint16_t )( 0x20U + idx );
        const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0xFFU, 0xFFU };

        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, address, _, 3U ) )
            .WillOnce( [expected_payload]( HWI2CChannel_T, uint16_t, const uint8_t* data,
                                           uint16_t length ) {
                EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
                return HW_I2C_STATUS_OK;
            } );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK );
}

TEST_F( LogicExpanderTest, PartialQueueFullRetryDoesNotDuplicateAcceptedExpander )
{
    logic_expander_ready          = true;
    logic_expander_active_bitmask = 0x03U;
    logic_expander_dirty_bitmask  = 0x03U;
    logic_expander_state[0]       = { 0x10U, 0x11U };
    logic_expander_state[1]       = { 0x20U, 0x21U };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, _, 3U ) )
            .WillOnce( Return( HW_I2C_STATUS_BUSY ) );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x02U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
    EXPECT_EQ( logic_expander_submitted_state[0].olat_a, 0x10U );
    EXPECT_EQ( logic_expander_submitted_state[1].olat_a, 0x00U );

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x03U );
    EXPECT_EQ( logic_expander_submitted_state[1].olat_a, 0x20U );
}

TEST_F( LogicExpanderTest, ReadyProcessClearsPendingWritesAfterPhysicalCompletion )
{
    logic_expander_ready           = true;
    logic_expander_config_state    = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_pending_bitmask = 0x01U;

    InSequence sequence;
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_pending_bitmask, 0U );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );
}

TEST_F( LogicExpanderTest, ReadyProcessAutomaticallyRetriesAsynchronousFailure )
{
    logic_expander_ready              = true;
    logic_expander_config_state       = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_active_bitmask     = 0x03U;
    logic_expander_dirty_bitmask      = 0U;
    logic_expander_pending_bitmask    = 0x01U;
    logic_expander_submitted_state[0] = { 0x31U, 0x32U };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
        EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
            .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_TRUE( logic_expander_ready );
    EXPECT_EQ( logic_expander_pending_bitmask, 0U );
    EXPECT_EQ( logic_expander_retry_bitmask, 0x01U );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x31U, 0x32U };
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
            EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
            return HW_I2C_STATUS_OK;
        } );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, ReadyProcessRecoversTimedOutWritesAndSchedulesFreshRetryDeadline )
{
    logic_expander_ready         = true;
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_dirty_bitmask = 0x01U;
    logic_expander_state[0]      = { 0x31U, 0x32U };
    g_current_tick               = 5U;

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    g_current_tick = 5U + pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_EQ( logic_expander_pending_bitmask, 0U );
    EXPECT_EQ( logic_expander_retry_bitmask, 0x01U );
    EXPECT_FALSE( logic_expander_deadline_active );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    g_current_tick = 200U;
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
    EXPECT_TRUE( logic_expander_deadline_active );
    EXPECT_EQ( logic_expander_transaction_start_tick, 200U );
}

TEST_F( LogicExpanderTest, LaterAcceptedWriteRefreshesPendingDeadline )
{
    logic_expander_ready         = true;
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_dirty_bitmask = 0x01U;
    logic_expander_state[0]      = { 0x41U, 0x42U };
    g_current_tick               = 10U;

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );

    logic_expander_state[0]      = { 0x51U, 0x52U };
    logic_expander_dirty_bitmask = 0x01U;
    g_current_tick               = 90U;
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_transaction_start_tick, 90U );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) ).Times( 2 );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .Times( 2 )
        .WillRepeatedly( Return( false ) );
    EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    g_current_tick = 110U;
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );

    g_current_tick = 90U + pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_EQ( logic_expander_retry_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, TransactionDeadlineHandlesTickCounterWraparound )
{
    logic_expander_deadline_active        = true;
    logic_expander_transaction_start_tick = std::numeric_limits<TickType_t>::max() - 49U;
    g_current_tick                        = 50U;

    EXPECT_TRUE( LOGIC_EXPANDER_Transaction_Deadline_Expired() );
}

TEST_F( LogicExpanderTest, ReadyProcessKeepsRetryScheduledWhenQueueIsBusy )
{
    logic_expander_ready              = true;
    logic_expander_config_state       = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_retry_bitmask      = 0x01U;
    logic_expander_submitted_state[0] = { 0x41U, 0x42U };

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_BUSY ) );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_retry_bitmask, 0x01U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0U );
}

TEST_F( LogicExpanderTest, ReadyProcessDoesNotSendNewDirtyStateWithoutExplicitSend )
{
    logic_expander_ready         = true;
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_dirty_bitmask = 0x01U;

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( _, _, _, _ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, RetryUsesSubmittedSnapshotWhileNewerUnsentShadowRemainsDirty )
{
    logic_expander_ready         = true;
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_state[0]      = { 0x51U, 0x52U };
    logic_expander_dirty_bitmask = 0x01U;

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );

    logic_expander_state[0]      = { 0x61U, 0x62U };
    logic_expander_dirty_bitmask = 0x01U;

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_EQ( logic_expander_retry_bitmask, 0x01U );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );

    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x51U, 0x52U };
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
            EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, NewerExplicitSnapshotSupersedesEarlierPendingSnapshot )
{
    logic_expander_ready         = true;
    logic_expander_config_state  = LOGIC_EXPANDER_CONFIG_READY;
    logic_expander_state[0]      = { 0x71U, 0x72U };
    logic_expander_dirty_bitmask = 0x01U;

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );

    logic_expander_state[0]      = { 0x81U, 0x82U };
    logic_expander_dirty_bitmask = 0x01U;
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_submitted_state[0].olat_a, 0x81U );
    EXPECT_EQ( logic_expander_submitted_state[0].olat_b, 0x82U );

    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_ERROR );

    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x81U, 0x82U };
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
            EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_retry_bitmask, 0U );
    EXPECT_EQ( logic_expander_pending_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, GetStateSnapshotUsesRoleIndexedAddressAndShadowTables )
{
    logic_expander_state[LOGIC_EXPANDER_DO_1] = { 0x11U, 0x22U };
    LogicExpanderStateSnapshot_T snapshot{};

    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot( LOGIC_EXPANDER_DO_1, &snapshot ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( snapshot.device_address_7bit, 0x22U );
    EXPECT_EQ( snapshot.olat_a, 0x11U );
    EXPECT_EQ( snapshot.olat_b, 0x22U );

    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot( LOGIC_EXPANDER_COUNT, nullptr ),
               LOGIC_EXPANDER_STATUS_INVALID_PARAM );
}
