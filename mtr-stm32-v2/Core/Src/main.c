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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Uncomment to disable safety watchdog during debugging/testing */
//#define DISABLE_SAFETY_WATCHDOG

/* DAC Output Scaling configuration (assuming 5.0V DAC VCC) */
#define DAC_MIN_VAL   655   /* 0.8V */
#define DAC_MAX_VAL   3931  /* 4.8V */

/* Decoder GPIO Pin Definitions */
#define DEC_EN_PIN    GPIO_PIN_0
#define DEC_EN_PORT   GPIOB
#define DEC_B_PIN     GPIO_PIN_1
#define DEC_B_PORT    GPIOA
#define DEC_A_PIN     GPIO_PIN_0
#define DEC_A_PORT    GPIOA
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Operating Modes */
typedef enum
{
  MODE_SAFE,
  MODE_AUTO,
  MODE_ESTOP
} ControlMode_t;

volatile ControlMode_t currentMode = MODE_SAFE;
volatile uint32_t systemTimeMs = 0;
volatile uint32_t lastRxTimeMs = 0;
volatile uint8_t firstMsgReceived = 0;

/* Motor Speed / Gear Commands (VCU -> Motor Controller) */
volatile int32_t targetSpeedMms = 0;
volatile uint8_t targetGear = 0;
volatile uint8_t driveRollCnt = 0;
volatile uint16_t torqueLimitNm = 300;

/* Brake Command (VCU -> Braking System, read by Motor Controller) */
volatile float targetBrakePressure = 0.0f;

/* Motor Feedback Variables (Motor Controller -> VCU) */
volatile int16_t actualSpeedMms = 0;
volatile uint8_t gearStateFeedback = 0;
volatile uint8_t faultFlags = 0;
volatile int8_t motorTemp = 25;
volatile int8_t controllerTemp = 30;
volatile int16_t motorCurrentA = 0;

/* Heartbeat Variables */
volatile uint8_t heartbeatAliveCtr = 0;

/* Peripheral handles */
FDCAN_HandleTypeDef hfdcan1;

/* Software I2C GPIO Pin Definitions */
#define I2C_SCL_PIN       GPIO_PIN_5
#define I2C_SCL_PORT      GPIOA
#define I2C_SDA_PIN       GPIO_PIN_7
#define I2C_SDA_PORT      GPIOA

volatile uint8_t detectedI2CAddr = 0;

#define WATCHDOG_TIMEOUT_MS  100
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void sw_i2c_delay(void);
static void sw_i2c_start(void);
static void sw_i2c_stop(void);
static uint8_t sw_i2c_write_byte(uint8_t byte);
static void writeMCP4725(uint16_t value);
static void executeSafeState(void);
static void processMainState(void);
static void transmitMotorFeedback(void);
static void transmitSystemHeartbeat(void);
static void scanI2CBus(void);
static void transmitDebugI2C(void);
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
  /* USER CODE BEGIN 2 */
  MX_GPIO_Init();
  MX_FDCAN1_Init();

  /* Start FDCAN */
  if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  
  /* Activate Rx FIFO 0 interrupt notification */
  if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
  {
    Error_Handler();
  }

  /* Scan I2C Bus to detect MCP4725 address */
  scanI2CBus();

  /* Force Startup Safe State */
  executeSafeState();
  lastRxTimeMs = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* Check VCU Watchdog Timeout */
#ifndef DISABLE_SAFETY_WATCHDOG
    if (firstMsgReceived && ((systemTimeMs - lastRxTimeMs) > WATCHDOG_TIMEOUT_MS))
    {
      currentMode = MODE_ESTOP;
      faultFlags |= (1 << 1); /* CMD Timeout Flag */
    }
