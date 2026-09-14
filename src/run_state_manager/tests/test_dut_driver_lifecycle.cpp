/******************************************************************************
 * Unit tests for DUT Driver Lifecycle coordination and sequencing.
 * Production code is included directly following the repository convention.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>
#include <cstdint>

extern "C"
{
#include "dut_driver_lifecycle.h"
#include "logic_expander.h"
#include "test_configuration.h"
#include "exec_analogue_input.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_digital_input.h"
#include "exec_digital_output.h"
#include "exec_i2c.h"
#include "exec_pwm_capture.h"
#include "exec_pwm_gen.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "power.h"
}

/* -------------------------------------------------------------------------- */
/* Mock state and shims                                                       */
/* -------------------------------------------------------------------------- */

static LogicExpanderStatus_T             s_expander_begin_status;
static LogicExpanderStatus_T             s_expander_end_status;
static LogicExpanderControlBatchStatus_T s_expander_batch_status;
static uint32_t                          s_expander_begin_calls;
static uint32_t                          s_expander_end_calls;
static uint32_t                          s_expander_cancel_calls;
static LogicExpanderControlBatchId_T     s_expander_next_batch_id;
static LogicExpanderControlBatchId_T     s_last_cancelled_batch_id;

static bool     s_ai_configure_result;
static bool     s_ai_start_result;
static bool     s_ai_stop_result;
static uint32_t s_ai_start_calls;
static uint32_t s_ai_stop_calls;

static bool                  s_ao_configure_result;
static AnalogueOutputState_T s_ao_state;
static bool                  s_ao_start_result;
static bool                  s_ao_stop_result;
static bool                  s_ao_abort_result;
static bool                  s_ao_tx_complete;
static uint32_t              s_ao_start_calls;
static uint32_t              s_ao_stop_calls;
static uint32_t              s_ao_abort_calls;

static bool     s_di_configure_result;
static bool     s_di_start_result;
static bool     s_di_stop_result;
static uint32_t s_di_start_calls;
static uint32_t s_di_stop_calls;

static bool     s_do_configure_result;
static bool     s_do_start_result;
static bool     s_do_stop_result;
static uint32_t s_do_start_calls;
static uint32_t s_do_stop_calls;

static EXEC_CAN_Result_T    s_can_configure_result;
static EXEC_CAN_Result_T    s_can_start_result;
static EXEC_CAN_Result_T    s_can_stop_result;
static EXEC_CAN_Result_T    s_can_abort_result;
static EXEC_CAN_Tx_Status_T s_can_tx_status;
static uint32_t             s_can_start_calls[EXEC_CAN_CHANNEL_COUNT];
static uint32_t             s_can_stop_calls[EXEC_CAN_CHANNEL_COUNT];
static uint32_t             s_can_abort_calls[EXEC_CAN_CHANNEL_COUNT];

static EXECI2CStatus_T s_i2c_configure_result;

static bool     s_pwm_cap_configure_result;
static bool     s_pwm_cap_start_result;
static bool     s_pwm_cap_stop_result;
static uint32_t s_pwm_cap_start_calls[TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT];
static uint32_t s_pwm_cap_stop_calls[TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT];

static bool     s_pwm_gen_configure_result;
static bool     s_pwm_gen_start_result;
static bool     s_pwm_gen_stop_result;
static uint32_t s_pwm_gen_start_calls[EXEC_PWM_GEN_CHANNEL_COUNT];
static uint32_t s_pwm_gen_stop_calls[EXEC_PWM_GEN_CHANNEL_COUNT];

static bool     s_spi_configure_result;
static bool     s_spi_start_result;
static bool     s_spi_stop_result;
static bool     s_spi_abort_result;
static bool     s_spi_tx_complete;
static bool     s_spi_tx_faulted;
static uint32_t s_spi_start_calls[TEST_CONFIGURATION_SPI_CHANNEL_COUNT];
static uint32_t s_spi_stop_calls[TEST_CONFIGURATION_SPI_CHANNEL_COUNT];
static uint32_t s_spi_abort_calls[TEST_CONFIGURATION_SPI_CHANNEL_COUNT];

