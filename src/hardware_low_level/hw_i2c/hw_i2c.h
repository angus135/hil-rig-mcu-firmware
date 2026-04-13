/******************************************************************************
 *  File:       hw_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    14-Apr-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_I2C_H
#define HW_I2C_H

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

#define HW_I2C_RX_BUFFER_SIZE 2048U
#define HW_I2C_TX_BUFFER_SIZE 512U

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
	HW_I2C_CHANNEL_1 = 0,
	HW_I2C_CHANNEL_2 = 1,
	HW_I2C_CHANNEL_INTERNAL_FMPI1 = 2,
} HwI2cChannel_T;

typedef enum
{
	HW_I2C_ROLE_MASTER = 0,
	HW_I2C_ROLE_SLAVE  = 1,
} HwI2cRole_T;

typedef enum
{
	HW_I2C_SPEED_100KHZ = 0,
	HW_I2C_SPEED_400KHZ = 1,
} HwI2cSpeed_T;

typedef enum
{
	HW_I2C_TRANSFER_INTERRUPT = 0,
	HW_I2C_TRANSFER_DMA       = 1,
} HwI2cTransferMode_T;

typedef struct
{
	HwI2cRole_T         role;
	HwI2cSpeed_T        speed;
	HwI2cTransferMode_T transfer_mode;
	uint8_t             own_address_7bit;
	bool                rx_enabled;
	bool                tx_enabled;
} HwI2cConfig_T;

typedef struct
{
	const uint8_t* data;
	uint32_t       length_bytes;
} HwI2cRxSpan_T;

typedef struct
{
	HwI2cRxSpan_T first_span;
	HwI2cRxSpan_T second_span;
	uint32_t      total_length_bytes;
} HwI2cRxSpans_T;
bool HW_I2C_Configure_Channel( HwI2cChannel_T channel, const HwI2cConfig_T* config );

bool HW_I2C_Configure_Internal_Channel( void );

bool HW_I2C_Rx_Start( HwI2cChannel_T channel, uint8_t target_address_7bit, uint16_t length_bytes );

HwI2cRxSpans_T HW_I2C_Rx_Peek( HwI2cChannel_T channel );

void HW_I2C_Rx_Consume( HwI2cChannel_T channel, uint32_t bytes_to_consume );

bool HW_I2C_Tx_Load_Buffer( HwI2cChannel_T channel, uint8_t target_address_7bit, const uint8_t* data,
							uint16_t length_bytes );

bool HW_I2C_Tx_Trigger( HwI2cChannel_T channel );

void HW_I2C1_EV_IRQHandler( void );
void HW_I2C2_EV_IRQHandler( void );
void HW_FMPI2C1_EV_IRQHandler( void );
void HW_I2C_DMA1_Stream0_IRQHandler( void );
void HW_I2C_DMA1_Stream2_IRQHandler( void );
void HW_I2C_DMA1_Stream6_IRQHandler( void );
void HW_I2C_DMA1_Stream7_IRQHandler( void );

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
