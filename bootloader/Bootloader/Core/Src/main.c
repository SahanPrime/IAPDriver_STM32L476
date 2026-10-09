/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "flash_map.h"
#include "metadata.h"
#include "crc_util.h"
#include "iap_apply.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void(*pFunction)(void);
#define FLASH_APP_ADDR 0x08008000
#define UART_START_BYTE 0xAAU
#define UART_CMD_START_UPDATE 0x01U
#define UART_CMD_WRITE_CHUNK 0x02U
#define UART_CMD_END_UPDATE 0x03U
#define UART_CMD_ACK 0x04U
#define UART_CMD_NACK 0x05U
#define UART_CMD_APPLY_UPDATE 0x06U
#define UART_UPDATE_WINDOW_MS 5000U
#define UART_PACKET_TIMEOUT_MS 2000U
#define UART_CHUNK_SIZE 256U
#define UART_MAX_PAYLOAD (UART_CHUNK_SIZE + 4U)
void go2APP(void);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void go2APP(void)
	{
		uint32_t JumpAddress;
		pFunction Jump_To_Application;
		printf("Bootloader Start\r\n");
		//check
		if(((*(__IO uint32_t*)FLASH_APP_ADDR)&0x2FFE0000)==0x20000000)
		{
			printf("APP Start...\r\n");
			HAL_Delay(100);
			//Jump to user Application//
			JumpAddress=*(__IO uint32_t*)(FLASH_APP_ADDR+4);
			Jump_To_Application=(pFunction)JumpAddress;
			//Initialize user application's stack pointer//
			__disable_irq();
			SysTick->CTRL = 0;
			SysTick->VAL = 0;
			HAL_UART_DeInit(&huart2);          // <-- add here: matches your bootloader's UART handle name
			for (uint8_t i = 0; i < 8; i++) {   // <-- add here: clear all NVIC enable + pending state
			            NVIC->ICER[i] = 0xFFFFFFFF;
			            NVIC->ICPR[i] = 0xFFFFFFFF;
			        }
      SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
      SCB->VTOR = FLASH_APP_ADDR;
      __DSB();
      __ISB();
			__set_MSP(*(__IO uint32_t*)FLASH_APP_ADDR);
			__enable_irq();
			Jump_To_Application();

		}
		else{
			printf("No APP found !!!\r\n");
		}
	}



int _write(int file,char *ptr,int len)
{
	int DataIdx;
	for(DataIdx=0;DataIdx<len;DataIdx++)
{
	HAL_UART_Transmit(&huart2,(uint8_t*)ptr++,1,100);
}
	return len;
}