static bool     s_uart_configure_result;
static bool     s_uart_start_result;
static bool     s_uart_stop_result;
static bool     s_uart_abort_result;
static bool     s_uart_tx_complete;
static uint32_t s_uart_start_calls[EXEC_UART_CHANNEL_COUNT];
static uint32_t s_uart_stop_calls[EXEC_UART_CHANNEL_COUNT];
static uint32_t s_uart_abort_calls[EXEC_UART_CHANNEL_COUNT];

extern "C"
{

LogicExpanderStatus_T LOGIC_EXPANDER_Begin_Control_Batch( LogicExpanderControlBatchId_T* batch_id )
{
    s_expander_begin_calls++;
    if ( s_expander_begin_status == LOGIC_EXPANDER_STATUS_OK && batch_id != nullptr )
    {
        *batch_id = s_expander_next_batch_id++;
    }
    return s_expander_begin_status;
}

LogicExpanderStatus_T LOGIC_EXPANDER_End_Control_Batch( LogicExpanderControlBatchId_T batch_id )
{
    ( void )batch_id;
    s_expander_end_calls++;
    return s_expander_end_status;
}

void LOGIC_EXPANDER_Cancel_Control_Batch( LogicExpanderControlBatchId_T batch_id )
{
    s_expander_cancel_calls++;
    s_last_cancelled_batch_id = batch_id;
}

LogicExpanderControlBatchStatus_T
LOGIC_EXPANDER_Get_Control_Batch_Status( LogicExpanderControlBatchId_T batch_id )
{
    ( void )batch_id;
    return s_expander_batch_status;
}

bool EXEC_ANALOGUE_INPUT_Configure( const ExecAnalogueInputConfig_T* configuration )
{
    ( void )configuration;
    return s_ai_configure_result;
}
bool EXEC_ANALOGUE_INPUT_Start( void )
{
    s_ai_start_calls++;
    return s_ai_start_result;
}
bool EXEC_ANALOGUE_INPUT_Stop( void )
{
    s_ai_stop_calls++;
    return s_ai_stop_result;
}

bool EXEC_ANALOGUE_OUTPUT_Configure( const ExecAnalogueOutputConfig_T* configuration )
{
    ( void )configuration;
    return s_ao_configure_result;
}
AnalogueOutputState_T EXEC_ANALOGUE_OUTPUT_Get_State( void )
{
    return s_ao_state;
}
bool EXEC_ANALOGUE_OUTPUT_Start( void )
{
    s_ao_start_calls++;
    return s_ao_start_result;
}
bool EXEC_ANALOGUE_OUTPUT_Stop( void )
{
    s_ao_stop_calls++;
    return s_ao_stop_result;
}
bool EXEC_ANALOGUE_OUTPUT_Abort( void )
{
    s_ao_abort_calls++;
    return s_ao_abort_result;
}
bool EXEC_ANALOGUE_OUTPUT_Is_Transmission_Complete( void )
{
    return s_ao_tx_complete;
}

bool EXEC_DIGITAL_INPUT_Configure( const ExecDigitalInputConfig_T* config )
{
    ( void )config;
    return s_di_configure_result;
}
bool EXEC_DIGITAL_INPUT_Start( void )
{
    s_di_start_calls++;
    return s_di_start_result;
}
bool EXEC_DIGITAL_INPUT_Stop( void )
{
    s_di_stop_calls++;
    return s_di_stop_result;
}

bool EXEC_DIGITAL_OUTPUT_Configure( const ExecDigitalOutputConfig_T* config )
{
    ( void )config;
    return s_do_configure_result;
}
bool EXEC_DIGITAL_OUTPUT_Start( void )
{
    s_do_start_calls++;
    return s_do_start_result;
}
bool EXEC_DIGITAL_OUTPUT_Stop( void )
{
    s_do_stop_calls++;
    return s_do_stop_result;
}

EXEC_CAN_Result_T EXEC_CAN_Configure_Channel( EXEC_CAN_Channel_T       channel,
                                              const EXEC_CAN_Config_T* config )
{
    ( void )channel;
    ( void )config;
    return s_can_configure_result;
}
EXEC_CAN_Result_T EXEC_CAN_Start_Channel( EXEC_CAN_Channel_T channel )
{
    if ( channel < EXEC_CAN_CHANNEL_COUNT )
    {
        s_can_start_calls[channel]++;
    }
    return s_can_start_result;
}
EXEC_CAN_Result_T EXEC_CAN_Stop_Channel( EXEC_CAN_Channel_T channel )
{
    if ( channel < EXEC_CAN_CHANNEL_COUNT )
    {
        s_can_stop_calls[channel]++;
    }
    return s_can_stop_result;
}
EXEC_CAN_Result_T EXEC_CAN_Abort_Channel( EXEC_CAN_Channel_T channel )
{
    if ( channel < EXEC_CAN_CHANNEL_COUNT )
    {
        s_can_abort_calls[channel]++;
    }
    return s_can_abort_result;
}
EXEC_CAN_Tx_Status_T EXEC_CAN_Get_Tx_Status( EXEC_CAN_Channel_T channel )
{
    ( void )channel;
    return s_can_tx_status;
}

EXECI2CStatus_T EXEC_I2C_Configure_Channel( ExecI2CChannel_T              channel,
                                            const EXECI2CChannelConfig_T* config )
{
    ( void )channel;
    ( void )config;
    return s_i2c_configure_result;
}

bool EXEC_PWM_Capture_Configure_Channel( ExecPwmCaptureChannel_T       channel,
                                         const ExecPwmCaptureConfig_T* config )
{
    ( void )channel;
    ( void )config;
    return s_pwm_cap_configure_result;
}
bool EXEC_PWM_Capture_Start_Channel( ExecPwmCaptureChannel_T channel )
{
    if ( channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT )
    {
        s_pwm_cap_start_calls[channel]++;
    }
    return s_pwm_cap_start_result;
}
bool EXEC_PWM_Capture_Stop_Channel( ExecPwmCaptureChannel_T channel )
{
    if ( channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT )
    {
        s_pwm_cap_stop_calls[channel]++;
    }
    return s_pwm_cap_stop_result;
}

bool EXEC_PWM_GEN_Configure_Channel( ExecPwmGenChannel_T channel, const ExecPwmGenConfig_T* config )
{
    ( void )channel;
    ( void )config;
    return s_pwm_gen_configure_result;
}
bool EXEC_PWM_GEN_Start_Channel( ExecPwmGenChannel_T channel )
{
    if ( channel < EXEC_PWM_GEN_CHANNEL_COUNT )
    {
        s_pwm_gen_start_calls[channel]++;
    }
    return s_pwm_gen_start_result;
}
bool EXEC_PWM_GEN_Stop_Channel( ExecPwmGenChannel_T channel )
{
    if ( channel < EXEC_PWM_GEN_CHANNEL_COUNT )
    {
        s_pwm_gen_stop_calls[channel]++;
    }
    return s_pwm_gen_stop_result;
}

bool EXEC_SPI_Configure_Channel( ExecSPIChannel_T channel, const ExecSPIConfig_T* config )
{
    ( void )channel;
    ( void )config;
    return s_spi_configure_result;
}
bool EXEC_SPI_Start_Channel( ExecSPIChannel_T channel )
{
    if ( channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT )
    {
        s_spi_start_calls[channel]++;
    }
    return s_spi_start_result;
}
bool EXEC_SPI_Stop_Channel( ExecSPIChannel_T channel )
{
    if ( channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT )
    {
        s_spi_stop_calls[channel]++;
    }
    return s_spi_stop_result;
}
bool EXEC_SPI_Abort_Channel( ExecSPIChannel_T channel )
{
    if ( channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT )
    {
        s_spi_abort_calls[channel]++;
    }
    return s_spi_abort_result;
}
bool EXEC_SPI_Is_Transmission_Complete( ExecSPIChannel_T channel )
{
    ( void )channel;
    return s_spi_tx_complete;
}
bool EXEC_SPI_Is_Transmission_Faulted( ExecSPIChannel_T channel )
{
    ( void )channel;
    return s_spi_tx_faulted;
}

bool EXEC_UART_Configure_Channel( ExecUartChannel_T channel, const ExecUartConfig_T* config )
{
    ( void )channel;
    ( void )config;
    return s_uart_configure_result;
}
bool EXEC_UART_Start_Channel( ExecUartChannel_T channel )
{
    if ( channel < EXEC_UART_CHANNEL_COUNT )
    {
        s_uart_start_calls[channel]++;
    }
    return s_uart_start_result;
}
bool EXEC_UART_Stop_Channel( ExecUartChannel_T channel )
{
    if ( channel < EXEC_UART_CHANNEL_COUNT )
    {
        s_uart_stop_calls[channel]++;
    }
    return s_uart_stop_result;
}
bool EXEC_UART_Abort_Channel( ExecUartChannel_T channel )
{
    if ( channel < EXEC_UART_CHANNEL_COUNT )
    {
        s_uart_abort_calls[channel]++;
    }
    return s_uart_abort_result;
}
bool EXEC_UART_Is_Tx_Complete( ExecUartChannel_T channel )
{
    ( void )channel;
    return s_uart_tx_complete;
}

#if defined( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../dut_driver_lifecycle.c" /* Private module under test */  // NOLINT
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif
}

