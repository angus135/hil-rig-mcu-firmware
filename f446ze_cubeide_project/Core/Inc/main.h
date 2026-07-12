/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_B_1_Pin GPIO_PIN_2
#define LED_B_1_GPIO_Port GPIOE
#define LED_B_0_Pin GPIO_PIN_3
#define LED_B_0_GPIO_Port GPIOE
#define LED_R_5_Pin GPIO_PIN_4
#define LED_R_5_GPIO_Port GPIOE
#define LED_R_4_Pin GPIO_PIN_5
#define LED_R_4_GPIO_Port GPIOE
#define LED_R_3_Pin GPIO_PIN_6
#define LED_R_3_GPIO_Port GPIOE
#define LED_R_2_Pin GPIO_PIN_13
#define LED_R_2_GPIO_Port GPIOC
#define LED_R_1_Pin GPIO_PIN_14
#define LED_R_1_GPIO_Port GPIOC
#define LED_R_0_Pin GPIO_PIN_15
#define LED_R_0_GPIO_Port GPIOC
#define STATUS_5V_Pin GPIO_PIN_0
#define STATUS_5V_GPIO_Port GPIOF
#define ADC_24V_Current_Pin GPIO_PIN_3
#define ADC_24V_Current_GPIO_Port GPIOF
#define ADC_12V_Current_Pin GPIO_PIN_4
#define ADC_12V_Current_GPIO_Port GPIOF
#define ADC_5V_Current_Pin GPIO_PIN_5
#define ADC_5V_Current_GPIO_Port GPIOF
#define MCO_Pin GPIO_PIN_0
#define MCO_GPIO_Port GPIOH
#define ADC_VIN_Pin GPIO_PIN_0
#define ADC_VIN_GPIO_Port GPIOC
#define ADC_24V_Pin GPIO_PIN_3
#define ADC_24V_GPIO_Port GPIOC
#define PWM_CAPTURE_CH1_Pin GPIO_PIN_0
#define PWM_CAPTURE_CH1_GPIO_Port GPIOA
#define PWM_CAPTURE_CH_2_Pin GPIO_PIN_1
#define PWM_CAPTURE_CH_2_GPIO_Port GPIOA
#define ADC_12V_Pin GPIO_PIN_2
#define ADC_12V_GPIO_Port GPIOA
#define ADC_5V_Pin GPIO_PIN_3
#define ADC_5V_GPIO_Port GPIOA
#define PWM_GEN_HV_Pin GPIO_PIN_0
#define PWM_GEN_HV_GPIO_Port GPIOB
#define SPI1_CS_TEST_Pin GPIO_PIN_9
#define SPI1_CS_TEST_GPIO_Port GPIOE
#define PWM_GEN_LV_Pin GPIO_PIN_15
#define PWM_GEN_LV_GPIO_Port GPIOB
#define Digital_Input_0_Pin GPIO_PIN_8
#define Digital_Input_0_GPIO_Port GPIOD
#define Digital_Input_1_Pin GPIO_PIN_9
#define Digital_Input_1_GPIO_Port GPIOD
#define Digital_Input_2_Pin GPIO_PIN_10
#define Digital_Input_2_GPIO_Port GPIOD
#define Digital_Input_3_Pin GPIO_PIN_11
#define Digital_Input_3_GPIO_Port GPIOD
#define Digital_Input_4_Pin GPIO_PIN_14
#define Digital_Input_4_GPIO_Port GPIOD
#define Digital_Input_5_Pin GPIO_PIN_15
#define Digital_Input_5_GPIO_Port GPIOD
#define Digital_Output_0_Pin GPIO_PIN_2
#define Digital_Output_0_GPIO_Port GPIOG
#define Digital_Output_1_Pin GPIO_PIN_3
#define Digital_Output_1_GPIO_Port GPIOG
#define Digital_Output_2_Pin GPIO_PIN_4
#define Digital_Output_2_GPIO_Port GPIOG
#define Digital_Output_3_Pin GPIO_PIN_5
#define Digital_Output_3_GPIO_Port GPIOG
#define Digital_Output_4_Pin GPIO_PIN_7
#define Digital_Output_4_GPIO_Port GPIOG
#define Digital_Output_5_Pin GPIO_PIN_8
#define Digital_Output_5_GPIO_Port GPIOG
#define USB_DM_Pin GPIO_PIN_11
#define USB_DM_GPIO_Port GPIOA
#define USB_DP_Pin GPIO_PIN_12
#define USB_DP_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define Digital_Input_6_Pin GPIO_PIN_0
#define Digital_Input_6_GPIO_Port GPIOD
#define Digital_Input_7_Pin GPIO_PIN_1
#define Digital_Input_7_GPIO_Port GPIOD
#define Digital_Input_8_Pin GPIO_PIN_2
#define Digital_Input_8_GPIO_Port GPIOD
#define Digital_Input_9_Pin GPIO_PIN_3
#define Digital_Input_9_GPIO_Port GPIOD
#define Digital_Output_6_Pin GPIO_PIN_9
#define Digital_Output_6_GPIO_Port GPIOG
#define Digital_Output_7_Pin GPIO_PIN_10
#define Digital_Output_7_GPIO_Port GPIOG
#define Digital_Output_8_Pin GPIO_PIN_11
#define Digital_Output_8_GPIO_Port GPIOG
#define Digital_Output_9_Pin GPIO_PIN_12
#define Digital_Output_9_GPIO_Port GPIOG
#define LED_B_5_Pin GPIO_PIN_4
#define LED_B_5_GPIO_Port GPIOB
#define LED_B_4_Pin GPIO_PIN_7
#define LED_B_4_GPIO_Port GPIOB
#define LED_B_3_Pin GPIO_PIN_0
#define LED_B_3_GPIO_Port GPIOE
#define LED_B_2_Pin GPIO_PIN_1
#define LED_B_2_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