#endif

    /* Safety State Machine */
    if (currentMode == MODE_ESTOP)
    {
      executeSafeState();
    }
    else
    {
      processMainState();
    }

    /* 20ms Cyclic Task: Motor Feedback Transmit */
    if ((systemTimeMs % 20) == 0)
    {
      transmitMotorFeedback();
    }

    /* 500ms Cyclic Task: Heartbeat Transmit */
    if ((systemTimeMs % 500) == 0)
    {
      transmitSystemHeartbeat();
    }

    HAL_Delay(1);
    systemTimeMs++;
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level for CS (PA4), I2C lines SCL/SDA (PA5/PA7) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | I2C_SCL_PIN | I2C_SDA_PIN, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level for Decoder Selects (PA1, PA0) */
  HAL_GPIO_WritePin(GPIOA, DEC_B_PIN | DEC_A_PIN, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level for Decoder Enable (PB0) */
  HAL_GPIO_WritePin(DEC_EN_PORT, DEC_EN_PIN, GPIO_PIN_SET); /* ENABLE_G = High (Disabled) */

  /*Configure GPIO pin Output Level for WeAct LED (PC6) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET); /* LED ON (Active Low) */

  /*Configure PA4, PA1 (DEC_B), PA0 (DEC_A) as Output PP */
  GPIO_InitStruct.Pin = GPIO_PIN_4 | DEC_B_PIN | DEC_A_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure PA5 (SCL) and PA7 (SDA) as Open-Drain Outputs with Pull-ups for Software I2C */
  GPIO_InitStruct.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure PB0 (Decoder Enable) as Output PP */
  GPIO_InitStruct.Pin = DEC_EN_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure PC6 as Output (Status LED) */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void MX_FDCAN1_Init(void)
{
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = ENABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  
  /* Nominal bit timing for 500kbps CAN baud rate (assuming 16MHz clock) */
  hfdcan1.Init.NominalPrescaler = 2;
  hfdcan1.Init.NominalSyncJumpWidth = 2;
  hfdcan1.Init.NominalTimeSeg1 = 13;
  hfdcan1.Init.NominalTimeSeg2 = 2;
  
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 3;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Configure Rx Filter to accept 0x204, 0x001, 0x205 */
  FDCAN_FilterTypeDef sFilterConfig;
  
  /* Filter for 0x204 (Drive Command) */
  sFilterConfig.IdType = FDCAN_STANDARD_ID;
  sFilterConfig.FilterIndex = 0;
  sFilterConfig.FilterType = FDCAN_FILTER_MASK;
  sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  sFilterConfig.FilterID1 = 0x204;
  sFilterConfig.FilterID2 = 0x7FF; /* Exact match mask */
  if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* Filter for 0x001 (ESTOP) */
  sFilterConfig.FilterIndex = 1;
  sFilterConfig.FilterID1 = 0x001;
  if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* Filter for 0x205 (Brake Command) */
  sFilterConfig.FilterIndex = 2;
  sFilterConfig.FilterID1 = 0x205;
  if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef* hfdcan)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(hfdcan->Instance==FDCAN1)
  {
    /* FDCAN1 Clock source config */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    /**FDCAN1 GPIO Configuration
    PA11     ------> FDCAN1_RX
    PA12     ------> FDCAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* FDCAN1 interrupt Init */
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
  }
}



static void sw_i2c_delay(void)
{
  /* Simple delay loop for ~50kHz-100kHz operation depending on clock speed.
     For STM32G4 at 16MHz, a loop to 50 is safe and reliable. */
  for (volatile int i = 0; i < 50; i++);
}

static void sw_i2c_start(void)
{
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
  sw_i2c_delay();
}

static void sw_i2c_stop(void)
{
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
}

static uint8_t sw_i2c_write_byte(uint8_t byte)
{
  uint8_t ack = 0;
  for (uint8_t i = 0; i < 8; i++)
  {
    if (byte & 0x80)
    {
      HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    }
    else
    {
      HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    }
    byte <<= 1;
    sw_i2c_delay();
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    sw_i2c_delay();
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
  }
  
  /* Read ACK (configure SDA as high, i.e., let pull-up pull it high, and sample pin state) */
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
  sw_i2c_delay();
  
  /* If pin is low, ACK was received */
  ack = (HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) == GPIO_PIN_RESET) ? 1 : 0;
  
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
  sw_i2c_delay();
  
  return ack;
}

static void writeMCP4725(uint16_t value)
{
  /* Limit value to 12-bit (0-4095) */
  if (value > 4095)
  {
    value = 4095;
  }
  
  /* Try both possible base addresses: 0x60 (A0 tied to GND) and 0x61 (A0 tied to VCC) */
  uint8_t addrs[] = {0x60 << 1, 0x61 << 1};
  
  for (int i = 0; i < 2; i++)
  {
    sw_i2c_start();
    
    /* Send device address with write bit (0) */
    if (sw_i2c_write_byte(addrs[i]))
    {
      /* Normal Write Mode (Write DAC Register):
         Byte 1: 0x40 (Command = 010, PD1=0, PD0=0)
         Byte 2: High 8 bits of data -> (value >> 4)
         Byte 3: Low 4 bits of data -> (value << 4) & 0xF0
      */
      sw_i2c_write_byte(0x40);
      sw_i2c_write_byte((uint8_t)(value >> 4));
      sw_i2c_write_byte((uint8_t)((value << 4) & 0xF0));
      sw_i2c_stop();
      return; /* Success, exit function */
    }
    
    sw_i2c_stop();
  }
}