/* -------------------------------------------------------------------------- */
/* Test Fixture                                                               */
/* -------------------------------------------------------------------------- */

class DutDriverLifecycleTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        s_expander_begin_status   = LOGIC_EXPANDER_STATUS_OK;
        s_expander_end_status     = LOGIC_EXPANDER_STATUS_OK;
        s_expander_batch_status   = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
        s_expander_begin_calls    = 0U;
        s_expander_end_calls      = 0U;
        s_expander_cancel_calls   = 0U;
        s_expander_next_batch_id  = 1U;
        s_last_cancelled_batch_id = 0U;

        s_ai_configure_result = true;
        s_ai_start_result     = true;
        s_ai_stop_result      = true;
        s_ai_start_calls      = 0U;
        s_ai_stop_calls       = 0U;

        s_ao_configure_result = true;
        s_ao_state            = EXEC_ANALOGUE_OUTPUT_STATE_CONFIGURED;
        s_ao_start_result     = true;
        s_ao_stop_result      = true;
        s_ao_abort_result     = true;
        s_ao_tx_complete      = true;
        s_ao_start_calls      = 0U;
        s_ao_stop_calls       = 0U;
        s_ao_abort_calls      = 0U;

        s_di_configure_result = true;
        s_di_start_result     = true;
        s_di_stop_result      = true;
        s_di_start_calls      = 0U;
        s_di_stop_calls       = 0U;

        s_do_configure_result = true;
        s_do_start_result     = true;
        s_do_stop_result      = true;
        s_do_start_calls      = 0U;
        s_do_stop_calls       = 0U;

        s_can_configure_result = EXEC_CAN_RESULT_OK;
        s_can_start_result     = EXEC_CAN_RESULT_OK;
        s_can_stop_result      = EXEC_CAN_RESULT_OK;
        s_can_abort_result     = EXEC_CAN_RESULT_OK;
        s_can_tx_status        = EXEC_CAN_TX_STATUS_COMPLETE;
        std::memset( s_can_start_calls, 0, sizeof( s_can_start_calls ) );
        std::memset( s_can_stop_calls, 0, sizeof( s_can_stop_calls ) );
        std::memset( s_can_abort_calls, 0, sizeof( s_can_abort_calls ) );

        s_i2c_configure_result = EXEC_I2C_STATUS_OK;

        s_pwm_cap_configure_result = true;
        s_pwm_cap_start_result     = true;
        s_pwm_cap_stop_result      = true;
        std::memset( s_pwm_cap_start_calls, 0, sizeof( s_pwm_cap_start_calls ) );
        std::memset( s_pwm_cap_stop_calls, 0, sizeof( s_pwm_cap_stop_calls ) );

        s_pwm_gen_configure_result = true;
        s_pwm_gen_start_result     = true;
        s_pwm_gen_stop_result      = true;
        std::memset( s_pwm_gen_start_calls, 0, sizeof( s_pwm_gen_start_calls ) );
        std::memset( s_pwm_gen_stop_calls, 0, sizeof( s_pwm_gen_stop_calls ) );

        s_spi_configure_result = true;
        s_spi_start_result     = true;
        s_spi_stop_result      = true;
        s_spi_abort_result     = true;
        s_spi_tx_complete      = true;
        s_spi_tx_faulted       = false;
        std::memset( s_spi_start_calls, 0, sizeof( s_spi_start_calls ) );
        std::memset( s_spi_stop_calls, 0, sizeof( s_spi_stop_calls ) );
        std::memset( s_spi_abort_calls, 0, sizeof( s_spi_abort_calls ) );

        s_uart_configure_result = true;
        s_uart_start_result     = true;
        s_uart_stop_result      = true;
        s_uart_abort_result     = true;
        s_uart_tx_complete      = true;
        std::memset( s_uart_start_calls, 0, sizeof( s_uart_start_calls ) );
        std::memset( s_uart_stop_calls, 0, sizeof( s_uart_stop_calls ) );
        std::memset( s_uart_abort_calls, 0, sizeof( s_uart_abort_calls ) );

        std::memset( &lifecycle_context, 0, sizeof( lifecycle_context ) );
    }

    static DutDriverConfiguration_T CreateSampleConfiguration( void )
    {
        DutDriverConfiguration_T config               = {};
        config.analogue_input.is_enabled              = true;
        config.analogue_output.is_enabled             = true;
        config.digital_inputs.channels[0]             = EXEC_DIGITAL_INPUT_MODE_3V3;
        config.digital_outputs.channels[0].is_enabled = true;
        config.can_channels[0].is_enabled             = true;
        config.can_channels[1].is_enabled             = true;
        config.pwm_capture_channels[0].is_enabled     = true;
        config.pwm_generation_channels[1].is_enabled  = true;
        config.spi_channels[0].is_enabled             = true;
        config.uart_channels[1].is_enabled            = true;
        return config;
    }
};

