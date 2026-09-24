/******************************************************************************
 *  File:       variable_result_message_producer.h
 *  Author:     Callum Rafferty
 *  Created:    24-Sep-2026
 *
 *  Description:
 *      Public interface for producing outbound Application Variable Test Result
 *      messages (VARIABLE_TEST_RESULT, Type 34) during the result transfer phase
 *      by retrieving, unpacking, and aggregating raw driver measurement records
 *      from the Flash Manager.
 *
 *  Notes:
 *      Constructs sparse application-level HIL_Application_Variable_Test_Result_T
 *      messages from the stored packed [FlashManagerResultHeader_T][payload] stream.
 *      Supports all captured peripheral families: Digital, Analogue, PWM, UART,
 *      SPI, and CAN.
 ******************************************************************************/

#ifndef VARIABLE_RESULT_MESSAGE_PRODUCER_H
#define VARIABLE_RESULT_MESSAGE_PRODUCER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hil_rig_protocol/application/application_message.h"
#include "result_message_producer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** @brief Maximum number of distinct captured records staged per variable result tick. */
#define VARIABLE_RESULT_MAX_STAGED_RECORDS ( 32U )

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the variable result message producer state and internal stream reader.
 *
 * @details Clears any buffered record bytes, staging records, and read cursor.
 *          Must be called before starting a new result transfer session.
 *
 * @note Call from Host Interface task context only.
 */
void VARIABLE_RESULT_MESSAGE_PRODUCER_Reset( void );

/**
 * @brief Retrieves raw measurement record(s) from Flash Manager and constructs
 *        the next Application Variable Test Result protocol message.
 *
 * @details Reads packed [FlashManagerResultHeader_T][payload] records from the
 *          Flash Manager result stream, unpacks driver measurement payloads
 *          (digital inputs, analogue inputs, PWM capture, UART, SPI, CAN),
 *          aggregates all records belonging to the next active execution timestamp,
 *          converts that timestamp to the protocol's zero-based result tick, and
 *          populates the supplied Application Message envelope with message type
 *          HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT.
 *
 *          If Flash Manager is currently prefetching from NAND (BUSY), this function
 *          returns RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE. When all stored
 *          result bytes have been consumed, returns RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM.
 *
 * @param[out] out_message Pointer to destination Application Message structure to populate.
 *                         Must not be NULL.
 *
 * @retval RESULT_MESSAGE_PRODUCER_STATUS_OK
 *         A complete Variable Test Result message for one tick was successfully constructed.
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
VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* out_message );

#ifdef __cplusplus
}
#endif

#endif /* VARIABLE_RESULT_MESSAGE_PRODUCER_H */
