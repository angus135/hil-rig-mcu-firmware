/******************************************************************************
 *  File:       command_helpers.h
 *  Author:     Angus Corr
 *  Created:    25-Apr-2026
 *
 *  Description:
 *      Header file for declaration of helper functions for console
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef COMMAND_HELPERS_H
#define COMMAND_HELPERS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "global_config.h"
#if GLOBAL_CONFIG__CONSOLE_ENABLED

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "hw_i2c.h"
#include "logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    CONSOLE_I2C_LOOPBACK_DIR_M2S,
    CONSOLE_I2C_LOOPBACK_DIR_S2M,
} ConsoleI2CLoopbackDirection_T;

typedef struct
{
    HWI2CChannel_T master;
    HWI2CChannel_T slave;
} CONSOLEI2CLoopbackChannels_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Handles UART-related console commands.
 *
 * Supported command namespace:
 *   uart_loopback ...
 *
 * @param argc Number of parsed command arguments.
 * @param argv Parsed command argument array.
 *
 * @returns void
 */
void CONSOLE_UART_Command_Handler( uint16_t argc, char* argv[] );

/**
 * @brief Handle the top-level classic-CAN console namespace.
 *
 * Supported subcommands configure or deconfigure one physical CAN controller,
 * queue standard or extended data/remote frames, read complete received frames,
 * inspect non-blocking diagnostics, and recover a faulted controller.
 *
 * Parsing and presentation remain in console task context. The implementation
 * delegates hardware lifecycle and frame movement exclusively to EXEC_CAN.
 *
 * @param argc Number of parsed command arguments, including the leading "can".
 * @param argv Parsed command argument array.
 *
 * @returns void
 */
void CONSOLE_CAN_Command_Handler( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Print_Usage( void );

void CONSOLE_SPI_Loopback_Config( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Apply( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Load( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Clear( void );

void CONSOLE_SPI_Loopback_Status( void );

void CONSOLE_SPI_Loopback_Run( uint16_t argc, char* argv[] );

/**
 * @brief Parses the expander port selector.
 */
bool CONSOLE_Parse_Expander_Port( const char* arg, LogicExpanderPort_T* port );

/**
 * @brief Parses the master and slave I2C channel selection.
 */
bool CONSOLE_Parse_I2C_Master_And_Slave( const char* arg, HWI2CChannel_T* master_channel,
                                         HWI2CChannel_T* slave_channel );
/**
 * @brief Parses the loopback direction selector.
 */
bool CONSOLE_Parse_I2C_Loopback_Direction( const char*                    arg,
                                           ConsoleI2CLoopbackDirection_T* direction );
/**
 * @brief Parses the requested I2C bus speed.
 */
bool CONSOLE_Parse_I2C_Speed( const char* arg, HWI2CSpeed_T* speed );
/**
 * @brief Parses the I2C transfer path selection.
 */
bool CONSOLE_Parse_I2C_Transfer_Path( const char* arg, HWI2CTransferPath_T* transfer_path );
/**
 * @brief Builds the loopback payload from command arguments.
 */
bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                uint16_t out_message_size, uint16_t* out_message_length );
/**
 * @brief Runs a master-to-slave I2C loopback transfer.
 */
bool CONSOLE_Run_I2C_Loopback_M2S( CONSOLEI2CLoopbackChannels_T channels, uint16_t slave_addr,
                                   const char* tx_message, uint16_t tx_len, char* rx_message,
                                   uint16_t rx_message_size, uint16_t* out_received_len );
/**
 * @brief Runs a slave-to-master I2C loopback transfer.
 */
bool CONSOLE_Run_I2C_Loopback_S2M( CONSOLEI2CLoopbackChannels_T channels, uint16_t slave_addr,
                                   const char* tx_message, uint16_t tx_len, char* rx_message,
                                   uint16_t rx_message_size, uint16_t* out_received_len );

#endif

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_HELPERS_H */
