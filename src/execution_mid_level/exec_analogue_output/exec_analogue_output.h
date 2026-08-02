/******************************************************************************
 *  File:       exec_analogue_output.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_ANALOGUE_OUTPUT_H
#define EXEC_ANALOGUE_OUTPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** @brief Number of bytes in one DAC write frame on the SPI wire. */
#define EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ( 3U )

/** @brief Maximum number of analogue-output changes in one execution tick. */
#define EXEC_ANALOG_OUTPUT_BATCH_MAX_FRAMES ( 6U )

/** @brief Maximum contiguous DAC payload submitted for one execution tick. */
#define EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES                                                         \
    ( EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES * EXEC_ANALOG_OUTPUT_BATCH_MAX_FRAMES )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Exact SPI wire representation of one prepared DAC write.
 *
 * This frame is created during configuration or test preparation. Its bytes
 * are already in DAC wire order and may be stored directly with a future
 * flash-backed execution instruction without further channel, register, or
 * host-endian data packing.
 */
typedef struct AnalogueOutputPreparedFrame_T
{
    uint8_t bytes[EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES];
} AnalogueOutputPreparedFrame_T;

/**
 * @brief Fixed-capacity wire payload for one execution tick.
 *
 * Preparation-time code appends zero through six prepared frames in schedule
 * order. The valid prefix of @ref bytes is described by @ref byte_count and
 * can be submitted directly without allocation, sorting, or concatenation.
 * The fixed representation is suitable for inclusion in future flash-backed
 * execution data.
 */
typedef struct AnalogueOutputPreparedBatch_T
{
    uint8_t bytes[EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES];
    uint8_t byte_count;
} AnalogueOutputPreparedBatch_T;

#if defined( __cplusplus )
static_assert( sizeof( AnalogueOutputPreparedFrame_T ) == EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES,
               "A prepared analogue-output frame must contain exactly three wire bytes" );
#else
_Static_assert( sizeof( AnalogueOutputPreparedFrame_T ) == EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES,
                "A prepared analogue-output frame must contain exactly three wire bytes" );
#endif

#if defined( __cplusplus )
static_assert( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES == 18U,
               "Six prepared analogue-output frames must occupy exactly 18 wire bytes" );
static_assert( sizeof( AnalogueOutputPreparedBatch_T )
                   == ( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES + 1U ),
               "A prepared analogue-output batch must use deterministic inline storage" );
#else
_Static_assert( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES == 18U,
                "Six prepared analogue-output frames must occupy exactly 18 wire bytes" );
_Static_assert( sizeof( AnalogueOutputPreparedBatch_T )
                    == ( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES + 1U ),
                "A prepared analogue-output batch must use deterministic inline storage" );
#endif

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure the DAC module and program initial DAC registers.
 *
 * @param use_external_vref
 *     If true, configure the DAC to use the external buffered VREF pin.
 *     If false, configure the DAC to use VDD as the reference.
 *
 * @return
 *     true on success, false on SPI transmission failure.
 */
bool EXEC_ANALOGUE_OUTPUT_Config( bool use_external_vref );

/**
 * @brief Configure and start the SPI hardware channel dedicated to DAC communication.
 *
 * Intended to only be used for console testing to set up the SPI channel independently
 *
 * Sets up the SPI peripheral with the configuration required by the
 * MCP48CVB28T-20E_ST octal DAC: 8-bit data size, 703K baud rate, MSB first,
 * CPOL low, CPHA 1 edge.
 *
 * This function must be called once during system initialization to prepare
 * SPI for use before any DAC operations are performed. In the real project,
 * this setup will be performed by the system/board initialization layer.
 *
 * This function is provided as a separate helper for console testing so that
 * test commands can independently set up the SPI channel without integrating
 * into the full system initialization sequence.
 *
 * The SPI channel is activated for immediate use. After this function returns
 * successfully, the channel is ready to transmit frames to the DAC.
 *
 * @return
 *     true if SPI configuration and startup completed successfully.
 *     false if hardware configuration or startup failed.
 */
bool EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup( void );

/**
 * @brief Return whether the analogue output module has been configured.
 *
 * Useful for console commands to know if `EXEC_ANALOGUE_OUTPUT_Config()` has
 * previously been called successfully.
 */
bool EXEC_ANALOG_OUTPUT_Is_Configured( void );

