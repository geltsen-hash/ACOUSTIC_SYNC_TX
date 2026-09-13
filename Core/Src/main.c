/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Acoustic Transmitter Firmware for STM32G474CET3
  ******************************************************************************
  * Target: STM32G474CET3
  * System Clock: 84 MHz (HSE 16 MHz -> PLL)
  * Carrier: 154.411 kHz
  * Period: 2.000 s (168 000 000 ticks)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "functions.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
   NORM,
   TUNE
} work_mode_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SYSCLK_FREQ_HZ               84000000UL
#define SYNC_PERIOD_TICKS            168000000UL /* 2.0s at 84 MHz */

#define CARRIER_HALF_PERIOD_TICKS    272UL       /* 154.411 kHz half-period */
#define CARRIER_FULL_PERIOD_TICKS    544UL       /* 154.411 kHz full period */
#define CARRIER_CYCLES_PER_BIT       66UL        /* 66 carrier cycles per bit */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim5;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
int32_t PulsePeriod = CARRIER_FULL_PERIOD_TICKS;
uint32_t ticks_per_bit = CARRIER_FULL_PERIOD_TICKS * CARRIER_CYCLES_PER_BIT; // 35904 ticks (66 periods)
uint32_t N = 0;
volatile work_mode_t work_mode = NORM;

static uint8_t uart_rx_byte = 0;
static char uart_cmd_buf[32] = {0};
static uint8_t uart_cmd_idx = 0;
static char transmit_buff[128] = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM5_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* Configure the system clock: 84 MHz */
  SystemClock_Config();

  /* USER CODE BEGIN Init */
  DWT_Init();
  HAL_Delay(50);
  /* USER CODE END Init */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM5_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  // Запуск таймера TIM5 (32-бит эталон 2.0 с, 168 000 000 тактов)
  TIM5->ARR = SYNC_PERIOD_TICKS - 1;
  TIM5->CNT = 0;
  __HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_UPDATE);
  HAL_TIM_Base_Start(&htim5);
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_4);

  // Включение TPS питания
  DIS_TPS_GPIO_Port->BSRR = DIS_TPS_Pin; // TPS ON

  // Индикация старта (мигание LED на PA5 и SYNC_OUT на PA0)
  for (int i = 0; i < 6; i++) {
      HAL_GPIO_TogglePin(SYNC_OUT_GPIO_Port, SYNC_OUT_Pin);
      HAL_GPIO_TogglePin(LED_TX_GPIO_Port, LED_TX_Pin);
      HAL_Delay(60);
  }
  SYNC_OUT_GPIO_Port->BRR = SYNC_OUT_Pin;
  LED_TX_GPIO_Port->BRR = LED_TX_Pin;

  snprintf(transmit_buff, sizeof(transmit_buff), "ACOUSTIC_SYNC_TX_84MHz\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t*)transmit_buff, strlen(transmit_buff), 50);

  // Настройка несущей 154.411 кГц на TIM1
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

  TIM1->BDTR &= ~TIM_BDTR_MOE; // Timer outputs disabled by default
  TIM1->ARR = CARRIER_FULL_PERIOD_TICKS - 1; // 544 - 1
  TIM1->CCR1 = CARRIER_HALF_PERIOD_TICKS;    // 272 (50% duty)

  // Запуск приема команд по UART (побайтовый неблокирующий прием)
  HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      if (work_mode == NORM)
      {
          // 1. Включение питания преобразователя TPS
          DIS_TPS_GPIO_Port->BSRR = DIS_TPS_Pin; // TPS ON
          delay_micros(200);

          // 2. Излучение 31-битной М-последовательности Баркера (154.4 кГц)
          SEND_M_SEQ();

          // 3. Формирование импульса синхронизации SYNC_OUT (PA0) и индикация LED (PA5)
          SYNC_OUT_GPIO_Port->BSRR = SYNC_OUT_Pin;
          LED_TX_GPIO_Port->BSRR = LED_TX_Pin;
          delay_micros(300);
          SYNC_OUT_GPIO_Port->BRR = SYNC_OUT_Pin;
          LED_TX_GPIO_Port->BRR = LED_TX_Pin;

          // 4. Отключение питания аналоговой части на время паузы (энергосбережение)
          DIS_TPS_GPIO_Port->BRR = DIS_TPS_Pin;   // TPS OFF
          DIS_DRV_GPIO_Port->BSRR = DIS_DRV_Pin; // Driver OFF (Active Low: 1 = Disabled)

          N++;

          // 5. Ожидание аппаратного переполнения 2.0-секундного периода (TIM_FLAG_UPDATE)
          while (__HAL_TIM_GET_FLAG(&htim5, TIM_FLAG_UPDATE) == RESET && work_mode == NORM)
          {
              __NOP();
          }
          __HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_UPDATE);
      }
      else if (work_mode == TUNE)
      {
          // Режим непрерывной генерации / свипирования для настройки резонанса
          DIS_TPS_GPIO_Port->BSRR = DIS_TPS_Pin; // TPS ON
          DIS_DRV_GPIO_Port->BRR = DIS_DRV_Pin;  // Driver ON (Active Low: 0 = Enabled)
          TIM1->BDTR |= TIM_BDTR_MOE;            // PWM ON
          HAL_Delay(100);
      }
  }
  /* USER CODE END WHILE */
}