/* -------------------------------------------------------------------------- */
/* Configuration & Enable Plan Tests                                          */
/* -------------------------------------------------------------------------- */

TEST_F( DutDriverLifecycleTest, ConfigureRejectsNull )
{
    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Configure( nullptr ) );
}

TEST_F( DutDriverLifecycleTest, ConfigureBatchBeginFailureReturnsFalse )
{
    s_expander_begin_status         = LOGIC_EXPANDER_STATUS_ERROR;
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    EXPECT_EQ( 1U, s_expander_begin_calls );
    EXPECT_EQ( 0U, s_expander_end_calls );
}

TEST_F( DutDriverLifecycleTest, ConfigureAppliesAllDriversAndBuildsEnablePlan )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    EXPECT_EQ( 1U, s_expander_begin_calls );
    EXPECT_EQ( 1U, s_expander_end_calls );

    DutDriverLifecycleStatus_T status = {};
    DUT_DRIVER_LIFECYCLE_GetStatus( &status );
    EXPECT_TRUE( status.configuration_valid );
    EXPECT_TRUE( status.analogue_input_enabled );
    EXPECT_TRUE( status.analogue_output_enabled );
    EXPECT_TRUE( status.digital_inputs_enabled );
    EXPECT_TRUE( status.digital_outputs_enabled );
    EXPECT_EQ( ( 1U << 0 ) | ( 1U << 1 ), status.can_enabled_mask );
    EXPECT_EQ( ( 1U << 0 ), status.pwm_capture_enabled_mask );
    EXPECT_EQ( ( 1U << 1 ), status.pwm_generation_enabled_mask );
    EXPECT_EQ( ( 1U << 0 ), status.spi_enabled_mask );
    EXPECT_EQ( ( 1U << 1 ), status.uart_enabled_mask );

    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_READY, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );
}

