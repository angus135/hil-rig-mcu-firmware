/******************************************************************************
 *  File:       hw_i2c_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_i2c module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_I2C_MOCKS_H
#define HW_I2C_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define I2C1    ( ( void* )0x40005400u )
#define I2C2    ( ( void* )0x40005800u )
#define FMPI2C1 ( ( void* )0x40006000u )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
	void* Instance;
} I2C_HandleTypeDef;

typedef struct
{
	void* Instance;
} FMPI2C_HandleTypeDef;

typedef enum
{
	HAL_OK = 0,
	HAL_ERROR,
	HAL_BUSY,
	HAL_TIMEOUT
} HAL_StatusTypeDef;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_I2C_MOCKS_H */
