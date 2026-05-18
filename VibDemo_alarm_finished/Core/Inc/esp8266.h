#ifndef __ESP8266_H
#define __ESP8266_H

#include "main.h"
#include "mpu6050.h"

// 使用USART2与ESP8266通信
#define ESP8266_USART        &huart2
#define ESP8266_ATIMEOUT      5000

// WiFi配置 - 修改为您的网络设置
#define WIFI_SSID           "YOUR_WIFI_SSID"      // replace with your local WiFi SSID
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"  // replace with your local WiFi password

// TCP服务器配置 (QT上位机作为TCP服务器)
#define TCP_SERVER_IP       "192.168.1.100"  // replace with your Qt host machine IP
#define TCP_SERVER_PORT     "8080"           // default Qt TCP server port

// ESP8266状态枚举
typedef enum {
    ESP8266_OK = 0,
    ESP8266_ERROR,
    ESP8266_TIMEOUT,
    ESP8266_NO_RESPONSE
} ESP8266_Status;

// 函数声明
ESP8266_Status ESP8266_Init(void);
ESP8266_Status ESP8266_ConnectToServer(void);
ESP8266_Status ESP8266_SendMPUData(MPU6050_t *data);
uint8_t ESP8266_IsConnected(void);
ESP8266_Status ESP8266_SendData(const char* data);
ESP8266_Status ESP8266_CheckResponse(const char* expected_response, uint32_t timeout);

#endif

