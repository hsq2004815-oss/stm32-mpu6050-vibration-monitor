#include "main.h"
#include "mpu6050.h"
#include "esp8266.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>

// 外部声明已经在globals.h中定义的变量
extern int16_t rawBuf[2][7];
extern uint8_t bufIdx;
extern uint8_t bufFull;

// MPU6050数据实例
MPU6050_t mpu_data;

// ESP8266状态标志
uint8_t wifi_ready = 0;

// 函数声明
void SystemClock_Config(void);
void Error_Handler(void);
void sendQtData(MPU6050_t *data);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();  // ESP8266使用的串口

    // 等待系统稳定
    HAL_Delay(1000);
    
    printf("\r\n=== MPU6050 with WiFi Transmission ===\r\n");
    
    // 初始化MPU6050数据结构
    MPU6050_DataStruct_Init(&mpu_data);
    
    // 初始化MPU6050硬件
    if(MPU6050_Init(&hi2c1) != 0) {
        printf("MPU6050 Init Failed!\r\n");
        Error_Handler();
    }
    printf("MPU6050 Init Success!\r\n");
    
    // 初始化ESP8266
    printf("Initializing WiFi...\r\n");
    if(ESP8266_Init() == ESP8266_OK) {
        printf("? WiFi initialized successfully!\r\n");
        
        // 连接到TCP服务器
        printf("Connecting to TCP server...\r\n");
        if(ESP8266_ConnectToServer() == ESP8266_OK) {
            wifi_ready = 1;
            printf("? Connected to TCP server successfully!\r\n");
            printf("? Data will be sent via WiFi to %s:%s\r\n", TCP_SERVER_IP, TCP_SERVER_PORT);
        } else {
            wifi_ready = 0;
            printf("? TCP Connection Failed! Data will be sent via serial only.\r\n");
        }
    } else {
        wifi_ready = 0;
        printf("? ESP8266 Init Failed! Data will be sent via serial only.\r\n");
    }
    
    printf("X-Angle(°)  Y-Angle(°)  Z-Angle(°)  Vib-Total(g)  Temp(°C)\r\n");
    printf("------------------------------------------------------------\r\n");
    
    // 启动DMA读取
    if(HAL_I2C_Mem_Read_DMA(&hi2c1, MPU6050_ADDR, ACCEL_XOUT_H_REG, I2C_MEMADD_SIZE_8BIT,
                           (uint8_t*)rawBuf[bufIdx], 14) != HAL_OK) {
        printf("DMA Start Failed!\r\n");
        Error_Handler();
    }
    
    printf("DMA started successfully\r\n");
    
    uint32_t last_print_time = 0;
    uint32_t data_count = 0;
    uint32_t last_data_time = HAL_GetTick();
    uint32_t wifi_reconnect_time = 0;
    char output_buffer[128];
    
    while(1) {
        if(bufFull) {
            bufFull = 0;
            data_count++;
            
            // 处理MPU6050数据
            MPU6050_Process_Data(&mpu_data, (uint8_t*)rawBuf[1-bufIdx]);
            
            // 每200ms输出一次数据
            if(HAL_GetTick() - last_print_time > 200) {
                last_print_time = HAL_GetTick();
                
                // 计算实际数据率
                uint32_t current_time = HAL_GetTick();
                uint32_t time_diff = current_time - last_data_time;
                float actual_rate = (time_diff > 0) ? (data_count * 1000.0f / time_diff) : 0;
                last_data_time = current_time;
                data_count = 0;
                
                // 格式化输出
                snprintf(output_buffer, sizeof(output_buffer),
                        "%8.2f   %8.2f   %8.2f   %11.3f   %7.1f",
                        mpu_data.KalmanAngleX,
                        mpu_data.KalmanAngleY,  
                        mpu_data.KalmanAngleZ,
                        mpu_data.Vibration_Energy,
                        mpu_data.Temperature);
                
                printf("%s\r\n", output_buffer);
                
                // 通过串口发送到Qt（保持原有功能）
                sendQtData(&mpu_data);
                
                // 通过WiFi发送数据到QT上位机
                if(wifi_ready) {
                    if(ESP8266_SendMPUData(&mpu_data) == ESP8266_OK) {
                        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_7);  // WiFi发送指示灯
                    } else {
                        printf("WiFi send failed, connection lost\r\n");
                        wifi_ready = 0;
                    }
                }
            }
            
            // 检查WiFi连接状态，如果断开尝试重连（每10秒尝试一次）
            if(!wifi_ready && (HAL_GetTick() - wifi_reconnect_time > 10000)) {
                wifi_reconnect_time = HAL_GetTick();
                printf("Attempting WiFi reconnection...\r\n");
                
                if(ESP8266_ConnectToServer() == ESP8266_OK) {
                    wifi_ready = 1;
                    printf("? WiFi reconnected successfully!\r\n");
                } else {
                    printf("? WiFi reconnection failed, will retry in 10 seconds\r\n");
                }
            }
            
            // 启动下一次DMA读取
            if(HAL_I2C_Mem_Read_DMA(&hi2c1, MPU6050_ADDR, ACCEL_XOUT_H_REG, I2C_MEMADD_SIZE_8BIT,
                                   (uint8_t*)rawBuf[bufIdx], 14) != HAL_OK) {
                printf("DMA Restart Failed!\r\n");
                HAL_Delay(10);
            }
        }
        
        // 处理其他任务
        HAL_Delay(1);
    }
}

// DMA完成回调函数
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if(hi2c->Instance == I2C1){
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6);  // DMA活动指示灯
        bufFull = 1;
        bufIdx = 1 - bufIdx;  // 切换缓冲区
    }
}

// DMA错误回调函数
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if(hi2c->Instance == I2C1) {
        uint32_t error = HAL_I2C_GetError(hi2c);
        printf("I2C DMA Error: 0x%02lX\r\n", error);
        
        // 清除错误标志
        __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
        
        // 短暂延迟后重新启动DMA
        HAL_Delay(50);
        if(HAL_I2C_Mem_Read_DMA(&hi2c1, MPU6050_ADDR, ACCEL_XOUT_H_REG, I2C_MEMADD_SIZE_8BIT,
                               (uint8_t*)rawBuf[bufIdx], 14) == HAL_OK) {
            printf("DMA Restarted after error\r\n");
        }
    }
}

void sendQtData(MPU6050_t *data) {
    // 直接发送角度数据，乘以100保留2位小数精度
    printf("QT:%d,%d,%d,%d\n", 
           (int16_t)(data->KalmanAngleX * 100),
           (int16_t)(data->KalmanAngleY * 100),  
           (int16_t)(data->KalmanAngleZ * 100),
           (int16_t)(data->Vibration_Energy * 1000)
    );
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** Initializes the RCC Oscillators according to the specified parameters */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM7) {
        HAL_IncTick();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(100);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    printf("Assert failed: file %s on line %d\r\n", file, line);
}
#endif