/* USER CODE BEGIN 4 */
static void Process_UART_Command(const char *cmd)
{
    // Команда GTUN (GTUN<период> или GTUN) -> Режим свипирования / настройки частоты
    if (strncmp(cmd, "GTUN", 4) == 0)
    {
        work_mode = TUNE;
        long period_val = 0;
        if (1 == sscanf(cmd, "GTUN%ld", &period_val) && period_val >= 20 && period_val <= 20000)
        {
            PulsePeriod = period_val;
            TIM1->BDTR &= ~TIM_BDTR_MOE; // timer off
            TIM1->ARR = PulsePeriod - 1;
            TIM1->CCR1 = (PulsePeriod / 2) - 1;
            TIM1->EGR = TIM_EGR_UG;
            snprintf(transmit_buff, sizeof(transmit_buff), "TUNE: ARR=%lu CCR1=%lu\r\n", TIM1->ARR, TIM1->CCR1);
            HAL_UART_Transmit(&huart1, (uint8_t*)transmit_buff, strlen(transmit_buff), 30);
            TIM1->BDTR |= TIM_BDTR_MOE; // timer on
        }
        else
        {
            HAL_UART_Transmit(&huart1, (uint8_t*)"TUNE_MODE\r\n", 11, 30);
        }
    }
    // Команда GNOR -> Возврат в штатный режим (NORM)
    else if (strncmp(cmd, "GNOR", 4) == 0)
    {
        work_mode = NORM;
        TIM1->BDTR &= ~TIM_BDTR_MOE; // timer off
        TIM1->ARR = CARRIER_FULL_PERIOD_TICKS - 1; // 544 - 1
        TIM1->CCR1 = CARRIER_HALF_PERIOD_TICKS;
        TIM1->EGR = TIM_EGR_UG;
        DIS_TPS_GPIO_Port->BRR = DIS_TPS_Pin;   // TPS OFF
        DIS_DRV_GPIO_Port->BSRR = DIS_DRV_Pin; // Driver OFF
        TIM5->CNT = 0;
        __HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_UPDATE);
        HAL_UART_Transmit(&huart1, (uint8_t*)"NORM\r\n", 6, 30);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        char c = (char)uart_rx_byte;

        if (c == 'G')
        {
            uart_cmd_buf[0] = 'G';
            uart_cmd_idx = 1;
        }
        else if (uart_cmd_idx > 0)
        {
            if (c == '\r' || c == '\n')
            {
                uart_cmd_buf[uart_cmd_idx] = '\0';
                Process_UART_Command(uart_cmd_buf);
                uart_cmd_idx = 0;
            }
            else
            {
                if (uart_cmd_idx < sizeof(uart_cmd_buf) - 1)
                {
                    uart_cmd_buf[uart_cmd_idx++] = c;
                    uart_cmd_buf[uart_cmd_idx] = '\0';
                }

                if (strncmp(uart_cmd_buf, "GNOR", 4) == 0)
                {
                    Process_UART_Command(uart_cmd_buf);
                    uart_cmd_idx = 0;
                }
                else if (uart_cmd_idx >= 12)
                {
                    Process_UART_Command(uart_cmd_buf);
                    uart_cmd_idx = 0;
                }
            }
        }

        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}
/* USER CODE END 4 */

/**
  * @brief System Clock Configuration (84 MHz)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 14;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM1 Initialization Function (154.411 kHz Complementary PWM)
  */
static void MX_TIM1_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 544 - 1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 272;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim1);
}

/**
  * @brief TIM5 Initialization Function (32-bit Master 2.0s Timer)
  */
static void MX_TIM5_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 0;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = SYNC_PERIOD_TICKS - 1; // 168 000 000 - 1 (2.0s at 84 MHz)
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim5);
}

/**
  * @brief USART1 Initialization Function (115200 baud)
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, SYNC_OUT_Pin|LED_TX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, DIS_DRV_Pin|DIS_TPS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : SYNC_OUT_Pin (PA0) LED_TX_Pin (PA5) */
  GPIO_InitStruct.Pin = SYNC_OUT_Pin|LED_TX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : DIS_DRV_Pin (PB11) */
  GPIO_InitStruct.Pin = DIS_DRV_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(DIS_DRV_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DIS_TPS_Pin (PB14) */
  GPIO_InitStruct.Pin = DIS_TPS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DIS_TPS_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