/**
 * @brief Prepare one DAC frame outside the execution hot path.
 *
 * Validates channels 0-5 and rejects non-finite voltage requests. Finite
 * voltages are clamped to 0-20 V and mapped to the existing 12-bit DAC count
 * using the established round-to-nearest calculation. The resulting command,
 * data MSB, and data LSB bytes are ready for direct SPI submission.
 *
 * This preparation-time API is the intended extension point for future
 * physical calibration. Such calibration must not change the prepared-frame
 * format consumed by the execution hot path.
 *
 * @param[in] channel
 *     DAC output channel number. Only channels 0-5 are supported.
 *
 * @param[in] input_voltage_v
 *     Requested output voltage. Finite values are clamped to 0-20 V.
 *
 * @param[out] prepared_frame
 *     Destination for the exact three-byte DAC wire frame. It is not modified
 *     when validation fails.
 *
 * @return true if the frame was prepared successfully.
 * @return false if the destination is NULL, the channel is unsupported, or
 *     the requested voltage is NaN or infinity.
 */
bool EXEC_ANALOG_OUTPUT_Prepare_Frame( uint8_t channel, float input_voltage_v,
                                       AnalogueOutputPreparedFrame_T* prepared_frame );

/**
 * @brief Initialize an empty prepared batch during test preparation.
 *
 * @param[out] prepared_batch
 *     Fixed-capacity batch to initialize. All inline storage is cleared.
 *
 * @return true if the batch was initialized.
 * @return false if prepared_batch is NULL.
 */
bool EXEC_ANALOG_OUTPUT_Batch_Init( AnalogueOutputPreparedBatch_T* prepared_batch );

/**
 * @brief Append one prepared frame to a per-tick batch.
 *
 * Frames remain in append order and their wire bytes are stored contiguously.
 * A failed append leaves the complete destination batch unchanged.
 *
 * @param[in,out] prepared_batch
 *     Batch previously initialized by EXEC_ANALOG_OUTPUT_Batch_Init().
 *
 * @param[in] prepared_frame
 *     Exact three-byte wire frame to append.
 *
 * @return true if the frame was appended.
 * @return false if either pointer is NULL, the batch is malformed, or six
 *     frames are already present.
 */
bool EXEC_ANALOG_OUTPUT_Batch_Append( AnalogueOutputPreparedBatch_T*       prepared_batch,
                                      const AnalogueOutputPreparedFrame_T* prepared_frame );

/**
 * @brief Submit one previously prepared per-tick batch on the execution path.
 *
 * The valid contiguous byte prefix is loaded into SPI as one operation and
 * triggered exactly once. An empty batch is a successful no-op. This function
 * performs no voltage conversion, channel validation, calibration, register
 * calculation, frame construction, per-frame submission, retry, or wait.
 *
 * A false return means the scheduled physical output update did not occur.
 * The future execution-manager caller must treat it as an execution fault and
 * must not continue silently.
 *
 * @param[in] prepared_batch
 *     Batch created during configuration or test preparation.
 *
 * @return true if an empty batch required no work or the complete payload was
 *     accepted and triggered.
 * @return false if the module is not configured, the batch is malformed, or
 *     SPI rejected the complete payload.
 */
bool EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch(
    const AnalogueOutputPreparedBatch_T* prepared_batch );

/**
 * @brief Write a voltage to a single DAC output channel.
 *
 * Accepts a voltage in the range 0V to 20V, clamps it to the valid input range,
 * scales it to the DAC's 0-5V output range, converts it to a 12-bit DAC code,
 * and transmits a write command to the MCP48 DAC via SPI4.
 *
 * Input voltage scaling and clamping:
 * - Input range: 0V to 20V (nominally full scale at 20V)
 * - Values below 0V are clamped to 0V
 * - Values above 20V are clamped to 20V
 * - Scaled to DAC output range: 0V to 5V
 * - DAC code formula: code = (clamped_voltage / 20.0) * 4095
 *
 * Only channels 0-5 are functional. Attempts to write to channels 6-7 will
 * fail with false return code because those channels are disabled (configured
 * in open-circuit mode).
 *
 * The module must be initialized via EXEC_ANALOGUE_OUTPUT_Config() before this
 * function is called. Writing to an uninitialized module returns false.
 * This compatibility API is intended for console commands, manual testing,
 * and other non-hot-path use. The future execution manager should submit
 * prepared data directly.
 *
 * @param channel
 *     The DAC output channel number (0-5 for active channels, 6-7 disabled).
 *
 * @param input_voltage_v
 *     The desired output voltage in volts (0V to 20V, clamped and scaled).
 *
 * @return
 *     true if the voltage write was accepted and queued to SPI for transmission.
 *     false if the module is not initialized, the channel is invalid (>= 6),
 *     or SPI transmission failed.
 */
bool EXEC_ANALOG_OUTPUT_Write_Voltage( uint8_t channel, float input_voltage_v );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_OUTPUT_H */
