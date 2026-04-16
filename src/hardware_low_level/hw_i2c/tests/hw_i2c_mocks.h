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

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#ifndef BIT_U32
#define BIT_U32( n ) ( (uint32_t)1U << ( n ) )
#endif

/* I2C register bit definitions used by hw_i2c.c */
#define I2C_CR1_PE    BIT_U32( 0 )
#define I2C_CR1_START BIT_U32( 8 )
#define I2C_CR1_STOP  BIT_U32( 9 )
#define I2C_CR1_ACK   BIT_U32( 10 )

#define I2C_CR2_FREQ    UINT32_C( 0x3FU )
#define I2C_CR2_ITERREN BIT_U32( 8 )
#define I2C_CR2_ITEVTEN BIT_U32( 9 )
#define I2C_CR2_ITBUFEN BIT_U32( 10 )
#define I2C_CR2_DMAEN   BIT_U32( 11 )

#define I2C_SR1_SB    BIT_U32( 0 )
#define I2C_SR1_ADDR  BIT_U32( 1 )
#define I2C_SR1_BTF   BIT_U32( 2 )
#define I2C_SR1_STOPF BIT_U32( 4 )
#define I2C_SR1_RXNE  BIT_U32( 6 )
#define I2C_SR1_TXE   BIT_U32( 7 )
#define I2C_SR1_AF    BIT_U32( 10 )

#define I2C_CCR_CCR UINT32_C( 0x0FFFU )
#define I2C_CCR_FS  BIT_U32( 15 )

/* DMA register bit definitions used by hw_i2c.c */
#define DMA_SxCR_EN        BIT_U32( 0 )
#define DMA_SxCR_TCIE      BIT_U32( 4 )
#define DMA_SxCR_DIR_0     BIT_U32( 6 )
#define DMA_SxCR_CIRC      BIT_U32( 8 )
#define DMA_SxCR_MINC      BIT_U32( 10 )
#define DMA_SxCR_PL_1      BIT_U32( 17 )
#define DMA_SxCR_CHSEL_Pos 25U

#define DMA_LISR_TCIF0 BIT_U32( 5 )
#define DMA_LISR_TCIF2 BIT_U32( 21 )
#define DMA_HISR_TCIF6 BIT_U32( 21 )
#define DMA_HISR_TCIF7 BIT_U32( 27 )

#define DMA_LIFCR_CFEIF0  BIT_U32( 0 )
#define DMA_LIFCR_CDMEIF0 BIT_U32( 2 )
#define DMA_LIFCR_CTEIF0  BIT_U32( 3 )
#define DMA_LIFCR_CHTIF0  BIT_U32( 4 )
#define DMA_LIFCR_CTCIF0  BIT_U32( 5 )

#define DMA_LIFCR_CFEIF2  BIT_U32( 16 )
#define DMA_LIFCR_CDMEIF2 BIT_U32( 18 )
#define DMA_LIFCR_CTEIF2  BIT_U32( 19 )
#define DMA_LIFCR_CHTIF2  BIT_U32( 20 )
#define DMA_LIFCR_CTCIF2  BIT_U32( 21 )

#define DMA_HIFCR_CFEIF6  BIT_U32( 16 )
#define DMA_HIFCR_CDMEIF6 BIT_U32( 18 )
#define DMA_HIFCR_CTEIF6  BIT_U32( 19 )
#define DMA_HIFCR_CHTIF6  BIT_U32( 20 )
#define DMA_HIFCR_CTCIF6  BIT_U32( 21 )

#define DMA_HIFCR_CFEIF7  BIT_U32( 22 )
#define DMA_HIFCR_CDMEIF7 BIT_U32( 24 )
#define DMA_HIFCR_CTEIF7  BIT_U32( 25 )
#define DMA_HIFCR_CHTIF7  BIT_U32( 26 )
#define DMA_HIFCR_CTCIF7  BIT_U32( 27 )

/* FMPI2C register bit definitions used by hw_i2c.c */
#define FMPI2C_CR1_PE     BIT_U32( 0 )
#define FMPI2C_CR1_TXIE   BIT_U32( 1 )
#define FMPI2C_CR1_RXIE   BIT_U32( 2 )
#define FMPI2C_CR1_STOPIE BIT_U32( 5 )
#define FMPI2C_CR1_NACKIE BIT_U32( 6 )

#define FMPI2C_CR2_RD_WRN      BIT_U32( 10 )
#define FMPI2C_CR2_START       BIT_U32( 13 )
#define FMPI2C_CR2_AUTOEND     BIT_U32( 25 )
#define FMPI2C_CR2_NBYTES_Pos  16U

#define FMPI2C_ISR_TXIS  BIT_U32( 1 )
#define FMPI2C_ISR_RXNE  BIT_U32( 2 )
#define FMPI2C_ISR_NACKF BIT_U32( 4 )
#define FMPI2C_ISR_STOPF BIT_U32( 5 )

#define FMPI2C_ICR_NACKCF BIT_U32( 4 )
#define FMPI2C_ICR_STOPCF BIT_U32( 5 )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
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
	volatile uint32_t FLTR;
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
	volatile uint32_t OAR1;
	volatile uint32_t OAR2;
	volatile uint32_t TIMINGR;
	volatile uint32_t TIMEOUTR;
	volatile uint32_t ISR;
	volatile uint32_t ICR;
	volatile uint32_t PECR;
	volatile uint32_t RXDR;
	volatile uint32_t TXDR;
} FMPI2C_TypeDef;

/**-----------------------------------------------------------------------------
 *  Mock Peripheral Instances
 *------------------------------------------------------------------------------
 */

static I2C_TypeDef I2C1_mock = { 0U };
static I2C_TypeDef I2C2_mock = { 0U };

static DMA_TypeDef DMA1_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream0_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream2_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream6_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream7_mock = { 0U };

static FMPI2C_TypeDef FMPI2C1_mock = { 0U };

#define I2C1 ( &I2C1_mock )
#define I2C2 ( &I2C2_mock )

#define DMA1 ( &DMA1_mock )
#define DMA1_Stream0 ( &DMA1_Stream0_mock )
#define DMA1_Stream2 ( &DMA1_Stream2_mock )
#define DMA1_Stream6 ( &DMA1_Stream6_mock )
#define DMA1_Stream7 ( &DMA1_Stream7_mock )

#define FMPI2C1 ( &FMPI2C1_mock )

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_I2C_MOCKS_H */
