/******************************************************************************
 *  File:       hw_spi.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_SPI_H
#define HW_SPI_H

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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum SPIBaudRate_T
{
    SPI_BAUD_45MBIT,
    SPI_BAUD_22M5BIT,
    SPI_BAUD_11M25BIT,
    SPI_BAUD_5M625BIT,
    SPI_BAUD_2M813BIT,
    SPI_BAUD_1M406BIT,
    SPI_BAUD_703KBIT,
    SPI_BAUD_352KBIT,
} SPIBaudRate_T;

typedef enum SPICPOL_T
{
    SPI_CPOL_LOW,
    SPI_CPOL_HIGH,
} SPICPOL_T;

typedef enum SPICPHA_T
{
    SPI_CPHA_1_EDGE,
    SPI_CPHA_2_EDGE,
} SPICPHA_T;

typedef enum SPIDataSize_T
{
    SPI_SIZE_8_BIT,
    SPI_SIZE_16_BIT,
} SPIDataSize_T;

typedef enum SPIFirstBit_T
{
    SPI_FIRST_MSB,
    SPI_FIRST_LSB,
} SPIFirstBit_T;

typedef enum SPIMode_T
{
    SPI_MODE_MASTER,
    SPI_MODE_SLAVE,
} SPIMode_T;

typedef enum SPIPeripheral_T
{
    SPI_CHANNEL_0,
    SPI_CHANNEL_1,
    SPI_DAC,
} SPIPeripheral_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* HW_SPI_H */
