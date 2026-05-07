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
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "blink_green_elf.h"
#include "blink_orange_elf.h"
#include "elf_loader.h"
#include "kernel_api.h"
#include "task.h"
#include "task_manager.h"
#include "myprintf.h"
#include "File_Handling.h"
#include "elf_rw.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ELFFROMSD true
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
FILELIST_FileTypeDef FileList; // records all files type, name, and ptr
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI2_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void test_task_manager(void *args)
{
    (void) args;

    ElfLoaderResult_t r1 =
        elf_loader_load(blink_green_elf, blink_green_elf_len);
    if (r1.entry == NULL) {
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }
    int id_green = task_manager_create("green", &r1);
    if (id_green < 0) {
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }

    ElfLoaderResult_t r2 =
        elf_loader_load(blink_orange_elf, blink_orange_elf_len);
    if (r2.entry == NULL) {
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }
    int id_orange = task_manager_create("orange", &r2);
    if (id_orange < 0) {
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    task_manager_kill(id_green);

    vTaskDelay(pdMS_TO_TICKS(3000));
    task_manager_kill(id_orange);

    HAL_GPIO_WritePin(GPIOD, LD6_Pin, GPIO_PIN_SET);

    vTaskDelete(NULL);
}
void SDCARD_Test(void *pvParameters){
	myprintf("SDCARDTest START!\r\n");
	FATFS fs;

	FRESULT res = f_mount(&fs, "", 1);
	if (res != FR_OK) {
      HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET); // orange
	    myprintf("SD mount failed with error code: %d\r\n", res);
	} else {
      HAL_GPIO_WritePin(GPIOD, LD4_Pin, GPIO_PIN_SET); // green
	    myprintf("SD mounted!\r\n");
	}
}
void ELF_read_Test(void *pvParameters)
{
    uint16_t elf_count;
    
    myprintf("\n=== Starting ELF Inspection Test ===\n");
    
    /* Get number of ELF files on SD card */
    elf_count = ELF_GetelfObjectNumber();
    myprintf("Found %u ELF files on SD card\n", elf_count);
    
    /* Inspect first ELF file if available */
    if(elf_count > 0)
    {
        char *filename = (char *)FileList.file[0].name;
        myprintf("Inspecting first ELF file: %s\n\n", filename);
        
        /* Print raw hexdump of entire file */
        ELF_hexdump(filename, 0);
        
        /* Then parse and inspect structure */
        ELF_ErrorTypeDef err = ELF_inspect(filename);
        if(err != ELF_ERROR_NONE)
        {
            myprintf("ERROR: ELF_inspect() failed with error code %d\n", err);
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);  // Red LED = error
        }
    }
    else
    {
        myprintf("ERROR: No ELF files found on SD card!\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);  // Red LED = error
    }
    
    myprintf("=== ELF Inspection Test Complete ===\n\n");
    vTaskDelete(NULL);
}
void ELF_write_Test(void *pvParameters)
{
    uint16_t elf_count;
    uint8_t *file_data;
    uint32_t file_size;
    ELF_ErrorTypeDef err;
    
    myprintf("\n=== Starting ELF Write Test ===\n");
    
    /* Get number of ELF files on SD card */
    elf_count = ELF_GetelfObjectNumber();
    myprintf("Found %u ELF files on SD card\n", elf_count);
    
    if(elf_count < 1)
    {
        myprintf("ERROR: No ELF files found on SD card!\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);  // Red LED = error
        vTaskDelete(NULL);
    }
    
    /* Read first ELF file */
    char *filename = (char *)FileList.file[0].name;
    myprintf("Reading ELF file: %s\n", filename);
    
    err = ELF_ReadComplete(filename, &file_data, &file_size);
    if(err != ELF_ERROR_NONE)
    {
        myprintf("ERROR: ELF_ReadComplete() failed with error code %d\n", err);
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);  // Red LED = error
        vTaskDelete(NULL);
    }
    
    myprintf("Read %u bytes from %s\n", file_size, filename);
    
    /* Write buffer to new file without .o extension */
    char output_filename[64];
    const char *dot = strrchr(filename, '.');
    if(dot && (dot[1] == 'o' || dot[1] == 'O') && dot[2] == '\0') {
        snprintf(output_filename, sizeof(output_filename), "copy_%.*s", (int)(dot - filename), filename);
    } else {
        snprintf(output_filename, sizeof(output_filename), "copy_%s", filename);
    }
    
    myprintf("Writing copy to: %s\n", output_filename);
    err = ELF_write(output_filename, file_data, file_size);
    
    if(err != ELF_ERROR_NONE)
    {
        myprintf("ERROR: ELF_write() failed with error code %d\n", err);
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);  // Red LED = error
        vPortFree(file_data);
        vTaskDelete(NULL);
    }
    
    myprintf("Successfully wrote %u bytes to %s\n", file_size, output_filename);
    myprintf("ELF Write Test Complete - SUCCESS\n\n");
    
    vPortFree(file_data);
    HAL_GPIO_WritePin(GPIOD, LD6_Pin, GPIO_PIN_SET);  // Green LED = success
    
    vTaskDelete(NULL);
}
void test_task_manager_fromSD(void *args)
{
    /* Files order in SD card: hello.o, blink_green.o, blink_orange.o (Adjus this function refer to the data in SD card later, maybe change it into loop)*/
    (void) args;
    myprintf("[test_task_manager_fromSD] Running...\n");
    
    /* Check if at least 2 ELF files exist on SD card */
    uint16_t elf_count = ELF_GetelfObjectNumber();
    if(elf_count < 2)
    {
        myprintf("ERROR [test_task_manager_fromSD]: Need at least 2 ELF files on SD card\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }
    
    /* Load blink_green.o (second-to-last file) */
    uint32_t green_idx = elf_count - 2;
    char *green_filename = (char *)FileList.file[green_idx].name;
    myprintf("Loading blink_green from SD card: %s\n", green_filename);
    
    uint8_t *green_data;
    uint32_t green_size;
    if(ELF_ReadComplete(green_filename, &green_data, &green_size) != ELF_ERROR_NONE)
    {
        myprintf("ERROR [test_task_manager_fromSD]: ELF_ReadComplete failed for green\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vTaskDelete(NULL);
    }
    
    myprintf("Loaded %u bytes, loading green into memory...\n", green_size);
    ElfLoaderResult_t r_green = elf_loader_load((uint8_t *)green_data, green_size);
    if (r_green.entry == NULL) {
        myprintf("ERROR [test_task_manager_fromSD]: elf_loader_load failed for green\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vPortFree(green_data);
        vTaskDelete(NULL);
    }
    
    int id_green = task_manager_create("green", &r_green);
    if (id_green < 0) {
        myprintf("ERROR [test_task_manager_fromSD]: task_manager_create failed for green\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vPortFree(green_data);
        vTaskDelete(NULL);
    }
    
    myprintf("Green task created successfully (ID=%d)\n", id_green);
    
    /* Load blink_orange.o (last file) */
    uint32_t orange_idx = elf_count - 1;
    char *orange_filename = (char *)FileList.file[orange_idx].name;
    myprintf("Loading blink_orange from SD card: %s\n", orange_filename);
    
    uint8_t *orange_data;
    uint32_t orange_size;
    if(ELF_ReadComplete(orange_filename, &orange_data, &orange_size) != ELF_ERROR_NONE)
    {
        myprintf("ERROR [test_task_manager_fromSD]: ELF_ReadComplete failed for orange\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vPortFree(green_data);
        vTaskDelete(NULL);
    }
    
    myprintf("Loaded %u bytes, loading orange into memory...\n", orange_size);
    ElfLoaderResult_t r_orange = elf_loader_load((uint8_t *)orange_data, orange_size);
    if (r_orange.entry == NULL) {
        myprintf("ERROR [test_task_manager_fromSD]: elf_loader_load failed for orange\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vPortFree(green_data);
        vPortFree(orange_data);
        vTaskDelete(NULL);
    }
    
    int id_orange = task_manager_create("orange", &r_orange);
    if (id_orange < 0) {
        myprintf("ERROR [test_task_manager_fromSD]: task_manager_create failed for orange\n");
        HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
        vPortFree(green_data);
        vPortFree(orange_data);
        vTaskDelete(NULL);
    }
    
    myprintf("Orange task created successfully (ID=%d)\n", id_orange);
    myprintf("Both SD tasks running for 3 seconds...\n");
    
    /* Let tasks run */
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    /* Kill green task */
    myprintf("Killing green task...\n");
    task_manager_kill(id_green);
    vPortFree(green_data);
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    /* Kill orange task */
    myprintf("Killing orange task...\n");
    task_manager_kill(id_orange);
    vPortFree(orange_data);
    
    myprintf("SD task test complete\n");
    HAL_GPIO_WritePin(GPIOD, LD6_Pin, GPIO_PIN_SET);
    
    vTaskDelete(NULL);
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
  MX_SPI2_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
    /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
    /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
    /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
    /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
    kernel_api_init();
    Mount_SD();
    xTaskCreate(ELF_read_Test, "elf_read_test", 1024, NULL, 3, NULL);
    // xTaskCreate(ELF_write_Test, "elf_write_test", 1024, NULL, 4, NULL);
#if ELFFROMSD
    xTaskCreate(test_task_manager_fromSD, "testFromSD", 512, NULL, 2, NULL);
#else
    // xTaskCreate(test_task_manager, "test", 512, NULL, 2, NULL);
#endif
    // xTaskCreate(SDCARD_Test, "SDCARD_Test", 256, NULL, 1, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1) {
    /* USER CODE END WHILE */

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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 50;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

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
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : I2S3_WS_Pin */
  GPIO_InitStruct.Pin = I2S3_WS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(I2S3_WS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI1_SCK_Pin SPI1_MISO_Pin SPI1_MOSI_Pin */
  GPIO_InitStruct.Pin = SPI1_SCK_Pin|SPI1_MISO_Pin|SPI1_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : I2S3_MCK_Pin I2S3_SCK_Pin I2S3_SD_Pin */
  GPIO_InitStruct.Pin = I2S3_MCK_Pin|I2S3_SCK_Pin|I2S3_SD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : VBUS_FS_Pin */
  GPIO_InitStruct.Pin = VBUS_FS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(VBUS_FS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OTG_FS_ID_Pin OTG_FS_DM_Pin OTG_FS_DP_Pin */
  GPIO_InitStruct.Pin = OTG_FS_ID_Pin|OTG_FS_DM_Pin|OTG_FS_DP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Audio_SCL_Pin Audio_SDA_Pin */
  GPIO_InitStruct.Pin = Audio_SCL_Pin|Audio_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
    /* Infinite loop */
    for (;;) {
        osDelay(1);
    }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state
     */
    __disable_irq();
    while (1) {
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
    /* User can add his own implementation to report the file name and line
       number, ex: printf("Wrong parameters value: file %s on line %d\r\n",
       file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
