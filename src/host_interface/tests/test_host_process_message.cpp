/******************************************************************************
 *  File:       test_host_process_message.cpp
 *  Author:     OpenAI
 *
 *  Description:
 *      Unit and white-box tests for host_process_message.c using GoogleTest
 *      and GoogleMock.
 *
 *      These tests verify:
 *        - Error message construction and argument validation.
 *        - Run-state request mapping and transition status translation.
 *        - System information request/response handling.
 *        - Test configuration and instruction dispatch.
 *        - Execution/global control handling.
 *        - Unexpected inbound result/response handling.
 *        - Error handling and result transfer notification behavior.
 *        - Incoming/internal message dispatch.
 *        - Top-level response ownership and overflow behavior.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

extern "C"
{
#include "config_message_handler.h"
#include "host_process_message.h"
#include "host_process_message_test_access.h"
#include "hil_rig_protocol/version.h"
#include "instruction_message_handler.h"
#include "result_message_producer.h"
#include "run_state_manager.h"
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

template <typename Function_T> struct FunctionTraits;

template <typename Return_T, typename Argument_T>
struct FunctionTraits<Return_T ( * )( Argument_T )>
{
    using return_type   = Return_T;
    using argument_type = Argument_T;
};

using RequestExecutionTraits_T   = FunctionTraits<decltype( &RUN_STATE_MANAGER_RequestExecution )>;
using RequestExecutionResult_T   = typename RequestExecutionTraits_T::return_type;
using RequestExecutionArgument_T = typename RequestExecutionTraits_T::argument_type;

class MockHostProcessMessageDependencies
{
public:
    virtual ~MockHostProcessMessageDependencies() = default;

    MOCK_METHOD( HOST_Interface_Status_T, HOST_INTERFACE_Config_Message_To_Driver,
                 ( const HIL_Application_Message_T*, DutDriverConfiguration_T* ));
    MOCK_METHOD( HOST_Interface_Status_T, HOST_INTERFACE_Commit_Config_Message,
                 ( const DutDriverConfiguration_T* ));

    MOCK_METHOD( void, HOST_INSTRUCTION_HANDLER_Reset, () );
    MOCK_METHOD( HOST_Interface_Status_T, HOST_INSTRUCTION_HANDLER_HandleInstruction,
                 ( const HIL_Application_Test_Instruction_T* ));

    MOCK_METHOD( void, RESULT_MESSAGE_PRODUCER_Reset, () );
    MOCK_METHOD( Result_Message_Producer_Status_T, RESULT_MESSAGE_PRODUCER_ProduceNextMessage,
                 ( HIL_Application_Message_T* ));

    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestPackageReceive, () );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestConfiguration, () );
    MOCK_METHOD( RequestExecutionResult_T, RUN_STATE_MANAGER_RequestExecution,
                 ( RequestExecutionArgument_T ) );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestResultTransfer, () );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestFault, ( RunStateFaultReason_T ) );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR, () );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestReset, () );
    MOCK_METHOD( bool, RUN_STATE_MANAGER_RequestResultTransferComplete, () );
    MOCK_METHOD( void, RUN_STATE_MANAGER_GetStatus, ( RunStateManagerStatus_T* ));
};

static MockHostProcessMessageDependencies* g_mock_deps = nullptr;

extern "C" HOST_Interface_Status_T
HOST_INTERFACE_Config_Message_To_Driver( const HIL_Application_Message_T* message,
                                         DutDriverConfiguration_T*        config )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HOST_INTERFACE_Config_Message_To_Driver( message, config );
    }
    return HOST_INTERFACE_STATUS_OK;
}

extern "C" HOST_Interface_Status_T
HOST_INTERFACE_Commit_Config_Message( const DutDriverConfiguration_T* config )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HOST_INTERFACE_Commit_Config_Message( config );
    }
    return HOST_INTERFACE_STATUS_OK;
}

extern "C" void HOST_INSTRUCTION_HANDLER_Reset( void )
{
    if ( g_mock_deps != nullptr )
    {
        g_mock_deps->HOST_INSTRUCTION_HANDLER_Reset();
    }
}

extern "C" HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_HandleInstruction( const HIL_Application_Test_Instruction_T* instruction )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HOST_INSTRUCTION_HANDLER_HandleInstruction( instruction );
    }
    return HOST_INTERFACE_STATUS_OK;
}

extern "C" void RESULT_MESSAGE_PRODUCER_Reset( void )
{
    if ( g_mock_deps != nullptr )
    {
        g_mock_deps->RESULT_MESSAGE_PRODUCER_Reset();
    }
}

extern "C" Result_Message_Producer_Status_T
RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* message )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->RESULT_MESSAGE_PRODUCER_ProduceNextMessage( message );
    }
    return RESULT_MESSAGE_PRODUCER_STATUS_OK;
}

extern "C" bool RUN_STATE_MANAGER_RequestPackageReceive( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestPackageReceive() : true;
}

extern "C" bool RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( uint32_t expected_tick_count )
{
    ( void )expected_tick_count;
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestPackageReceive() : true;
}

extern "C" bool RUN_STATE_MANAGER_RequestConfiguration( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestConfiguration() : true;
}

extern "C" RequestExecutionResult_T
RUN_STATE_MANAGER_RequestExecution( RequestExecutionArgument_T request )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->RUN_STATE_MANAGER_RequestExecution( request );
    }
    return RUN_STATE_EXECUTION_REQUEST_ACCEPTED;
}

extern "C" bool RUN_STATE_MANAGER_RequestResultTransfer( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestResultTransfer() : true;
}

extern "C" bool RUN_STATE_MANAGER_RequestFault( RunStateFaultReason_T reason )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->RUN_STATE_MANAGER_RequestFault( reason );
    }
    return true;
}

extern "C" bool RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR()
                                  : true;
}

extern "C" bool RUN_STATE_MANAGER_RequestReset( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestReset() : true;
}

extern "C" bool RUN_STATE_MANAGER_RequestResultTransferComplete( void )
{
    return g_mock_deps != nullptr ? g_mock_deps->RUN_STATE_MANAGER_RequestResultTransferComplete()
                                  : true;
}

extern "C" void RUN_STATE_MANAGER_GetStatus( RunStateManagerStatus_T* status )
{
    if ( g_mock_deps != nullptr )
    {
        g_mock_deps->RUN_STATE_MANAGER_GetStatus( status );
    }
}

class HostProcessMessageTest : public ::testing::Test
{
protected:
    HIL_Application_Message_T incoming{};
    HIL_Application_Message_T outgoing{};
    HIL_Application_Message_T overflow_outgoing{};
    uint8_t                   data[64]{};
    bool                      response_required   = false;
    uint32_t                  notifications       = 0U;
    uint32_t                  expected_tick_count = 0U;
    RunStateManagerStatus_T   run_state_status{};

    void SetUp() override
    {
        g_mock_deps = new NiceMock<MockHostProcessMessageDependencies>();

        std::memset( &incoming, 0, sizeof( incoming ) );
        std::memset( &outgoing, 0, sizeof( outgoing ) );
        std::memset( &overflow_outgoing, 0, sizeof( overflow_outgoing ) );
        std::memset( data, 0, sizeof( data ) );
        response_required   = false;
        notifications       = 0U;
        expected_tick_count = 0U;
        std::memset( &run_state_status, 0, sizeof( run_state_status ) );

        ON_CALL( *g_mock_deps, HOST_INTERFACE_Config_Message_To_Driver( _, _ ) )
            .WillByDefault( Return( HOST_INTERFACE_STATUS_OK ) );
        ON_CALL( *g_mock_deps, HOST_INTERFACE_Commit_Config_Message( _ ) )
            .WillByDefault( Return( HOST_INTERFACE_STATUS_OK ) );
        ON_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_Reset() ).WillByDefault( [] {} );
        ON_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
            .WillByDefault( Return( HOST_INTERFACE_STATUS_OK ) );
        ON_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_Reset() ).WillByDefault( [] {} );
        ON_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
            .WillByDefault( Return( RESULT_MESSAGE_PRODUCER_STATUS_OK ) );

        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestPackageReceive() )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestConfiguration() )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestExecution( _ ) )
            .WillByDefault( []( RequestExecutionArgument_T request ) {
                if ( request != nullptr )
                {
                    ( void )request->tick_count;
                }
                return RUN_STATE_EXECUTION_REQUEST_ACCEPTED;
            } );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestResultTransfer() )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestFault( _ ) )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestReset() ).WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestResultTransferComplete() )
            .WillByDefault( Return( true ) );
        ON_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
            .WillByDefault( [this]( RunStateManagerStatus_T* status ) {
                if ( status != nullptr )
                {
                    *status = run_state_status;
                }
            } );
    }

    void TearDown() override
    {
        delete g_mock_deps;
        g_mock_deps = nullptr;
    }

    void SetIncomingType(
        HIL_Application_Message_Type_T    type,
        HIL_Application_Message_Subtype_T subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE )
    {
        incoming.type    = type;
        incoming.subtype = subtype;
    }
};

TEST_F( HostProcessMessageTest, DefaultErrorInitializesProtocolErrorEnvelope )
{
    const auto status = HOST_INTERFACE_Test_Access_Default_Error( &outgoing );

    EXPECT_EQ( status, HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( outgoing.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    EXPECT_EQ( outgoing.body.error.category, HIL_APPLICATION_ERROR_CATEGORY_INVALID );
    EXPECT_EQ( outgoing.body.error.recoverable, 1U );
    EXPECT_EQ( outgoing.body.error.has_tick_number, 0U );
    EXPECT_EQ( outgoing.body.error.tick_number, 0U );
    EXPECT_EQ( outgoing.body.error.detail, 0U );
    EXPECT_EQ( outgoing.body.error.diagnostic_data.size, 0U );
    EXPECT_EQ( outgoing.body.error.diagnostic_data.data, nullptr );
}

TEST_F( HostProcessMessageTest, StateToStateRequestMapsPackageReceive )
{
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestPackageReceive() )
        .WillOnce( Return( true ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_TEST_PACKAGE_RECEIVE, 0U ),
        HOST_INTERFACE_STATUS_OK );
}

TEST_F( HostProcessMessageTest, StateToStateRequestReturnsFailureWhenPackageReceiveRejected )
{
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestPackageReceive() )
        .WillOnce( Return( false ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_TEST_PACKAGE_RECEIVE, 0U ),
        HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE );
}

TEST_F( HostProcessMessageTest, StateToStateRequestPassesExecutionTickCount )
{
    constexpr uint32_t expected_tick = 1234U;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestExecution( _ ) )
        .WillOnce( Invoke( [expected_tick]( RequestExecutionArgument_T request ) {
            EXPECT_NE( request, nullptr );
            EXPECT_EQ( request->tick_count, expected_tick );
            return RUN_STATE_EXECUTION_REQUEST_ACCEPTED;
        } ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_EXECUTION, expected_tick ),
        HOST_INTERFACE_STATUS_OK );
}

TEST_F( HostProcessMessageTest, StateToStateRequestRejectsUnsupportedRequests )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_IDLE, 0U ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_ARMED, 0U ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_RESULT_FINALISATION, 0U ),
        HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_RESULTS_READY, 0U ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
}

TEST_F( HostProcessMessageTest, StateToStateRequestMapsResultTransferFaultAbortAndReset )
{
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestResultTransfer() )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_EXTERNAL_REQUEST ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestReset() ).WillOnce( Return( true ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_RESULT_TRANSFER, 0U ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_FAULT, 0U ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_ABORT, 0U ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( HOST_INTERFACE_Test_Access_State_To_State_Request( HOST_REQUEST_RESET, 0U ),
               HOST_INTERFACE_STATUS_OK );
}

TEST_F( HostProcessMessageTest, RequestStateTransitionReturnsUnsupportedWithoutStatusCheck )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Request_State_Transition( RUN_STATE_EXECUTION,
                                                                    HOST_REQUEST_ARMED, 2U, 0U ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) ).Times( 0 );
}

TEST_F( HostProcessMessageTest, RequestStateTransitionSucceedsWhenExpectedStateReached )
{
    run_state_status.state = RUN_STATE_CONFIGURATION;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestConfiguration() )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Request_State_Transition(
                   RUN_STATE_CONFIGURATION, HOST_REQUEST_CONFIGURATION, 1U, 0U ),
               HOST_INTERFACE_STATUS_OK );
}

TEST_F( HostProcessMessageTest, RequestStateTransitionReturnsInternalErrorForFaultedState )
{
    run_state_status.state = RUN_STATE_FAULT;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Request_State_Transition(
                   RUN_STATE_EXECUTION, HOST_REQUEST_EXECUTION, 1U, 0U ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );
}

TEST_F( HostProcessMessageTest, RequestStateTransitionWithZeroAttemptsReturnsFailure )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Request_State_Transition(
                   RUN_STATE_EXECUTION, HOST_REQUEST_EXECUTION, 0U, 0U ),
               HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE );
}

TEST_F( HostProcessMessageTest, InfoRequestInvalidQueryProducesError )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST );
    incoming.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Request(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, InfoRequestBasicBuildsSystemInfoResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST );
    incoming.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Request(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE );
    EXPECT_EQ( outgoing.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    EXPECT_EQ( outgoing.body.system_info_response.firmware_git_hash.size, 1U );
    EXPECT_EQ( outgoing.body.system_info_response.firmware_git_hash.data, data );
    EXPECT_EQ( data[0], 0U );
    EXPECT_EQ( outgoing.body.system_info_response.diagnostic_data.size, 0U );
    EXPECT_EQ( outgoing.body.system_info_response.diagnostic_data.data, nullptr );
}

TEST_F( HostProcessMessageTest, InfoRequestBasicReportsBufferTooSmall )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST );
    incoming.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Request( &incoming, &outgoing,
                                                                &response_required, data, 0U ),
               HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, InfoResponseRejectsUnsupportedSubtype )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_RESERVED );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Response(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, InfoResponseAcceptsMatchingProtocolVersion )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    incoming.body.system_info_response.application_protocol_major = HIL_RIG_PROTOCOL_VERSION_MAJOR;
    incoming.body.system_info_response.application_protocol_minor = HIL_RIG_PROTOCOL_VERSION_MINOR;
    incoming.body.system_info_response.application_protocol_patch = HIL_RIG_PROTOCOL_VERSION_PATCH;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Response(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, InfoResponseRejectsMajorMismatchEvenWhenMinorAndPatchMatch )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    incoming.body.system_info_response.application_protocol_major =
        static_cast<uint16_t>( HIL_RIG_PROTOCOL_VERSION_MAJOR + 1U );
    incoming.body.system_info_response.application_protocol_minor = HIL_RIG_PROTOCOL_VERSION_MINOR;
    incoming.body.system_info_response.application_protocol_patch = HIL_RIG_PROTOCOL_VERSION_PATCH;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Response(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, InfoResponseWithFullyMismatchedVersionProducesError )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    incoming.body.system_info_response.application_protocol_major =
        static_cast<uint8_t>( HIL_RIG_PROTOCOL_VERSION_MAJOR + 1U );
    incoming.body.system_info_response.application_protocol_minor =
        static_cast<uint8_t>( HIL_RIG_PROTOCOL_VERSION_MINOR + 1U );
    incoming.body.system_info_response.application_protocol_patch =
        static_cast<uint8_t>( HIL_RIG_PROTOCOL_VERSION_PATCH + 1U );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Info_Response(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, TestConfigurationConversionFailureReturnsProtocolErrorResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION );
    incoming.body.test_configuration.expected_tick_count = 900U;

    EXPECT_CALL( *g_mock_deps, HOST_INTERFACE_Config_Message_To_Driver( _, _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_INVALID_ARGUMENT ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Configuration(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( outgoing.body.error.category, HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL );
}

TEST_F( HostProcessMessageTest, TestConfigurationCommitFailureReturnsProtocolErrorResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION );
    run_state_status.state = RUN_STATE_TEST_PACKAGE_RECEIVE;

    EXPECT_CALL( *g_mock_deps, HOST_INTERFACE_Commit_Config_Message( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_INTERNAL_ERROR ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Configuration(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( outgoing.body.error.category, HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL );
}

TEST_F( HostProcessMessageTest, TestConfigurationSuccessCopiesExpectedTicksAndResetsHandlers )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION );
    incoming.body.test_configuration.expected_tick_count = 4567U;
    run_state_status.state                               = RUN_STATE_TEST_PACKAGE_RECEIVE;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestPackageReceive() )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );
    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_Reset() ).Times( 1 );
    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_Reset() ).Times( 1 );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Configuration(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
    EXPECT_EQ( expected_tick_count, 4567U );
}

TEST_F( HostProcessMessageTest, TestInstructionHandlerFailureProducesRejectedResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION );
    incoming.body.test_instruction.tick_number = 1U;
    expected_tick_count                        = 10U;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_VALIDATION_FAILED ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Instructions(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED );
    EXPECT_EQ( outgoing.body.response.tick_number, 1U );
}

TEST_F( HostProcessMessageTest, TestInstructionBuildsTickResponseForNonFinalInstruction )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION );
    incoming.body.test_instruction.tick_number = 4U;
    expected_tick_count                        = 9U;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_OK ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Instructions(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED );
    EXPECT_EQ( outgoing.body.response.tick_number, 4U );
}

TEST_F( HostProcessMessageTest, FinalInstructionUsesUnsupportedArmedTransitionPath )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION );
    incoming.body.test_instruction.tick_number = 9U;
    expected_tick_count                        = 9U;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_OK ) );

    EXPECT_EQ(
        HOST_INTERFACE_Test_Access_Process_Test_Instructions(
            &incoming, &outgoing, &response_required, data, sizeof( data ), &expected_tick_count ),
        HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( outgoing.body.error.category, HIL_APPLICATION_ERROR_CATEGORY_RESERVED );
}

TEST_F( HostProcessMessageTest, VariableInstructionDataIsNotImplemented )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Variable_Instruction_Data(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_NOT_IMPLEMENTED );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, ExecutionControlRejectsInvalidAndReservedCommands )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL );

    incoming.body.execution_control.command = HIL_APPLICATION_CONTROL_INVALID;
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Execution_Control(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_FALSE( response_required );

    incoming.body.execution_control.command = HIL_APPLICATION_CONTROL_RESERVED;
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Execution_Control(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, ExecutionControlStartSucceedsWhenExecutionStateIsReached )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL );
    incoming.body.execution_control.command = HIL_APPLICATION_CONTROL_START;
    run_state_status.state                  = RUN_STATE_EXECUTION;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestExecution( _ ) )
        .WillOnce( Return( RUN_STATE_EXECUTION_REQUEST_ACCEPTED ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Execution_Control(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, ExecutionControlAbortSucceedsWhenFaultStateIsReached )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL );
    incoming.body.execution_control.command = HIL_APPLICATION_CONTROL_ABORT;
    run_state_status.state                  = RUN_STATE_FAULT;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_EXTERNAL_REQUEST ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Execution_Control(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, GlobalControlResetSucceedsWhenIdleStateIsReached )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL );
    incoming.body.global_control.command = HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION;
    run_state_status.state               = RUN_STATE_IDLE;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestReset() ).WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Global_Control(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, TestResultFromHostProducesErrorResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Result(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, VariableResultDataIsNotImplemented )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Variable_Result_Data(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_NOT_IMPLEMENTED );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, ResponseFromHostProducesErrorResponse )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Response(
                   &incoming, &outgoing, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, IncomingErrorRequestsFaultAndDoesNotRespondOnSuccess )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    incoming.body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_EXTERNAL_REQUEST ) )
        .WillOnce( Return( true ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Error( &incoming, &outgoing, &response_required,
                                                         data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, IncomingErrorProducesErrorResponseWhenFaultRequestFails )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    incoming.body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;

    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_EXTERNAL_REQUEST ) )
        .WillOnce( Return( false ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Error( &incoming, &outgoing, &response_required,
                                                         data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, UnimplementedNotificationsReturnNotImplemented )
{
    struct NotificationCase
    {
        uint32_t flag;
        HOST_Interface_Status_T ( *function )( HIL_Application_Message_T*, uint32_t*, bool*,
                                               uint8_t*, size_t );
    };

    const NotificationCase cases[] = {
        { HOST_INTERFACE_NOTIFY_PACKAGE_RECEIVE,
          HOST_INTERFACE_Test_Access_Process_Package_Received_Notification },
        { HOST_INTERFACE_NOTIFY_CONFIGURATION,
          HOST_INTERFACE_Test_Access_Process_Config_Started_Notification },
        { HOST_INTERFACE_NOTIFY_EXECUTION,
          HOST_INTERFACE_Test_Access_Process_Execution_Started_Notification },
        { HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE,
          HOST_INTERFACE_Test_Access_Process_Execution_Complete_Notification },
        { HOST_INTERFACE_NOTIFY_RESULT_TRANSFER_COMPLETE,
          HOST_INTERFACE_Test_Access_Process_Transfer_Complete_Notification },
    };

    for ( const NotificationCase& test_case : cases )
    {
        notifications = test_case.flag;
        EXPECT_EQ( test_case.function( &outgoing, &notifications, &response_required, data,
                                       sizeof( data ) ),
                   HOST_INTERFACE_STATUS_NOT_IMPLEMENTED );
    }
}

TEST_F( HostProcessMessageTest, ResultTransferNotificationProducesNextResultMessage )
{
    notifications = HOST_INTERFACE_NOTIFY_RESULT_TRANSFER;

    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
        .WillOnce( DoAll( Invoke( []( HIL_Application_Message_T* message ) {
                              message->type    = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
                              message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
                          } ),
                          Return( RESULT_MESSAGE_PRODUCER_STATUS_OK ) ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Result_Transfer_Notification(
                   &outgoing, &notifications, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    EXPECT_EQ( notifications, HOST_INTERFACE_NOTIFY_RESULT_TRANSFER );
}

TEST_F( HostProcessMessageTest,
        ResultTransferNotificationReturnsInternalErrorWhenNoDataIsAvailable )
{
    notifications = HOST_INTERFACE_NOTIFY_RESULT_TRANSFER;

    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
        .WillOnce( Return( RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Result_Transfer_Notification(
                   &outgoing, &notifications, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );
    EXPECT_FALSE( response_required );
    EXPECT_EQ( notifications, HOST_INTERFACE_NOTIFY_RESULT_TRANSFER );
}

TEST_F( HostProcessMessageTest, ResultTransferNotificationClearsFlagAtEndOfStream )
{
    notifications = HOST_INTERFACE_NOTIFY_RESULT_TRANSFER;

    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
        .WillOnce( Return( RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestResultTransferComplete() )
        .WillOnce( Return( true ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Result_Transfer_Notification(
                   &outgoing, &notifications, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
    EXPECT_EQ( notifications, 0U );
}

TEST_F( HostProcessMessageTest,
        ResultTransferNotificationPropagatesUnexpectedProducerStatusAsInternalError )
{
    notifications = HOST_INTERFACE_NOTIFY_RESULT_TRANSFER;

    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
        .WillOnce( Return( RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Result_Transfer_Notification(
                   &outgoing, &notifications, &response_required, data, sizeof( data ) ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );
    EXPECT_FALSE( response_required );
}

TEST_F( HostProcessMessageTest, IncomingDispatcherRejectsNullArguments )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Incoming_Message(
                   true, nullptr, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Incoming_Message(
                   true, &incoming, nullptr, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );
}

TEST_F( HostProcessMessageTest, IncomingDispatcherRejectsUnsupportedMessageType )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_RESERVED );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Incoming_Message(
                   true, &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE );
}

TEST_F( HostProcessMessageTest, IncomingDispatcherRoutesSystemInfoRequest )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST );
    incoming.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Incoming_Message(
                   true, &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
}

TEST_F( HostProcessMessageTest, IncomingDispatcherDoesNothingWhenNoMessageIsAvailable )
{
    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Incoming_Message(
                   false, &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

/**
 * @brief Verifies that internal dispatcher returns OK and requires no response when notifications
 * bitmask is zero.
 */
