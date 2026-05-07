/******************************************************************************
 *  File:       hw_i2c_mocks.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
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

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t DR;
    volatile uint32_t SR1;
    volatile uint32_t SR2;
    volatile uint32_t CCR;
    volatile uint32_t TRISE;
} I2C_TypeDef;

typedef struct
{
    volatile uint32_t CR;
    volatile uint32_t NDTR;
    volatile uint32_t PAR;
    volatile uint32_t M0AR;
    volatile uint32_t M1AR;
    volatile uint32_t FCR;
} DMA_Stream_TypeDef;

typedef struct
{
    volatile uint32_t LISR;
    volatile uint32_t HISR;
    volatile uint32_t LIFCR;
    volatile uint32_t HIFCR;
} DMA_TypeDef;

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t TIMINGR;
    volatile uint32_t OAR1;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t RXDR;
    volatile uint32_t TXDR;
} FMPI2C_TypeDef;

static I2C_TypeDef        hw_i2c_mock_i2c1 = { 0 };
static I2C_TypeDef        hw_i2c_mock_i2c2 = { 0 };
static DMA_TypeDef        hw_i2c_mock_dma1 = { 0 };
static DMA_Stream_TypeDef hw_i2c_mock_dma1_stream2 = { 0 };
static DMA_Stream_TypeDef hw_i2c_mock_dma1_stream7 = { 0 };
static FMPI2C_TypeDef     hw_i2c_mock_fmpi2c1 = { 0 };

#define I2C1 ( &hw_i2c_mock_i2c1 )
#define I2C2 ( &hw_i2c_mock_i2c2 )
#define DMA1 ( &hw_i2c_mock_dma1 )
#define DMA1_Stream2 ( &hw_i2c_mock_dma1_stream2 )
#define DMA1_Stream7 ( &hw_i2c_mock_dma1_stream7 )
#define FMPI2C1 ( &hw_i2c_mock_fmpi2c1 )

#define LL_APB1_GRP1_PERIPH_I2C1 ( 0x00000001U )
#define LL_APB1_GRP1_PERIPH_I2C2 ( 0x00000002U )
#define LL_APB1_GRP1_PERIPH_FMPI2C1 ( 0x00000004U )

#define LL_APB1_GRP1_EnableClock( x ) ( ( void )( x ) )

#define I2C_CR1_PE ( 1U << 0 )
#define I2C_CR1_START ( 1U << 8 )
#define I2C_CR1_STOP ( 1U << 9 )
#define I2C_CR1_ACK ( 1U << 10 )

#define I2C_CR2_ITERREN ( 1U << 8 )
#define I2C_CR2_ITEVTEN ( 1U << 9 )
#define I2C_CR2_ITBUFEN ( 1U << 10 )
#define I2C_CR2_DMAEN ( 1U << 11 )
#define I2C_CR2_FREQ ( 0x3FU )

#define I2C_CCR_FS ( 1U << 15 )
#define I2C_CCR_CCR ( 0x0FFFU )

#define I2C_TRISE_TRISE ( 0x3FU )

#define I2C_OAR1_ADDMODE ( 1U << 15 )

#define I2C_SR1_SB ( 1U << 0 )
#define I2C_SR1_ADDR ( 1U << 1 )
#define I2C_SR1_BTF ( 1U << 2 )
#define I2C_SR1_STOPF ( 1U << 4 )
#define I2C_SR1_RXNE ( 1U << 6 )
#define I2C_SR1_TXE ( 1U << 7 )
#define I2C_SR1_BERR ( 1U << 8 )
#define I2C_SR1_ARLO ( 1U << 9 )
#define I2C_SR1_AF ( 1U << 10 )
#define I2C_SR1_OVR ( 1U << 11 )

#define DMA_SxCR_EN ( 1U << 0 )
#define DMA_SxCR_DIR_0 ( 1U << 6 )
#define DMA_SxCR_MINC ( 1U << 10 )
#define DMA_SxCR_TCIE ( 1U << 4 )
#define DMA_SxCR_TEIE ( 1U << 2 )
#define DMA_SxCR_CHSEL_Pos ( 25U )

#define DMA_LISR_TCIF2 ( 1U << 5 )
#define DMA_LISR_TEIF2 ( 1U << 3 )
#define DMA_HISR_TCIF7 ( 1U << 27 )
#define DMA_HISR_TEIF7 ( 1U << 25 )
#define DMA_LIFCR_CTCIF2 ( 1U << 5 )
#define DMA_LIFCR_CTEIF2 ( 1U << 3 )
#define DMA_LIFCR_CDMEIF2 ( 1U << 2 )
#define DMA_LIFCR_CFEIF2 ( 1U << 1 )
#define DMA_HIFCR_CTCIF7 ( 1U << 27 )
#define DMA_HIFCR_CTEIF7 ( 1U << 25 )
#define DMA_HIFCR_CDMEIF7 ( 1U << 24 )
#define DMA_HIFCR_CFEIF7 ( 1U << 22 )

#define FMPI2C_CR1_PE ( 1U << 0 )
#define FMPI2C_CR1_TXIE ( 1U << 1 )
#define FMPI2C_CR1_RXIE ( 1U << 2 )
#define FMPI2C_CR1_TCIE ( 1U << 3 )
#define FMPI2C_CR1_STOPIE ( 1U << 4 )
#define FMPI2C_CR1_ERRIE ( 1U << 5 )

#define FMPI2C_CR2_START ( 1U << 13 )
#define FMPI2C_CR2_STOP ( 1U << 14 )
#define FMPI2C_CR2_AUTOEND ( 1U << 12 )
#define FMPI2C_CR2_RD_WRN ( 1U << 10 )
#define FMPI2C_CR2_NBYTES_Pos ( 16U )

#define FMPI2C_OAR1_OA1EN ( 1U << 15 )

#define FMPI2C_ISR_TXIS ( 1U << 1 )
#define FMPI2C_ISR_RXNE ( 1U << 2 )
#define FMPI2C_ISR_STOPF ( 1U << 5 )
#define FMPI2C_ISR_TC ( 1U << 6 )
#define FMPI2C_ISR_NACKF ( 1U << 4 )
#define FMPI2C_ISR_BERR ( 1U << 8 )
#define FMPI2C_ISR_ARLO ( 1U << 9 )
#define FMPI2C_ISR_OVR ( 1U << 10 )
#define FMPI2C_ISR_TIMEOUT ( 1U << 12 )

#define FMPI2C_ICR_STOPCF ( 1U << 5 )
#define FMPI2C_ICR_NACKCF ( 1U << 4 )
#define FMPI2C_ICR_BERRCF ( 1U << 8 )
#define FMPI2C_ICR_ARLOCF ( 1U << 9 )
#define FMPI2C_ICR_OVRCF ( 1U << 10 )
#define FMPI2C_ICR_TIMOUTCF ( 1U << 12 )

#define LL_I2C_ACK ( I2C_CR1_ACK )
#define LL_I2C_NACK ( 0U )
#define LL_I2C_OWNADDRESS1_7BIT ( 0x00004000U )
#define LL_I2C_DUTYCYCLE_2 ( 0x00000000U )
#define LL_FMPI2C_OWNADDRESS1_7BIT ( 0x00000000U )

static inline void LL_I2C_Disable( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR1 &= ~I2C_CR1_PE;
}

static inline void LL_I2C_Enable( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR1 |= I2C_CR1_PE;
}

static inline void LL_I2C_SetPeriphClock( I2C_TypeDef* i2c_instance, uint32_t PeriphClock )
{
    i2c_instance->CR2 = ( i2c_instance->CR2 & ~I2C_CR2_FREQ ) | ( ( PeriphClock / 1000000U ) & I2C_CR2_FREQ );
}

static inline void LL_I2C_SetClockPeriod( I2C_TypeDef* i2c_instance, uint32_t ClockPeriod )
{
    i2c_instance->CCR = ( i2c_instance->CCR & ~I2C_CCR_CCR ) | ( ClockPeriod & I2C_CCR_CCR );
}

static inline void LL_I2C_SetRiseTime( I2C_TypeDef* i2c_instance, uint32_t RiseTime )
{
    i2c_instance->TRISE = RiseTime & I2C_TRISE_TRISE;
}

static inline void LL_I2C_SetOwnAddress1( I2C_TypeDef* i2c_instance, uint32_t OwnAddress1,
                                          uint32_t OwnAddrSize )
{
    i2c_instance->OAR1 = OwnAddress1 | OwnAddrSize;
}

static inline void LL_I2C_ConfigSpeed( I2C_TypeDef* i2c_instance, uint32_t PeriphClock,
                                       uint32_t ClockSpeed, uint32_t DutyCycle )
{
    const uint32_t pclk_mhz = PeriphClock / 1000000U;

    i2c_instance->CR2 = ( i2c_instance->CR2 & ~I2C_CR2_FREQ ) | ( pclk_mhz & I2C_CR2_FREQ );

    if ( ClockSpeed > 100000U )
    {
        uint32_t ccr_value = PeriphClock / ( ClockSpeed * 3U );
        if ( ccr_value < 1U )
        {
            ccr_value = 1U;
        }
        i2c_instance->CCR = I2C_CCR_FS | ( ccr_value & I2C_CCR_CCR ) | DutyCycle;
        i2c_instance->TRISE = ( ( pclk_mhz * 300U ) / 1000U + 1U ) & I2C_TRISE_TRISE;
    }
    else
    {
        uint32_t ccr_value = PeriphClock / ( ClockSpeed * 2U );
        if ( ccr_value < 4U )
        {
            ccr_value = 4U;
        }
        i2c_instance->CCR = ccr_value & I2C_CCR_CCR;
        i2c_instance->TRISE = ( pclk_mhz + 1U ) & I2C_TRISE_TRISE;
    }
}

static inline void LL_I2C_EnableIT_ERR( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= I2C_CR2_ITERREN;
}

static inline void LL_I2C_DisableIT_ERR( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_ITERREN;
}

static inline void LL_I2C_EnableIT_EVT( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= I2C_CR2_ITEVTEN;
}

static inline void LL_I2C_DisableIT_EVT( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_ITEVTEN;
}

static inline void LL_I2C_EnableIT_BUF( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= I2C_CR2_ITBUFEN;
}

static inline void LL_I2C_DisableIT_BUF( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_ITBUFEN;
}

static inline void LL_I2C_EnableIT_TX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= ( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void LL_I2C_DisableIT_TX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void LL_I2C_EnableIT_RX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= ( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void LL_I2C_DisableIT_RX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~( I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void LL_I2C_EnableDMAReq_TX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= I2C_CR2_DMAEN;
}

static inline void LL_I2C_DisableDMAReq_TX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_DMAEN;
}

static inline void LL_I2C_EnableDMAReq_RX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= I2C_CR2_DMAEN;
}

static inline void LL_I2C_DisableDMAReq_RX( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_DMAEN;
}

static inline void LL_I2C_AcknowledgeNextData( I2C_TypeDef* i2c_instance, uint32_t TypeAcknowledge )
{
    if ( TypeAcknowledge == LL_I2C_ACK )
    {
        i2c_instance->CR1 |= I2C_CR1_ACK;
    }
    else
    {
        i2c_instance->CR1 &= ~I2C_CR1_ACK;
    }
}

static inline void LL_I2C_GenerateStartCondition( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR1 |= I2C_CR1_START;
}

static inline void LL_I2C_GenerateStopCondition( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR1 |= I2C_CR1_STOP;
}

static inline void LL_I2C_ClearFlag_ADDR( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_ADDR;
    ( void )i2c_instance->SR2;
}

static inline void LL_I2C_ClearFlag_STOP( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_STOPF;
    i2c_instance->CR1 |= I2C_CR1_PE;
}

static inline void LL_I2C_ClearFlag_AF( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_AF;
}

static inline void LL_I2C_ClearFlag_BERR( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_BERR;
}

static inline void LL_I2C_ClearFlag_ARLO( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_ARLO;
}

static inline void LL_I2C_ClearFlag_OVR( I2C_TypeDef* i2c_instance )
{
    i2c_instance->SR1 &= ~I2C_SR1_OVR;
}

static inline void LL_FMPI2C_Enable( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->CR1 |= FMPI2C_CR1_PE;
}

static inline void LL_FMPI2C_Disable( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->CR1 &= ~FMPI2C_CR1_PE;
}

static inline void LL_FMPI2C_SetTiming( FMPI2C_TypeDef* fmpi2c_instance, uint32_t Timing )
{
    fmpi2c_instance->TIMINGR = Timing;
}

static inline void LL_FMPI2C_SetOwnAddress1( FMPI2C_TypeDef* fmpi2c_instance, uint32_t OwnAddress1,
                                             uint32_t OwnAddrSize )
{
    fmpi2c_instance->OAR1 = OwnAddress1 | OwnAddrSize;
}

static inline void LL_FMPI2C_EnableOwnAddress1( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->OAR1 |= FMPI2C_OAR1_OA1EN;
}

static inline void LL_FMPI2C_GenerateStartCondition( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->CR2 |= FMPI2C_CR2_START;
}

static inline void LL_FMPI2C_GenerateStopCondition( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->CR2 |= FMPI2C_CR2_STOP;
}

static inline void LL_FMPI2C_ClearFlag_STOP( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_STOPF;
    fmpi2c_instance->ICR |= FMPI2C_ICR_STOPCF;
}

static inline void LL_FMPI2C_ClearFlag_NACK( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_NACKF;
    fmpi2c_instance->ICR |= FMPI2C_ICR_NACKCF;
}

static inline void LL_FMPI2C_ClearFlag_BERR( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_BERR;
    fmpi2c_instance->ICR |= FMPI2C_ICR_BERRCF;
}

static inline void LL_FMPI2C_ClearFlag_ARLO( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_ARLO;
    fmpi2c_instance->ICR |= FMPI2C_ICR_ARLOCF;
}

static inline void LL_FMPI2C_ClearFlag_OVR( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_OVR;
    fmpi2c_instance->ICR |= FMPI2C_ICR_OVRCF;
}

static inline void LL_FMPI2C_ClearSMBusFlag_TIMEOUT( FMPI2C_TypeDef* fmpi2c_instance )
{
    fmpi2c_instance->ISR &= ~FMPI2C_ISR_TIMEOUT;
    fmpi2c_instance->ICR |= FMPI2C_ICR_TIMOUTCF;
}

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
