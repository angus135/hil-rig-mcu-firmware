/******************************************************************************
 *  File:       logic_expander.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef LOGIC_EXPANDER_H
#define LOGIC_EXPANDER_H

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

#define LOGIC_EXPANDER_COUNT              ( 8U )
#define LOGIC_EXPANDER_PORT_WIDTH_BITS    ( 8U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum LogicExpanderPort_T
{
	LOGIC_EXPANDER_PORT_A,
	LOGIC_EXPANDER_PORT_B,
} LogicExpanderPort_T;

typedef enum LogicExpanderStatus_T
{
	LOGIC_EXPANDER_STATUS_OK,
	LOGIC_EXPANDER_STATUS_BUSY,
	LOGIC_EXPANDER_STATUS_ERROR,
	LOGIC_EXPANDER_STATUS_INVALID_PARAM,
	LOGIC_EXPANDER_STATUS_NOT_READY,
} LogicExpanderStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

LogicExpanderStatus_T expander_self_config( void );

LogicExpanderStatus_T expander_load_control_bit( uint8_t expander_index,
														 LogicExpanderPort_T port,
														 uint8_t bit_index,
														 bool bit_value );

LogicExpanderStatus_T expander_send_control_bits( void );

#ifdef __cplusplus
}
#endif

#endif /* LOGIC_EXPANDER_H */