static void executeSafeState(void)
{
  writeMCP4725(0); /* Zero output voltage */
  
  /* Disable Decoder (ENABLE_G = High) */
  HAL_GPIO_WritePin(DEC_EN_PORT, DEC_EN_PIN, GPIO_PIN_SET);
  
  /* Turn OFF status LED */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET);
  
  faultFlags |= (1 << 0); /* ESTOP Active flag */
}
 
static void processMainState(void)
{
  uint16_t dacVal = DAC_MIN_VAL + (uint16_t)(((uint32_t)targetSpeedMms * (DAC_MAX_VAL - DAC_MIN_VAL)) / 3000);
  
  /* Select Decoder channel based on Gear state selection:
     targetGear: 0=Neutral, 1=Drive, 2=Sport, 3=Reverse
  */
  switch (targetGear)
  {
    case 0: /* Neutral */
      HAL_GPIO_WritePin(DEC_B_PORT, DEC_B_PIN, GPIO_PIN_RESET); /* B = 0 */
      HAL_GPIO_WritePin(DEC_A_PORT, DEC_A_PIN, GPIO_PIN_RESET); /* A = 0 */
      break;
    case 1: /* Drive */
      HAL_GPIO_WritePin(DEC_B_PORT, DEC_B_PIN, GPIO_PIN_RESET); /* B = 0 */
      HAL_GPIO_WritePin(DEC_A_PORT, DEC_A_PIN, GPIO_PIN_SET);   /* A = 1 */
      break;
    case 2: /* Sport */
      HAL_GPIO_WritePin(DEC_B_PORT, DEC_B_PIN, GPIO_PIN_SET);   /* B = 1 */
      HAL_GPIO_WritePin(DEC_A_PORT, DEC_A_PIN, GPIO_PIN_RESET); /* A = 0 */
      break;
    case 3: /* Reverse */
      HAL_GPIO_WritePin(DEC_B_PORT, DEC_B_PIN, GPIO_PIN_SET);   /* B = 1 */
      HAL_GPIO_WritePin(DEC_A_PORT, DEC_A_PIN, GPIO_PIN_SET);   /* A = 1 */
      break;
    default:
      break;
  }
  
  /* Enable Decoder (ENABLE_G = Low) */
  HAL_GPIO_WritePin(DEC_EN_PORT, DEC_EN_PIN, GPIO_PIN_RESET);
  
  /* Mirror Selector A and B state to LEDs / debug signals */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, (targetGear != 0) ? GPIO_PIN_RESET : GPIO_PIN_SET); /* LED Active when not in neutral */
  
  /* Write value to DAC */
  writeMCP4725(dacVal);
  
  faultFlags &= ~(1 << 0); /* Clear ESTOP Active flag */
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0)
  {
    FDCAN_RxHeaderTypeDef rxHeader;
    uint8_t rxData[8];
    
    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK)
    {
      /* 1. Check ID 0x204 (Drive Command) */
      if (rxHeader.Identifier == 0x204)
      {
        uint32_t data0 = ((uint32_t)rxData[0] << 24) | ((uint32_t)rxData[1] << 16) | ((uint32_t)rxData[2] << 8) | rxData[3];
        uint32_t data1 = ((uint32_t)rxData[4] << 24) | ((uint32_t)rxData[5] << 16) | ((uint32_t)rxData[6] << 8) | rxData[7];

        int32_t speed = (int32_t)data0; 
        uint8_t gear = (uint8_t)((data1 >> 28) & 0x0F);    /* High nibble of Byte 4 (bits 39-36) */
        uint8_t rollCnt = (uint8_t)((data1 >> 24) & 0x0F); /* Low nibble of Byte 4 (bits 35-32) */
        uint16_t tqLimit = (uint16_t)((data1 >> 8) & 0xFFFF);
        uint8_t checksum = (uint8_t)(data1 & 0xFF);

        /* VCU Drive Command Checksum Validation */
        uint8_t calculatedChecksum = 0;
        calculatedChecksum += (uint8_t)((speed >> 24) & 0xFF);
        calculatedChecksum += (uint8_t)((speed >> 16) & 0xFF);
        calculatedChecksum += (uint8_t)((speed >> 8) & 0xFF);
        calculatedChecksum += (uint8_t)(speed & 0xFF);
        calculatedChecksum += (uint8_t)((data1 >> 24) & 0xFF);
        calculatedChecksum += (uint8_t)((data1 >> 16) & 0xFF);
        calculatedChecksum += (uint8_t)((data1 >> 8) & 0xFF);

        if (checksum == calculatedChecksum)
        {
          targetSpeedMms = speed;
          targetGear = gear;
          driveRollCnt = rollCnt;
          torqueLimitNm = tqLimit;
          
          firstMsgReceived = 1;
          lastRxTimeMs = systemTimeMs; /* Reset watchdog timer */
          
          /* Auto-recover from watchdog command timeout once communication resumes */
          if (currentMode == MODE_ESTOP && (faultFlags & (1 << 0)) == 0)
          {
            currentMode = MODE_AUTO;
            faultFlags &= ~(1 << 1); /* Clear Timeout Flag */
          }
          else if (currentMode != MODE_ESTOP)
          {
            currentMode = MODE_AUTO;
          }
          
          /* Toggle status LED (PC6) on each valid CAN command received */
          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_6);
        }
      }

      /* 2. Check ID 0x001 (SAFETY_ESTOP) */
      if (rxHeader.Identifier == 0x001)
      {
        uint8_t state = rxData[0];
        if (state == 0xFF)
        {
          currentMode = MODE_ESTOP;
        }
      }

      /* 3. Check ID 0x205 (Brake Command) */
      if (rxHeader.Identifier == 0x205)
      {
        uint16_t brakeReqRaw = ((uint16_t)rxData[0] << 8) | rxData[1];
        targetBrakePressure = ((float)brakeReqRaw * 0.05f) - 30.0f;
      }
    }
  }
}