TEST_F( DutDriverLifecycleTest, ExpanderBackpressureDuringConfigureHandledAsPending )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    s_spi_configure_result          = false; /* Simulate expander queue full during configure */

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    EXPECT_TRUE( lifecycle_context.configuration_pending );
    EXPECT_EQ( 1U, s_expander_begin_calls );
    EXPECT_EQ( 0U, s_expander_end_calls ); /* Batch not sealed yet due to backpressure */

    /* GetConfigurationStatus retries configuration */
    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_PENDING, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );

    /* Once queue clears, configuration completes */
    s_spi_configure_result = true;
    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_READY, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );
    EXPECT_FALSE( lifecycle_context.configuration_pending );
    EXPECT_EQ( 1U, s_expander_end_calls );
}

TEST_F( DutDriverLifecycleTest, AnalogueOutputConfiguringStateReportsPending )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    s_ao_state                      = EXEC_ANALOGUE_OUTPUT_STATE_CONFIGURING;

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_PENDING, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );

    s_ao_state = EXEC_ANALOGUE_OUTPUT_STATE_CONFIGURED;
    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_READY, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );
}

TEST_F( DutDriverLifecycleTest, AnalogueOutputFaultCancelsOpenBatchAndFails )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );

    s_ao_state = EXEC_ANALOGUE_OUTPUT_STATE_FAULTED;
    EXPECT_EQ( DUT_DRIVER_CONFIGURATION_FAILED, DUT_DRIVER_LIFECYCLE_GetConfigurationStatus() );
    EXPECT_FALSE( lifecycle_context.configuration_valid );
}

