/******************************************************************************
 *  File:       hw_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    14-Apr-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_i2c.h"

#ifdef TEST_BUILD
#include "hw_i2c_mocks.h"
#else
#include "stm32f446xx.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_I2C_EXTERNAL_CHANNEL_COUNT 2U
#define HW_I2C_CHANNEL_COUNT          3U

#ifdef TEST_BUILD
#define HW_I2C_MAYBE_UNUSED __attribute__( ( unused ) )
#else
#define HW_I2C_MAYBE_UNUSED
#endif

#if ( ( HW_I2C_RX_BUFFER_SIZE & ( HW_I2C_RX_BUFFER_SIZE - 1U ) ) != 0U )
#error "HW_I2C_RX_BUFFER_SIZE must be a power of 2"
#endif

#ifndef TEST_BUILD
#define HW_I2C_APB1_CLOCK_HZ 45000000U
#define HW_I2C_APB1_FREQ_MHZ 45U

#define HW_I2C1_RX_DMA_CHANNEL 1U
#define HW_I2C1_TX_DMA_CHANNEL 1U
#define HW_I2C2_RX_DMA_CHANNEL 7U
#define HW_I2C2_TX_DMA_CHANNEL 7U
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
	HwI2cConfig_T config;

	uint32_t rx_read_index;
	uint32_t rx_write_index;

	uint16_t rx_expected_length;
	uint16_t rx_dma_pending_length;
	uint16_t tx_length_bytes;
	uint16_t tx_index;

	uint8_t target_address_7bit;

	bool configured;
	bool rx_running;
	bool tx_loaded;
	bool tx_running;
	bool rx_uses_dma_circular;
	bool tx_uses_dma;
} HwI2cRuntimeState_T;

typedef struct
{
	HwI2cRuntimeState_T runtime;
	uint8_t             rx_buffer[HW_I2C_RX_BUFFER_SIZE];
	uint8_t             tx_buffer[HW_I2C_TX_BUFFER_SIZE];
} HwI2cChannelState_T;

#ifndef TEST_BUILD
typedef struct
{
	I2C_TypeDef*        i2c_instance;
	DMA_Stream_TypeDef* rx_dma_stream;
	DMA_Stream_TypeDef* tx_dma_stream;
	uint32_t            rx_dma_channel;
	uint32_t            tx_dma_channel;
} HwI2cExternalHardwareMap_T;
#endif

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HwI2cChannelState_T i2c_channel_states[HW_I2C_CHANNEL_COUNT];

#ifndef TEST_BUILD
static const HwI2cExternalHardwareMap_T i2c_external_hardware_map[HW_I2C_EXTERNAL_CHANNEL_COUNT] = {
	[HW_I2C_CHANNEL_1] = { .i2c_instance   = I2C1,
						   .rx_dma_stream  = DMA1_Stream0,
						   .tx_dma_stream  = DMA1_Stream6,
						   .rx_dma_channel = HW_I2C1_RX_DMA_CHANNEL,
						   .tx_dma_channel = HW_I2C1_TX_DMA_CHANNEL },
	[HW_I2C_CHANNEL_2] = { .i2c_instance   = I2C2,
						   .rx_dma_stream  = DMA1_Stream2,
						   .tx_dma_stream  = DMA1_Stream7,
						   .rx_dma_channel = HW_I2C2_RX_DMA_CHANNEL,
						   .tx_dma_channel = HW_I2C2_TX_DMA_CHANNEL } };
#endif

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline bool HW_I2C_Channel_Is_Valid( HwI2cChannel_T channel )
{
	return ( channel == HW_I2C_CHANNEL_1 || channel == HW_I2C_CHANNEL_2
			 || channel == HW_I2C_CHANNEL_INTERNAL_FMPI1 );
}

static inline HW_I2C_MAYBE_UNUSED bool HW_I2C_Channel_Is_External( HwI2cChannel_T channel )
{
	return ( channel == HW_I2C_CHANNEL_1 || channel == HW_I2C_CHANNEL_2 );
}

static inline uint32_t HW_I2C_Unread_Count( uint32_t read_index, uint32_t write_index )
{
	return ( write_index - read_index ) & ( HW_I2C_RX_BUFFER_SIZE - 1U );
}

static inline uint32_t HW_I2C_Advance_Index( uint32_t index, uint32_t advance_by )
{
	return ( index + advance_by ) & ( HW_I2C_RX_BUFFER_SIZE - 1U );
}

static inline HW_I2C_MAYBE_UNUSED void HW_I2C_Rx_Push_Byte( HwI2cChannel_T channel, uint8_t value )
{
	HwI2cChannelState_T* state = &i2c_channel_states[channel];
	const uint32_t next_write = HW_I2C_Advance_Index( state->runtime.rx_write_index, 1U );

	if ( next_write == state->runtime.rx_read_index )
	{
		return;
	}

	state->rx_buffer[state->runtime.rx_write_index] = value;
	state->runtime.rx_write_index                   = next_write;
}

static bool HW_I2C_Configuration_Is_Valid( HwI2cChannel_T channel, const HwI2cConfig_T* config )
{
	if ( !HW_I2C_Channel_Is_Valid( channel ) || config == NULL )
	{
		return false;
	}

	if ( config->role != HW_I2C_ROLE_MASTER && config->role != HW_I2C_ROLE_SLAVE )
	{
		return false;
	}

	if ( config->speed != HW_I2C_SPEED_100KHZ && config->speed != HW_I2C_SPEED_400KHZ )
	{
		return false;
	}

	if ( config->transfer_mode != HW_I2C_TRANSFER_INTERRUPT
		 && config->transfer_mode != HW_I2C_TRANSFER_DMA )
	{
		return false;
	}

	if ( !config->rx_enabled && !config->tx_enabled )
	{
		return false;
	}

	if ( config->own_address_7bit > 0x7FU )
	{
		return false;
	}

	if ( channel == HW_I2C_CHANNEL_INTERNAL_FMPI1 )
	{
		return ( config->role == HW_I2C_ROLE_MASTER && config->speed == HW_I2C_SPEED_100KHZ
				 && config->transfer_mode == HW_I2C_TRANSFER_INTERRUPT );
	}

	return true;
}

#ifndef TEST_BUILD
static inline uint32_t HW_I2C_DMA_Channel_Shifted( uint32_t channel )
{
	return ( channel << DMA_SxCR_CHSEL_Pos );
}

static inline void HW_I2C_DMA_Clear_Stream0_Flags( void )
{
	DMA1->LIFCR = DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CTEIF0 | DMA_LIFCR_CHTIF0
				  | DMA_LIFCR_CTCIF0;
}

static inline void HW_I2C_DMA_Clear_Stream2_Flags( void )
{
	DMA1->LIFCR = DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CHTIF2
				  | DMA_LIFCR_CTCIF2;
}

static inline void HW_I2C_DMA_Clear_Stream6_Flags( void )
{
	DMA1->HIFCR = DMA_HIFCR_CFEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CHTIF6
				  | DMA_HIFCR_CTCIF6;
}

static inline void HW_I2C_DMA_Clear_Stream7_Flags( void )
{
	DMA1->HIFCR = DMA_HIFCR_CFEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CHTIF7
				  | DMA_HIFCR_CTCIF7;
}

static inline void HW_I2C_DMA_Stream_Disable( DMA_Stream_TypeDef* stream )
{
	stream->CR &= ~DMA_SxCR_EN;
	while ( ( stream->CR & DMA_SxCR_EN ) != 0U )
	{
	}
}

static void HW_I2C_DMA_Config_Periph_To_Memory( DMA_Stream_TypeDef* stream, uint32_t channel,
												 volatile uint32_t* peripheral_address,
												 uint8_t* memory_address, uint16_t length,
												 bool circular )
{
	HW_I2C_DMA_Stream_Disable( stream );

	stream->CR = 0U;
	stream->NDTR = (uint32_t)length;
	stream->PAR  = (uint32_t)peripheral_address;
	stream->M0AR = (uint32_t)memory_address;
	stream->FCR  = 0U;

	stream->CR = HW_I2C_DMA_Channel_Shifted( channel ) | DMA_SxCR_MINC | DMA_SxCR_TCIE
				 | ( circular ? DMA_SxCR_CIRC : 0U ) | DMA_SxCR_PL_1;

	stream->CR |= DMA_SxCR_EN;
}

static void HW_I2C_DMA_Config_Memory_To_Periph( DMA_Stream_TypeDef* stream, uint32_t channel,
												 volatile uint32_t* peripheral_address,
												 const uint8_t* memory_address, uint16_t length )
{
	HW_I2C_DMA_Stream_Disable( stream );

	stream->CR = 0U;
	stream->NDTR = (uint32_t)length;
	stream->PAR  = (uint32_t)peripheral_address;
	stream->M0AR = (uint32_t)memory_address;
	stream->FCR  = 0U;

	stream->CR = HW_I2C_DMA_Channel_Shifted( channel ) | DMA_SxCR_DIR_0 | DMA_SxCR_MINC
				 | DMA_SxCR_TCIE | DMA_SxCR_PL_1;

	stream->CR |= DMA_SxCR_EN;
}

static void HW_I2C_DMA_Transfer_Complete( HwI2cChannel_T channel, bool is_rx )
{
	HwI2cRuntimeState_T* state = &i2c_channel_states[channel].runtime;
	I2C_TypeDef* i2c           = i2c_external_hardware_map[channel].i2c_instance;

	if ( is_rx )
	{
		if ( !state->rx_running )
		{
			return;
		}

		if ( state->rx_uses_dma_circular )
		{
			state->rx_write_index = HW_I2C_DMA_Circular_Write_Index( channel );
			return;
		}

		if ( state->rx_dma_pending_length > 0U )
		{
			state->rx_write_index = HW_I2C_Advance_Index( state->rx_write_index,
												  state->rx_dma_pending_length );
			state->rx_dma_pending_length = 0U;
		}

		if ( state->config.role == HW_I2C_ROLE_MASTER )
		{
			HW_I2C_Master_Generate_Stop( i2c );
		}

		state->rx_running = false;
		i2c->CR2 &= ~( I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
		i2c->CR1 |= I2C_CR1_ACK;
		return;
	}

	if ( !state->tx_running )
	{
		return;
	}

	state->tx_index = state->tx_length_bytes;

	if ( state->config.role == HW_I2C_ROLE_MASTER )
	{
		HW_I2C_Master_Generate_Stop( i2c );
	}

	state->tx_running = false;
	state->tx_loaded  = false;
	state->tx_uses_dma = false;
	i2c->CR2 &= ~( I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
}

static void HW_I2C_Configure_Timing( I2C_TypeDef* i2c, HwI2cSpeed_T speed )
{
	i2c->CR1 &= ~I2C_CR1_PE;

	i2c->CR2 = ( i2c->CR2 & ~I2C_CR2_FREQ ) | HW_I2C_APB1_FREQ_MHZ;

	if ( speed == HW_I2C_SPEED_100KHZ )
	{
		uint32_t ccr = HW_I2C_APB1_CLOCK_HZ / ( 2U * 100000U );
		if ( ccr < 4U )
		{
			ccr = 4U;
		}

		i2c->CCR   = ( ccr & I2C_CCR_CCR );
		i2c->TRISE = HW_I2C_APB1_FREQ_MHZ + 1U;
	}
	else
	{
		uint32_t ccr = HW_I2C_APB1_CLOCK_HZ / ( 3U * 400000U );
		if ( ccr < 1U )
		{
			ccr = 1U;
		}

		i2c->CCR   = I2C_CCR_FS | ( ccr & I2C_CCR_CCR );
		i2c->TRISE = ( ( HW_I2C_APB1_FREQ_MHZ * 300U ) / 1000U ) + 1U;
	}

	i2c->CR1 |= I2C_CR1_PE;
}

static void HW_I2C_Apply_Config_External( HwI2cChannel_T channel )
{
	I2C_TypeDef* i2c                   = i2c_external_hardware_map[channel].i2c_instance;
	const HwI2cConfig_T* config        = &i2c_channel_states[channel].runtime.config;
	HwI2cRuntimeState_T* runtime_state = &i2c_channel_states[channel].runtime;

	HW_I2C_Configure_Timing( i2c, config->speed );

	i2c->OAR1 = ( 1U << 14 ) | ( (uint32_t)config->own_address_7bit << 1U );

	if ( config->rx_enabled )
	{
		i2c->CR1 |= I2C_CR1_ACK;
	}
	else
	{
		i2c->CR1 &= ~I2C_CR1_ACK;
	}

	i2c->CR2 &= ~( I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );

	runtime_state->rx_uses_dma_circular = false;
	runtime_state->tx_uses_dma          = false;
}

static inline uint32_t HW_I2C_DMA_Circular_Write_Index( HwI2cChannel_T channel )
{
	const DMA_Stream_TypeDef* stream = i2c_external_hardware_map[channel].rx_dma_stream;
	return ( HW_I2C_RX_BUFFER_SIZE - stream->NDTR ) & ( HW_I2C_RX_BUFFER_SIZE - 1U );
}

static inline void HW_I2C_Master_Generate_Start( I2C_TypeDef* i2c )
{
	i2c->CR1 |= I2C_CR1_START;
}

static inline void HW_I2C_Master_Generate_Stop( I2C_TypeDef* i2c )
{
	i2c->CR1 |= I2C_CR1_STOP;
}

static void HW_I2C_External_Event_IRQHandler( HwI2cChannel_T channel )
{
	I2C_TypeDef* i2c            = i2c_external_hardware_map[channel].i2c_instance;
	HwI2cRuntimeState_T* state  = &i2c_channel_states[channel].runtime;
	const bool use_dma_transfer = ( state->config.transfer_mode == HW_I2C_TRANSFER_DMA );

	const uint32_t sr1 = i2c->SR1;

	if ( ( sr1 & I2C_SR1_SB ) != 0U )
	{
		if ( state->tx_running )
		{
			i2c->DR = (uint8_t)( state->target_address_7bit << 1U );
		}
		else if ( state->rx_running )
		{
			i2c->DR = (uint8_t)( ( state->target_address_7bit << 1U ) | 0x01U );
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_ADDR ) != 0U )
	{
		volatile uint32_t clear_sequence = i2c->SR1;
		clear_sequence                    = i2c->SR2;
		(void)clear_sequence;

		if ( state->rx_running && state->config.role == HW_I2C_ROLE_MASTER
			 && state->rx_expected_length == 1U )
		{
			i2c->CR1 &= ~I2C_CR1_ACK;
			HW_I2C_Master_Generate_Stop( i2c );
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_RXNE ) != 0U && !use_dma_transfer )
	{
		const uint8_t value = (uint8_t)i2c->DR;
		HW_I2C_Rx_Push_Byte( channel, value );

		if ( state->config.role == HW_I2C_ROLE_MASTER && state->rx_expected_length > 0U )
		{
			state->rx_expected_length--;

			if ( state->rx_expected_length == 1U )
			{
				i2c->CR1 &= ~I2C_CR1_ACK;
				HW_I2C_Master_Generate_Stop( i2c );
			}
			else if ( state->rx_expected_length == 0U )
			{
				state->rx_running = false;
				i2c->CR2 &= ~( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
				i2c->CR1 |= I2C_CR1_ACK;
			}
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_TXE ) != 0U && state->tx_running && !use_dma_transfer )
	{
		if ( state->tx_index < state->tx_length_bytes )
		{
			i2c->DR = i2c_channel_states[channel].tx_buffer[state->tx_index];
			state->tx_index++;
		}
		else
		{
			if ( state->config.role == HW_I2C_ROLE_MASTER )
			{
				HW_I2C_Master_Generate_Stop( i2c );
				state->tx_running = false;
				state->tx_loaded  = false;
				state->tx_index   = 0U;
				i2c->CR2 &= ~( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
			}
			else
			{
				i2c->DR = 0xFFU;
			}
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_BTF ) != 0U )
	{
		if ( state->tx_running && use_dma_transfer )
		{
			const DMA_Stream_TypeDef* stream = i2c_external_hardware_map[channel].tx_dma_stream;
			if ( stream->NDTR == 0U )
			{
				HW_I2C_Master_Generate_Stop( i2c );
				state->tx_running = false;
				state->tx_loaded  = false;
				state->tx_uses_dma = false;
				i2c->CR2 &= ~( I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
			}
		}

		if ( state->rx_running && use_dma_transfer && state->config.role == HW_I2C_ROLE_MASTER )
		{
			if ( state->rx_dma_pending_length > 0U )
			{
				state->rx_write_index = HW_I2C_Advance_Index( state->rx_write_index,
															  state->rx_dma_pending_length );
				state->rx_dma_pending_length = 0U;
			}

			HW_I2C_Master_Generate_Stop( i2c );
			state->rx_running = false;
			i2c->CR2 &= ~( I2C_CR2_DMAEN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN );
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_STOPF ) != 0U )
	{
		volatile uint32_t clear_sequence = i2c->SR1;
		(void)clear_sequence;
		i2c->CR1 |= 0U;

		if ( state->tx_running && state->config.role == HW_I2C_ROLE_SLAVE )
		{
			state->tx_running = false;
			state->tx_loaded  = false;
			state->tx_index   = 0U;
		}

		if ( state->rx_uses_dma_circular )
		{
			state->rx_write_index = HW_I2C_DMA_Circular_Write_Index( channel );
		}

		return;
	}

	if ( ( sr1 & I2C_SR1_AF ) != 0U )
	{
		i2c->SR1 &= ~I2C_SR1_AF;

		if ( state->tx_running && state->config.role == HW_I2C_ROLE_MASTER )
		{
			HW_I2C_Master_Generate_Stop( i2c );
			state->tx_running = false;
			state->tx_loaded  = false;
			state->tx_index   = 0U;
		}
	}
}

static void HW_FMPI2C1_Event_IRQHandler_Internal( void )
{
	HwI2cRuntimeState_T* state = &i2c_channel_states[HW_I2C_CHANNEL_INTERNAL_FMPI1].runtime;
	const uint32_t isr         = FMPI2C1->ISR;

	if ( ( isr & FMPI2C_ISR_TXIS ) != 0U && state->tx_running )
	{
		if ( state->tx_index < state->tx_length_bytes )
		{
			FMPI2C1->TXDR = i2c_channel_states[HW_I2C_CHANNEL_INTERNAL_FMPI1].tx_buffer[state->tx_index];
			state->tx_index++;
		}
		else
		{
			FMPI2C1->TXDR = 0xFFU;
		}
	}

	if ( ( isr & FMPI2C_ISR_RXNE ) != 0U && state->rx_running )
	{
		HW_I2C_Rx_Push_Byte( HW_I2C_CHANNEL_INTERNAL_FMPI1, (uint8_t)FMPI2C1->RXDR );
	}

	if ( ( isr & FMPI2C_ISR_STOPF ) != 0U )
	{
		FMPI2C1->ICR = FMPI2C_ICR_STOPCF;
		state->rx_running = false;
		state->tx_running = false;
		state->tx_loaded  = false;
		state->tx_index   = 0U;
	}

	if ( ( isr & FMPI2C_ISR_NACKF ) != 0U )
	{
		FMPI2C1->ICR = FMPI2C_ICR_NACKCF;
		state->rx_running = false;
		state->tx_running = false;
		state->tx_loaded  = false;
		state->tx_index   = 0U;
	}
}
#endif

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HW_I2C_Configure_Channel( HwI2cChannel_T channel, const HwI2cConfig_T* config )
{
	if ( !HW_I2C_Configuration_Is_Valid( channel, config ) )
	{
		return false;
	}

	HwI2cChannelState_T* state = &i2c_channel_states[channel];

	state->runtime.config               = *config;
	state->runtime.rx_read_index        = 0U;
	state->runtime.rx_write_index       = 0U;
	state->runtime.rx_expected_length   = 0U;
	state->runtime.rx_dma_pending_length = 0U;
	state->runtime.tx_length_bytes      = 0U;
	state->runtime.tx_index             = 0U;
	state->runtime.target_address_7bit  = 0U;
	state->runtime.rx_running           = false;
	state->runtime.tx_loaded            = false;
	state->runtime.tx_running           = false;
	state->runtime.rx_uses_dma_circular = false;
	state->runtime.tx_uses_dma          = false;
	state->runtime.configured           = true;

#ifndef TEST_BUILD
	if ( HW_I2C_Channel_Is_External( channel ) )
	{
		HW_I2C_Apply_Config_External( channel );
	}
#endif

	return true;
}

bool HW_I2C_Configure_Internal_Channel( void )
{
	const HwI2cConfig_T fixed_internal_config = { .role            = HW_I2C_ROLE_MASTER,
												  .speed           = HW_I2C_SPEED_100KHZ,
												  .transfer_mode   = HW_I2C_TRANSFER_INTERRUPT,
												  .own_address_7bit = 0U,
												  .rx_enabled      = true,
												  .tx_enabled      = true };

	if ( !HW_I2C_Configure_Channel( HW_I2C_CHANNEL_INTERNAL_FMPI1, &fixed_internal_config ) )
	{
		return false;
	}

#ifndef TEST_BUILD
	FMPI2C1->CR1 &= ~FMPI2C_CR1_PE;
	FMPI2C1->TIMINGR = 0xC0000E12U;
	FMPI2C1->CR1 = FMPI2C_CR1_PE;
#endif

	return true;
}

bool HW_I2C_Rx_Start( HwI2cChannel_T channel, uint8_t target_address_7bit, uint16_t length_bytes )
{
	if ( !HW_I2C_Channel_Is_Valid( channel ) )
	{
		return false;
	}

	HwI2cRuntimeState_T* runtime = &i2c_channel_states[channel].runtime;

	if ( !runtime->configured || !runtime->config.rx_enabled )
	{
		return false;
	}

	if ( runtime->config.role == HW_I2C_ROLE_MASTER && ( target_address_7bit > 0x7FU || length_bytes == 0U ) )
	{
		return false;
	}

	runtime->target_address_7bit = target_address_7bit;
	runtime->rx_expected_length  = length_bytes;
	runtime->rx_running          = true;

#ifdef TEST_BUILD
	return true;
#else
	if ( channel == HW_I2C_CHANNEL_INTERNAL_FMPI1 )
	{
		FMPI2C1->CR1 |= FMPI2C_CR1_PE;

		if ( runtime->config.role == HW_I2C_ROLE_MASTER )
		{
			FMPI2C1->CR2 = ( (uint32_t)target_address_7bit << 1U )
						   | ( (uint32_t)length_bytes << FMPI2C_CR2_NBYTES_Pos )
						   | FMPI2C_CR2_RD_WRN | FMPI2C_CR2_AUTOEND | FMPI2C_CR2_START;

			FMPI2C1->CR1 |= FMPI2C_CR1_RXIE | FMPI2C_CR1_STOPIE | FMPI2C_CR1_NACKIE;
		}

		return true;
	}

	I2C_TypeDef* i2c = i2c_external_hardware_map[channel].i2c_instance;

	if ( runtime->config.role == HW_I2C_ROLE_SLAVE )
	{
		runtime->rx_read_index  = 0U;
		runtime->rx_write_index = 0U;

		if ( runtime->config.transfer_mode == HW_I2C_TRANSFER_DMA )
		{
			HW_I2C_DMA_Config_Periph_To_Memory( i2c_external_hardware_map[channel].rx_dma_stream,
												i2c_external_hardware_map[channel].rx_dma_channel,
												&i2c->DR, i2c_channel_states[channel].rx_buffer,
												(uint16_t)HW_I2C_RX_BUFFER_SIZE, true );

			runtime->rx_uses_dma_circular = true;
			i2c->CR2 |= I2C_CR2_DMAEN;
		}
		else
		{
			runtime->rx_uses_dma_circular = false;
			i2c->CR2 &= ~I2C_CR2_DMAEN;
		}

		i2c->CR1 |= I2C_CR1_ACK;
		i2c->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN;
		return true;
	}

	i2c->CR1 |= I2C_CR1_ACK;

	if ( runtime->config.transfer_mode == HW_I2C_TRANSFER_DMA )
	{
		const uint32_t write_index = runtime->rx_write_index;
		const uint32_t contiguous_space = HW_I2C_RX_BUFFER_SIZE - write_index;
		if ( length_bytes > contiguous_space )
		{
			return false;
		}

		HW_I2C_DMA_Config_Periph_To_Memory( i2c_external_hardware_map[channel].rx_dma_stream,
											i2c_external_hardware_map[channel].rx_dma_channel,
											&i2c->DR,
											&i2c_channel_states[channel].rx_buffer[write_index],
											length_bytes, false );

		runtime->rx_dma_pending_length = length_bytes;
		i2c->CR2 |= I2C_CR2_DMAEN;
	}
	else
	{
		i2c->CR2 &= ~I2C_CR2_DMAEN;
	}

	i2c->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN;
	HW_I2C_Master_Generate_Start( i2c );
	return true;
#endif
}

HwI2cRxSpans_T HW_I2C_Rx_Peek( HwI2cChannel_T channel )
{
	HwI2cRxSpans_T empty = {0};

	if ( !HW_I2C_Channel_Is_Valid( channel ) )
	{
		return empty;
	}

	HwI2cChannelState_T* state = &i2c_channel_states[channel];

#ifndef TEST_BUILD
	if ( HW_I2C_Channel_Is_External( channel ) && state->runtime.rx_uses_dma_circular )
	{
		state->runtime.rx_write_index = HW_I2C_DMA_Circular_Write_Index( channel );
	}
#endif

	const uint32_t read_index  = state->runtime.rx_read_index;
	const uint32_t write_index = state->runtime.rx_write_index;
	const uint32_t unread      = HW_I2C_Unread_Count( read_index, write_index );

	if ( unread == 0U )
	{
		return empty;
	}

	const uint32_t contiguous = HW_I2C_RX_BUFFER_SIZE - read_index;

	if ( unread <= contiguous )
	{
		empty.first_span.data         = &state->rx_buffer[read_index];
		empty.first_span.length_bytes = unread;
	}
	else
	{
		empty.first_span.data          = &state->rx_buffer[read_index];
		empty.first_span.length_bytes  = contiguous;
		empty.second_span.data         = &state->rx_buffer[0];
		empty.second_span.length_bytes = unread - contiguous;
	}

	empty.total_length_bytes = unread;

	return empty;
}

void HW_I2C_Rx_Consume( HwI2cChannel_T channel, uint32_t bytes_to_consume )
{
	if ( !HW_I2C_Channel_Is_Valid( channel ) || bytes_to_consume == 0U )
	{
		return;
	}

	HwI2cChannelState_T* state = &i2c_channel_states[channel];

#ifndef TEST_BUILD
	if ( HW_I2C_Channel_Is_External( channel ) && state->runtime.rx_uses_dma_circular )
	{
		state->runtime.rx_write_index = HW_I2C_DMA_Circular_Write_Index( channel );
	}
#endif

	const uint32_t unread = HW_I2C_Unread_Count( state->runtime.rx_read_index,
												 state->runtime.rx_write_index );
	if ( bytes_to_consume > unread )
	{
		bytes_to_consume = unread;
	}

	state->runtime.rx_read_index = HW_I2C_Advance_Index( state->runtime.rx_read_index,
														 bytes_to_consume );
}

bool HW_I2C_Tx_Load_Buffer( HwI2cChannel_T channel, uint8_t target_address_7bit, const uint8_t* data,
							uint16_t length_bytes )
{
	if ( !HW_I2C_Channel_Is_Valid( channel ) || data == NULL || length_bytes == 0U
		 || length_bytes > HW_I2C_TX_BUFFER_SIZE || target_address_7bit > 0x7FU )
	{
		return false;
	}

	HwI2cChannelState_T* state = &i2c_channel_states[channel];

	if ( !state->runtime.configured || !state->runtime.config.tx_enabled || state->runtime.tx_running
		 || state->runtime.tx_loaded )
	{
		return false;
	}

	memcpy( state->tx_buffer, data, length_bytes );

	state->runtime.target_address_7bit = target_address_7bit;
	state->runtime.tx_length_bytes     = length_bytes;
	state->runtime.tx_index            = 0U;
	state->runtime.tx_loaded           = true;

	return true;
}

bool HW_I2C_Tx_Trigger( HwI2cChannel_T channel )
{
	if ( !HW_I2C_Channel_Is_Valid( channel ) )
	{
		return false;
	}

	HwI2cRuntimeState_T* runtime = &i2c_channel_states[channel].runtime;

	if ( !runtime->configured || !runtime->tx_loaded || runtime->tx_running || !runtime->config.tx_enabled )
	{
		return false;
	}

	runtime->tx_running = true;
	runtime->tx_index   = 0U;

#ifdef TEST_BUILD
	return true;
#else
	if ( channel == HW_I2C_CHANNEL_INTERNAL_FMPI1 )
	{
		FMPI2C1->CR1 |= FMPI2C_CR1_PE;
		FMPI2C1->CR2 = ( (uint32_t)runtime->target_address_7bit << 1U )
					   | ( (uint32_t)runtime->tx_length_bytes << FMPI2C_CR2_NBYTES_Pos )
					   | FMPI2C_CR2_AUTOEND | FMPI2C_CR2_START;

		FMPI2C1->CR1 |= FMPI2C_CR1_TXIE | FMPI2C_CR1_STOPIE | FMPI2C_CR1_NACKIE;
		return true;
	}

	I2C_TypeDef* i2c = i2c_external_hardware_map[channel].i2c_instance;

	if ( runtime->config.transfer_mode == HW_I2C_TRANSFER_DMA )
	{
		HW_I2C_DMA_Config_Memory_To_Periph( i2c_external_hardware_map[channel].tx_dma_stream,
											i2c_external_hardware_map[channel].tx_dma_channel,
											&i2c->DR,
											i2c_channel_states[channel].tx_buffer,
											runtime->tx_length_bytes );

		runtime->tx_uses_dma = true;
		i2c->CR2 |= I2C_CR2_DMAEN;
	}
	else
	{
		runtime->tx_uses_dma = false;
		i2c->CR2 &= ~I2C_CR2_DMAEN;
	}

	i2c->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN;

	if ( runtime->config.role == HW_I2C_ROLE_MASTER )
	{
		HW_I2C_Master_Generate_Start( i2c );
	}

	return true;
#endif
}

void HW_I2C1_EV_IRQHandler( void )
{
#ifndef TEST_BUILD
	HW_I2C_External_Event_IRQHandler( HW_I2C_CHANNEL_1 );
#endif
}

void HW_I2C2_EV_IRQHandler( void )
{
#ifndef TEST_BUILD
	HW_I2C_External_Event_IRQHandler( HW_I2C_CHANNEL_2 );
#endif
}

void HW_FMPI2C1_EV_IRQHandler( void )
{
#ifndef TEST_BUILD
	HW_FMPI2C1_Event_IRQHandler_Internal();
#endif
}

void HW_I2C_DMA1_Stream0_IRQHandler( void )
{
#ifndef TEST_BUILD
	const uint32_t lisr = DMA1->LISR;

	if ( ( lisr & DMA_LISR_TCIF0 ) != 0U )
	{
		HW_I2C_DMA_Transfer_Complete( HW_I2C_CHANNEL_1, true );
	}

	HW_I2C_DMA_Clear_Stream0_Flags();
#endif
}

void HW_I2C_DMA1_Stream2_IRQHandler( void )
{
#ifndef TEST_BUILD
	const uint32_t lisr = DMA1->LISR;

	if ( ( lisr & DMA_LISR_TCIF2 ) != 0U )
	{
		HW_I2C_DMA_Transfer_Complete( HW_I2C_CHANNEL_2, true );
	}

	HW_I2C_DMA_Clear_Stream2_Flags();
#endif
}

void HW_I2C_DMA1_Stream6_IRQHandler( void )
{
#ifndef TEST_BUILD
	const uint32_t hisr = DMA1->HISR;

	if ( ( hisr & DMA_HISR_TCIF6 ) != 0U )
	{
		HW_I2C_DMA_Transfer_Complete( HW_I2C_CHANNEL_1, false );
	}

	HW_I2C_DMA_Clear_Stream6_Flags();
#endif
}

void HW_I2C_DMA1_Stream7_IRQHandler( void )
{
#ifndef TEST_BUILD
	const uint32_t hisr = DMA1->HISR;

	if ( ( hisr & DMA_HISR_TCIF7 ) != 0U )
	{
		HW_I2C_DMA_Transfer_Complete( HW_I2C_CHANNEL_2, false );
	}

	HW_I2C_DMA_Clear_Stream7_Flags();
#endif
}