static void transmitMotorFeedback(void)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8];

  txHeader.Identifier = 0x206;
  txHeader.IdType = FDCAN_STANDARD_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0;

  uint32_t data0 = ((uint32_t)actualSpeedMms << 16) | ((uint32_t)gearStateFeedback << 8) | (uint32_t)faultFlags;
  uint32_t data1 = ((uint32_t)motorTemp << 24) | ((uint32_t)controllerTemp << 16) | (uint32_t)(motorCurrentA & 0xFFFF);

  txData[0] = (uint8_t)(data0 >> 24);
  txData[1] = (uint8_t)(data0 >> 16);
  txData[2] = (uint8_t)(data0 >> 8);
  txData[3] = (uint8_t)(data0 & 0xFF);
  txData[4] = (uint8_t)(data1 >> 24);
  txData[5] = (uint8_t)(data1 >> 16);
  txData[6] = (uint8_t)(data1 >> 8);
  txData[7] = (uint8_t)(data1 & 0xFF);

  /* Wait for free Tx FIFO buffer with safety timeout */
  uint32_t wait_timeout = 2000;
  while ((hfdcan1.Instance->TXFQS & FDCAN_TXFQS_TFQF) != 0 && wait_timeout > 0)
  {
    wait_timeout--;
  }

  HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txHeader, txData);
}

static void transmitSystemHeartbeat(void)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8];

  txHeader.Identifier = 0x7FE;
  txHeader.IdType = FDCAN_STANDARD_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0;

  uint32_t uptimeSeconds = systemTimeMs / 1000;
  uint32_t data0 = ((uint32_t)heartbeatAliveCtr << 24);
  uint32_t data1 = uptimeSeconds;

  txData[0] = (uint8_t)(data0 >> 24);
  txData[1] = (uint8_t)(data0 >> 16);
  txData[2] = (uint8_t)(data0 >> 8);
  txData[3] = (uint8_t)(data0 & 0xFF);
  txData[4] = (uint8_t)(data1 >> 24);
  txData[5] = (uint8_t)(data1 >> 16);
  txData[6] = (uint8_t)(data1 >> 8);
  txData[7] = (uint8_t)(data1 & 0xFF);

  /* Wait for free Tx FIFO buffer with safety timeout */
  uint32_t wait_timeout = 2000;
  while ((hfdcan1.Instance->TXFQS & FDCAN_TXFQS_TFQF) != 0 && wait_timeout > 0)
  {
    wait_timeout--;
  }

  HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txHeader, txData);
  
  heartbeatAliveCtr++;
  
  /* Send scanned I2C address for debugging */
  transmitDebugI2C();
}

static void scanI2CBus(void)
{
  /* Scan standard 7-bit addresses 0x08 to 0x77 */
  for (uint8_t addr = 0x08; addr <= 0x77; addr++)
  {
    sw_i2c_start();
    if (sw_i2c_write_byte(addr << 1))
    {
      detectedI2CAddr = addr;
      sw_i2c_stop();
      break; /* Store found address and exit */
    }
    sw_i2c_stop();
    HAL_Delay(1);
  }
}

static void transmitDebugI2C(void)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8] = {0};

  txHeader.Identifier = 0x700;
  txHeader.IdType = FDCAN_STANDARD_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0;

  txData[0] = detectedI2CAddr;

  /* Wait for free Tx FIFO buffer with safety timeout */
  uint32_t wait_timeout = 2000;
  while ((hfdcan1.Instance->TXFQS & FDCAN_TXFQS_TFQF) != 0 && wait_timeout > 0)
  {
    wait_timeout--;
  }

  HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &txHeader, txData);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