TEST_F( DutDriverLifecycleTest, ExpanderEndBatchFailureCancelsBatchAndFails )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    s_expander_end_status           = LOGIC_EXPANDER_STATUS_ERROR;

    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    EXPECT_EQ( 1U, s_expander_cancel_calls );
}

/* -------------------------------------------------------------------------- */
/* Start & Startup Rollback Tests                                             */
/* -------------------------------------------------------------------------- */

TEST_F( DutDriverLifecycleTest, StartInvokesOnlyEnabledDriversAndSealsBatch )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );
    EXPECT_EQ( 1U, s_ai_start_calls );
    EXPECT_EQ( 1U, s_ao_start_calls );
    EXPECT_EQ( 1U, s_di_start_calls );
    EXPECT_EQ( 1U, s_can_start_calls[0] );
    EXPECT_EQ( 1U, s_can_start_calls[1] );
    EXPECT_EQ( 1U, s_pwm_cap_start_calls[0] );
    EXPECT_EQ( 0U, s_pwm_gen_start_calls[0] );
    EXPECT_EQ( 1U, s_pwm_gen_start_calls[1] );
    EXPECT_EQ( 1U, s_spi_start_calls[0] );
    EXPECT_EQ( 0U, s_uart_start_calls[0] );
    EXPECT_EQ( 1U, s_uart_start_calls[1] );

    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_PENDING;
    EXPECT_EQ( DUT_DRIVER_START_PENDING, DUT_DRIVER_LIFECYCLE_GetStartStatus() );

    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
    EXPECT_EQ( DUT_DRIVER_START_READY, DUT_DRIVER_LIFECYCLE_GetStartStatus() );
}

TEST_F( DutDriverLifecycleTest, StartWithoutValidConfigFails )
{
    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Start() );
    EXPECT_EQ( DUT_DRIVER_START_FAILED, DUT_DRIVER_LIFECYCLE_GetStartStatus() );
}

TEST_F( DutDriverLifecycleTest, StartFailsIfAlreadyStarted )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );

    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Start() );
}

TEST_F( DutDriverLifecycleTest, StartFailureRollsBackStartedDriversAndCancelsBatch )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );

    /* Fail halfway through startup on SPI */
    s_spi_start_result = false;

    EXPECT_FALSE( DUT_DRIVER_LIFECYCLE_Start() );

    /* Drivers started before SPI (AI, DI, PWM Capture, CAN) must have been stopped */
    EXPECT_EQ( 1U, s_ai_stop_calls );
    EXPECT_EQ( 1U, s_di_stop_calls );
    EXPECT_EQ( 1U, s_can_stop_calls[0] );
    EXPECT_EQ( 1U, s_can_stop_calls[1] );
    EXPECT_EQ( 1U, s_pwm_cap_stop_calls[0] );
    EXPECT_EQ( 1U, s_expander_cancel_calls );
}

/* -------------------------------------------------------------------------- */
/* Shutdown & Backpressure Tests                                              */
/* -------------------------------------------------------------------------- */

TEST_F( DutDriverLifecycleTest, GracefulShutdownWaitsForTransmissionComplete )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( false, false ) );

    /* Transmissions busy */
    s_ao_tx_complete = false;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_PENDING, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_EQ( 0U, s_ao_stop_calls );

    /* Transmissions finish */
    s_ao_tx_complete        = true;
    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_COMPLETE, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_EQ( 1U, s_ao_stop_calls );
    EXPECT_FALSE( lifecycle_context.shutdown_active );
}