TEST_F( HostProcessMessageTest, InternalDispatcherHandlesZeroNotificationsGracefully )
{
    notifications = 0U;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Internal_Message(
                   &outgoing, &response_required, data, sizeof( data ), &notifications ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
    EXPECT_EQ( notifications, 0U );
}

TEST_F( HostProcessMessageTest, InternalDispatcherRejectsUnknownNotification )
{
    notifications = HOST_INTERFACE_NOTIFY_FAULT;

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Internal_Message(
                   &outgoing, &response_required, data, sizeof( data ), &notifications ),
               HOST_INTERFACE_STATUS_UNSUPPORTED_NOTIFICATION );
    EXPECT_EQ( notifications, 0U );
}

TEST_F( HostProcessMessageTest, ProcessMessageRejectsNullRequiredArguments )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST );
    incoming.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID;

    EXPECT_EQ( HOST_INTERFACE_process_message( true, nullptr, true, &outgoing, &overflow_outgoing,
                                               &response_required, data, sizeof( data ),
                                               &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );

    EXPECT_EQ( HOST_INTERFACE_process_message( true, &incoming, true, nullptr, &overflow_outgoing,
                                               &response_required, data, sizeof( data ),
                                               &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );
}

TEST_F( HostProcessMessageTest, ProcessMessageCopiesResponseAndPreservesTestId )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    incoming.has_test_id = 1U;
    for ( size_t i = 0U; i < sizeof( incoming.test_id.bytes ); i++ )
    {
        incoming.test_id.bytes[i] = static_cast<uint8_t>( i + 1U );
    }

    EXPECT_EQ( HOST_INTERFACE_process_message( true, &incoming, true, &outgoing, &overflow_outgoing,
                                               &response_required, data, sizeof( data ),
                                               &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( outgoing.has_test_id, 1U );
    EXPECT_EQ( std::memcmp( &outgoing.test_id, &incoming.test_id, sizeof( incoming.test_id ) ), 0 );
}

/**
 * @brief Verifies that response is stored in overflow buffer with test_id preserved when output is
 * busy.
 */
TEST_F( HostProcessMessageTest, ProcessMessageStoresResponseInOverflowWhenOutputIsBusy )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    incoming.has_test_id = 1U;
    for ( size_t i = 0U; i < sizeof( incoming.test_id.bytes ); i++ )
    {
        incoming.test_id.bytes[i] = static_cast<uint8_t>( i + 1U );
    }

    EXPECT_EQ( HOST_INTERFACE_process_message(
                   true, &incoming, false, &outgoing, &overflow_outgoing, &response_required, data,
                   sizeof( data ), &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_OUTGOING_REQUIRED );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( overflow_outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
    EXPECT_EQ( overflow_outgoing.has_test_id, 1U );
    EXPECT_EQ(
        std::memcmp( &overflow_outgoing.test_id, &incoming.test_id, sizeof( incoming.test_id ) ),
        0 );
}

TEST_F( HostProcessMessageTest, ProcessMessageCanServiceResultTransferAfterNonRespondingInput )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    incoming.body.system_info_response.application_protocol_major = HIL_RIG_PROTOCOL_VERSION_MAJOR;
    incoming.body.system_info_response.application_protocol_minor = HIL_RIG_PROTOCOL_VERSION_MINOR;
    incoming.body.system_info_response.application_protocol_patch = HIL_RIG_PROTOCOL_VERSION_PATCH;
    notifications = HOST_INTERFACE_NOTIFY_RESULT_TRANSFER;

    EXPECT_CALL( *g_mock_deps, RESULT_MESSAGE_PRODUCER_ProduceNextMessage( _ ) )
        .WillOnce( Invoke( []( HIL_Application_Message_T* message ) {
            message->type = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
            return RESULT_MESSAGE_PRODUCER_STATUS_OK;
        } ) );

    EXPECT_EQ( HOST_INTERFACE_process_message( true, &incoming, true, &outgoing, &overflow_outgoing,
                                               &response_required, data, sizeof( data ),
                                               &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
}

/**
 * @brief Verifies that process_message returns OK and requires no response when no incoming message
 * is available, even if output is busy.
 */
TEST_F( HostProcessMessageTest, ProcessMessageReturnsOkAndNoResponseWhenNoIncomingMessage )
{
    response_required = true;
    notifications     = 0U;

    EXPECT_EQ( HOST_INTERFACE_process_message(
                   false, &incoming, false, &outgoing, &overflow_outgoing, &response_required, data,
                   sizeof( data ), &notifications, &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_FALSE( response_required );
}

/**-----------------------------------------------------------------------------
 *  Test Instruction Processing Tests
 *------------------------------------------------------------------------------
 */

TEST_F( HostProcessMessageTest, ProcessTestInstructionsAcceptsValidInstruction )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    incoming.body.test_instruction.tick_number = 1U;
    expected_tick_count                        = 100U;
    response_required                          = false;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_OK ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Instructions(
                   &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_NONE );
    EXPECT_EQ( outgoing.body.response.tick_number, 1U );
}

TEST_F( HostProcessMessageTest, ProcessTestInstructionsRejectsInvalidInstruction )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    incoming.body.test_instruction.tick_number = 2U;
    expected_tick_count                        = 100U;
    response_required                          = false;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_VALIDATION_FAILED ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Instructions(
                   &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED );
    EXPECT_EQ( outgoing.body.response.tick_number, 2U );
}

TEST_F( HostProcessMessageTest, ProcessTestInstructionsRejectsInconsistentTick )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    incoming.body.test_instruction.tick_number = 2U;
    expected_tick_count                        = 100U;
    response_required                          = false;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_INCONSISTENT_TICK ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Instructions(
                   &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK );
    EXPECT_EQ( outgoing.body.response.tick_number, 2U );
}

TEST_F( HostProcessMessageTest, ProcessTestInstructionsFinalTickTransitionsToConfiguration )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    incoming.body.test_instruction.tick_number = 100U;
    expected_tick_count                        = 100U;
    response_required                          = false;
    run_state_status.state                     = RUN_STATE_CONFIGURATION;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_OK ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestConfiguration() )
        .WillOnce( Return( true ) );
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_GetStatus( _ ) )
        .WillOnce(
            Invoke( [this]( RunStateManagerStatus_T* status ) { *status = run_state_status; } ) );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Instructions(
                   &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_NONE );
    EXPECT_EQ( outgoing.body.response.tick_number, 100U );
}

TEST_F( HostProcessMessageTest, ProcessTestInstructionsFinalTickRejectsOnValidationFailureWithoutTransition )
{
    SetIncomingType( HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION,
                     HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    incoming.body.test_instruction.tick_number = 100U;
    expected_tick_count                        = 100U;
    response_required                          = false;

    EXPECT_CALL( *g_mock_deps, HOST_INSTRUCTION_HANDLER_HandleInstruction( _ ) )
        .WillOnce( Return( HOST_INTERFACE_STATUS_VALIDATION_FAILED ) );
    /* Should NEVER call RequestConfiguration if instruction handling failed */
    EXPECT_CALL( *g_mock_deps, RUN_STATE_MANAGER_RequestConfiguration() ).Times( 0 );

    EXPECT_EQ( HOST_INTERFACE_Test_Access_Process_Test_Instructions(
                   &incoming, &outgoing, &response_required, data, sizeof( data ),
                   &expected_tick_count ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( response_required );
    EXPECT_EQ( outgoing.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( outgoing.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( outgoing.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( outgoing.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED );
    EXPECT_EQ( outgoing.body.response.tick_number, 100U );
}