static uint32_t read_u32_be(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static void uart_send_response(uint8_t command)
{
  uint8_t packet[8] = {UART_START_BYTE, command, 0, 0, 0, 0, 0, 0};
  HAL_UART_Transmit(&huart2, packet, sizeof(packet), HAL_MAX_DELAY);
}

static int uart_receive_packet(uint8_t *command, uint8_t *payload,
    uint16_t *payload_length, uint32_t first_byte_timeout_ms)
{
  uint8_t start_byte;
  uint8_t header[3];
  uint8_t crc_bytes[4];
  uint32_t started = HAL_GetTick();

  while ((HAL_GetTick() - started) < first_byte_timeout_ms) {
    uint32_t remaining = first_byte_timeout_ms - (HAL_GetTick() - started);
    if (HAL_UART_Receive(&huart2, &start_byte, 1, remaining) != HAL_OK) {
      return 0;
    }
    if (start_byte == UART_START_BYTE) {
      break;
    }
  }
  if (start_byte != UART_START_BYTE) {
    return 0;
  }

  if (HAL_UART_Receive(&huart2, header, sizeof(header), UART_PACKET_TIMEOUT_MS) != HAL_OK) {
    return -1;
  }
  *command = header[0];
  *payload_length = ((uint16_t)header[1] << 8) | header[2];
  if (*payload_length > UART_MAX_PAYLOAD) {
    return -1;
  }
  if (*payload_length > 0 &&
      HAL_UART_Receive(&huart2, payload, *payload_length, UART_PACKET_TIMEOUT_MS) != HAL_OK) {
    return -1;
  }
  if (HAL_UART_Receive(&huart2, crc_bytes, sizeof(crc_bytes), UART_PACKET_TIMEOUT_MS) != HAL_OK) {
    return -1;
  }

  uint32_t received_crc = read_u32_be(crc_bytes);
  uint32_t calculated_crc = *payload_length ?
    crc32_zlib_compatible(payload, *payload_length) : 0U;
  return received_crc == calculated_crc ? 1 : -1;
}

static HAL_StatusTypeDef erase_staging_region(uint32_t image_size)
{
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t page_error = 0;
  uint32_t first_page = (STAGING_ADDR - FLASH_BASE - FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
  uint32_t page_count = (image_size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.Banks = FLASH_BANK_2;
  erase_init.Page = first_page;
  erase_init.NbPages = page_count;
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &page_error);
  HAL_FLASH_Lock();
  return status;
}

static HAL_StatusTypeDef write_staging_chunk(uint32_t offset,
    const uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef status = HAL_OK;
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

  for (uint16_t index = 0; index < length; index += 8U) {
    uint64_t double_word;
    memcpy(&double_word, &data[index], sizeof(double_word));
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
        STAGING_ADDR + offset + index, double_word);
    if (status != HAL_OK) {
      break;
    }
  }

  HAL_FLASH_Lock();
  return status;
}

static int staging_vector_table_is_valid(uint32_t image_size)
{
  const uint32_t *vectors = (const uint32_t *)STAGING_ADDR;
  uint32_t stack_pointer = vectors[0];
  uint32_t reset_handler = vectors[1] & ~1U;

  return image_size >= 8U &&
         stack_pointer >= 0x20000000UL && stack_pointer <= 0x20018000UL &&
         reset_handler >= ACTIVE_APP_ADDR &&
         reset_handler < ACTIVE_APP_ADDR + ACTIVE_APP_SIZE;
}

static void bootloader_uart_update(void)
{
  uint8_t payload[UART_MAX_PAYLOAD];
  uint8_t command;
  uint16_t payload_length;
  uint32_t image_size = 0;
  uint32_t expected_crc = 0;
  uint32_t received_size = 0;
  uint32_t started = HAL_GetTick();
  int update_started = 0;

  printf("UART update window: %u ms\r\n", UART_UPDATE_WINDOW_MS);
  while (1) {
    uint32_t timeout;
    if (update_started) {
      timeout = UART_PACKET_TIMEOUT_MS;
    } else {
      uint32_t elapsed = HAL_GetTick() - started;
      if (elapsed >= UART_UPDATE_WINDOW_MS) {
        return;
      }
      timeout = UART_UPDATE_WINDOW_MS - elapsed;
    }

    int packet_status = uart_receive_packet(&command, payload, &payload_length, timeout);
    if (packet_status == 0) {
      return;
    }
    if (packet_status < 0) {
      uart_send_response(UART_CMD_NACK);
      continue;
    }

    if (command == UART_CMD_START_UPDATE && payload_length == 8U) {
      image_size = read_u32_be(payload);
      expected_crc = read_u32_be(&payload[4]);
      if (image_size < 8U || image_size > STAGING_SIZE ||
          erase_staging_region(image_size) != HAL_OK) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }
      received_size = 0;
      update_started = 1;
      uart_send_response(UART_CMD_ACK);
      continue;
    }

    if (command == UART_CMD_WRITE_CHUNK && update_started && payload_length > 0U &&
        payload_length <= UART_CHUNK_SIZE && received_size < image_size) {
      uint32_t remaining = image_size - received_size;
      uint16_t actual_length = remaining < UART_CHUNK_SIZE ?
        (uint16_t)remaining : UART_CHUNK_SIZE;
      uint16_t padded_length = (actual_length + 7U) & ~7U;

      if (payload_length != padded_length ||
          write_staging_chunk(received_size, payload, payload_length) != HAL_OK) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }

      received_size += actual_length;
      uart_send_response(UART_CMD_ACK);
      continue;
    }

    if (command == UART_CMD_END_UPDATE && update_started && payload_length == 0U) {
      uint32_t actual_crc = crc32_zlib_compatible(
        (const uint8_t *)STAGING_ADDR, image_size);
      if (received_size != image_size || actual_crc != expected_crc ||
          !staging_vector_table_is_valid(image_size)) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }

      boot_metadata_t metadata;
      metadata_read(&metadata);
      metadata.magic = METADATA_MAGIC;
      metadata.staging_valid = 1U;
      metadata.staging_size = image_size;
      metadata.staging_crc = expected_crc;
      metadata.apply_requested = 1U;
      if (metadata_write(&metadata) != HAL_OK) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }
      uart_send_response(UART_CMD_ACK);
      NVIC_SystemReset();
    }

    if (command == UART_CMD_APPLY_UPDATE && payload_length == 0U) {
      boot_metadata_t metadata;
      metadata_read(&metadata);
      if (metadata.magic != METADATA_MAGIC || metadata.staging_valid == 0U ||
          metadata.staging_size < 8U || metadata.staging_size > STAGING_SIZE) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }
      metadata.apply_requested = 1U;
      if (metadata_write(&metadata) != HAL_OK) {
        uart_send_response(UART_CMD_NACK);
        continue;
      }
      uart_send_response(UART_CMD_ACK);
      NVIC_SystemReset();
    }

    uart_send_response(UART_CMD_NACK);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  	printf("IAP Demo Boot\r\n");
    CRC_Init_Zlib_Compatible();
    metadata_init_if_needed();
    iap_check_and_apply_update();   /* checks apply_requested, copies Staging->Active if needed */
    bootloader_uart_update();


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
	  go2APP();
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