TEST_F( DutDriverLifecycleTest, SafeStateBackpressureDuringShutdownReportsPending )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( false, true ) );

    /* Simulate backpressure when applying disabled configuration */
    s_can_configure_result = EXEC_CAN_RESULT_ERROR;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_PENDING, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_FALSE( lifecycle_context.shutdown_disabled_applied );

    /* Backpressure clears */
    s_can_configure_result  = EXEC_CAN_RESULT_OK;
    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_PENDING;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_PENDING, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_TRUE( lifecycle_context.shutdown_disabled_applied );

    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_COMPLETE, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_FALSE( lifecycle_context.configuration_valid );
}

TEST_F( DutDriverLifecycleTest, ExpanderEndBatchBusyDuringShutdownReportsPending )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );

    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( false, false ) );

    /* End_Control_Batch returns BUSY */
    s_expander_end_status = LOGIC_EXPANDER_STATUS_BUSY;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_PENDING, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_FALSE( lifecycle_context.shutdown_batch_sealed );

    /* End_Control_Batch succeeds */
    s_expander_end_status   = LOGIC_EXPANDER_STATUS_OK;
    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_COMPLETE, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_TRUE( lifecycle_context.shutdown_batch_sealed );
}

/* -------------------------------------------------------------------------- */
/* Forced Abort & Fault Handling Tests                                        */
/* -------------------------------------------------------------------------- */

TEST_F( DutDriverLifecycleTest, ForcedAbortEscalationInvokesAbortApis )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );

    /* Analogue output faulted at runtime, communications transmitting */
    s_ao_state         = EXEC_ANALOGUE_OUTPUT_STATE_FAULTED;
    s_can_tx_status    = EXEC_CAN_TX_STATUS_ACTIVE;
    s_spi_tx_complete  = false;
    s_uart_tx_complete = false;

    /* Graceful shutdown fails on faulted AO and busy communications */
    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( false, false ) );
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_FAILED, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );

    /* Escalate to forced abort */
    EXPECT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( true, true ) );
    s_expander_batch_status = LOGIC_EXPANDER_CONTROL_BATCH_COMPLETE;
    EXPECT_EQ( DUT_DRIVER_SHUTDOWN_COMPLETE, DUT_DRIVER_LIFECYCLE_GetShutdownStatus() );
    EXPECT_EQ( 1U, s_ao_abort_calls );
    EXPECT_EQ( 1U, s_can_abort_calls[0] );
    EXPECT_EQ( 1U, s_can_abort_calls[1] );
    EXPECT_EQ( 1U, s_spi_abort_calls[0] );
    EXPECT_EQ( 1U, s_uart_abort_calls[1] );
}

TEST_F( DutDriverLifecycleTest, EnterFaultCancelsAllBatchesAndStopsDrivers )
{
    DutDriverConfiguration_T config = CreateSampleConfiguration();
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Configure( &config ) );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_Start() );
    ASSERT_TRUE( DUT_DRIVER_LIFECYCLE_BeginShutdown( true, true ) );

    lifecycle_context.configuration_batch_id = 10U;
    lifecycle_context.start_batch_id         = 11U;
    lifecycle_context.shutdown_batch_id      = 12U;

    DUT_DRIVER_LIFECYCLE_EnterFault();

    EXPECT_GE( s_expander_cancel_calls, 3U );
    EXPECT_EQ( 0U, lifecycle_context.configuration_batch_id );
    EXPECT_EQ( 0U, lifecycle_context.start_batch_id );
    EXPECT_EQ( 0U, lifecycle_context.shutdown_batch_id );
    EXPECT_FALSE( lifecycle_context.configuration_valid );

    /* Idempotent subsequent call */
    DUT_DRIVER_LIFECYCLE_EnterFault();
}

TEST_F( DutDriverLifecycleTest, EnterIdleCancelsBatchesAndResetsContext )
{
    lifecycle_context.configuration_batch_id = 20U;
    lifecycle_context.start_batch_id         = 21U;

    DUT_DRIVER_LIFECYCLE_EnterIdle();

    EXPECT_GE( s_expander_cancel_calls, 2U );
    EXPECT_EQ( 0U, lifecycle_context.configuration_batch_id );
    EXPECT_EQ( 0U, lifecycle_context.start_batch_id );
    EXPECT_FALSE( lifecycle_context.configuration_valid );
}
