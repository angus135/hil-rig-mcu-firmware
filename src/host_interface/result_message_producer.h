/******************************************************************************
 *  File:       result_message_producer.h
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Public interface for producing outbound Application Test Result messages
 *      during the test result transfer phase by retrieving, unpacking, and
 *      aggregating raw driver measurement records from the Flash Manager.
 *
 *  Notes:
 *      Constructs application-level HIL_Application_Test_Result_T messages from
 *      the stored packed [FlashManagerResultHeader_T][payload] stream. Called
 *      iteratively by the Host Interface task during result retrieval.
 ******************************************************************************/

#ifndef RESULT_MESSAGE_PRODUCER_H
#define RESULT_MESSAGE_PRODUCER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hil_rig_protocol/application/application_message.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Status codes returned by the Result Message Producer pipeline.
 */
typedef enum
{
    /** A complete test result message was successfully constructed for the next tick. */
    RESULT_MESSAGE_PRODUCER_STATUS_OK = 0,

    /** No buffered result data is currently available from Flash Manager; retry later. */
    RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE,

    /** All stored result records have been consumed from Flash Manager. */
    RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM,

    /** Invalid argument (e.g. NULL out_message pointer). */
    RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT,

    /** A record header, peripheral type, or payload in the result stream was corrupt or invalid. */
    RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA,

    /** Flash Manager reported an unexpected lifecycle state or internal failure. */
    RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR
} Result_Message_Producer_Status_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the result message producer state and internal stream reader.
 *
 * @details Clears any buffered record bytes, partial chunk cursor, and cached
 *          measurement aggregation state. Must be called before starting a
 *          new result transfer session.
 *
 * @note Call from Host Interface task context only.
 */
void RESULT_MESSAGE_PRODUCER_Reset( void );

/**
 * @brief Retrieves raw measurement record(s) from Flash Manager and constructs
 *        the next Application Test Result protocol message.
 *
 * @details Reads packed [FlashManagerResultHeader_T][payload] records from the
 *          Flash Manager result stream, unpacks driver measurement payloads
 *          (digital inputs, analogue inputs, PWM capture), aggregates all measurements
 *          belonging to the next execution tick timestamp, and populates the
 *          supplied Application Message envelope with message type
 *          HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT.
 *
 *          If Flash Manager is currently prefetching from NAND (BUSY), this function
 *          returns RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE. When all stored result
 *          bytes have been consumed, returns RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM.
 *
 * @par Test ID Stamping Contract:
 *      This function sets out_message->type to HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT,
 *      out_message->subtype to HIL_APPLICATION_MESSAGE_SUBTYPE_NONE, and
 *      out_message->has_test_id to 1U. The 16-byte test_id field is left zeroed for
 *      the calling Host Interface / session task to stamp with the active session's
 *      Test ID before serialisation and transmission over USB.
 *
 * @param[out] out_message Pointer to destination Application Message structure to populate.
 *                         Must not be NULL.
 *
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_OK
 *         A complete Test Result message for one tick was successfully constructed.
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE
 *         Flash Manager has no buffered bytes ready yet; caller should retry.
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM
 *         The entire result stream has been consumed; no more results available.
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT
 *         out_message is NULL.
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA
 *         Encountered invalid header length, unrecognized peripheral type, or malformed record.
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR
 *         Flash Manager reported an internal error or invalid lifecycle state.
 *
 * @note Call from Host Interface task context only while Flash Manager is in
 *       FLASH_MANAGER_STATE_TRANSFERRING_RESULTS.
 */
Result_Message_Producer_Status_T
RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* out_message );

#ifdef __cplusplus
}
#endif

#endif /* RESULT_MESSAGE_PRODUCER_H */
