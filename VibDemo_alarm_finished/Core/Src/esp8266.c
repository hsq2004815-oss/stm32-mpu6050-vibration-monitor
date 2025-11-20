#include "esp8266.h"
#include <string.h>
#include <stdio.h>

static uint8_t esp_response[512];
static uint8_t tcp_connected = 0;

// 清空串口接收缓冲区
static void ESP8266_FlushBuffer(void)
{
    uint8_t dummy;
    while (HAL_UART_Receive(ESP8266_USART, &dummy, 1, 10) == HAL_OK) {
        // 持续读取直到超时
    }
}

// 等待并检查响应
ESP8266_Status ESP8266_CheckResponse(const char* expected_response, uint32_t timeout)
{
    uint32_t start_time = HAL_GetTick();
    uint32_t received = 0;
    memset(esp_response, 0, sizeof(esp_response));
    
    while ((HAL_GetTick() - start_time) < timeout) {
        if (HAL_UART_Receive(ESP8266_USART, &esp_response[received], 1, 50) == HAL_OK) {
            received++;
            if (received >= sizeof(esp_response) - 1) break;
            
            // 检查是否收到预期响应
            if (strstr((char*)esp_response, expected_response) != NULL) {
                return ESP8266_OK;
            }
            
            // 检查错误响应
            if (strstr((char*)esp_response, "ERROR") != NULL || 
                strstr((char*)esp_response, "FAIL") != NULL) {
                printf("ESP Error: %s\r\n", esp_response);
                return ESP8266_ERROR;
            }
        }
    }
    
    if (strlen((char*)esp_response) > 0) {
        printf("ESP Timeout/Error, received: %s\r\n", esp_response);
    }
    return ESP8266_TIMEOUT;
}

// 发送AT命令并检查响应
static ESP8266_Status ESP8266_SendCommand(const char* cmd, const char* expected_response, uint32_t timeout)
{
    ESP8266_FlushBuffer(); // 清空缓冲区
    
    printf("Send: %s\r\n", cmd);
    
    // 发送命令
    if (HAL_UART_Transmit(ESP8266_USART, (uint8_t*)cmd, strlen(cmd), timeout) != HAL_OK) {
        return ESP8266_ERROR;
    }
    
    // 发送回车换行
    if (HAL_UART_Transmit(ESP8266_USART, (uint8_t*)"\r\n", 2, timeout) != HAL_OK) {
        return ESP8266_ERROR;
    }
    
    // 等待响应
    return ESP8266_CheckResponse(expected_response, timeout);
}

// ESP8266初始化
ESP8266_Status ESP8266_Init(void)
{
    printf("Initializing ESP8266...\r\n");
    HAL_Delay(2000); // 等待ESP8266启动
    
    // 1. 测试AT命令
    if (ESP8266_SendCommand("AT", "OK", 3000) != ESP8266_OK) {
        printf("ESP8266 not responding!\r\n");
        return ESP8266_NO_RESPONSE;
    }
    printf("ESP8266 AT OK\r\n");
    
    // 2. 设置为STA模式
    if (ESP8266_SendCommand("AT+CWMODE=1", "OK", 3000) != ESP8266_OK) {
        printf("Set STA mode failed!\r\n");
        return ESP8266_ERROR;
    }
    printf("STA mode set\r\n");
    
    // 3. 连接WiFi
    char wifi_cmd[128];
    snprintf(wifi_cmd, sizeof(wifi_cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);
    printf("Connecting to WiFi: %s\r\n", WIFI_SSID);
    
    if (ESP8266_SendCommand(wifi_cmd, "OK", 15000) != ESP8266_OK) {
        printf("WiFi connection failed!\r\n");
        return ESP8266_ERROR;
    }
    printf("WiFi connected\r\n");
    
    // 4. 设置为单连接模式
    if (ESP8266_SendCommand("AT+CIPMUX=0", "OK", 3000) != ESP8266_OK) {
        printf("Set single connection failed!\r\n");
        return ESP8266_ERROR;
    }
    printf("Single connection mode set\r\n");
    
    return ESP8266_OK;
}

// 连接到TCP服务器 (QT上位机)
ESP8266_Status ESP8266_ConnectToServer(void)
{
    char tcp_cmd[128];
    snprintf(tcp_cmd, sizeof(tcp_cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s", TCP_SERVER_IP, TCP_SERVER_PORT);
    printf("Connecting to TCP Server: %s:%s\r\n", TCP_SERVER_IP, TCP_SERVER_PORT);
    
    if (ESP8266_SendCommand(tcp_cmd, "CONNECT", 10000) != ESP8266_OK) {
        printf("TCP connection failed!\r\n");
        tcp_connected = 0;
        return ESP8266_ERROR;
    }
    
    printf("Connected to TCP server!\r\n");
    tcp_connected = 1;
    return ESP8266_OK;
}

// 检查TCP连接状态
uint8_t ESP8266_IsConnected(void)
{
    return tcp_connected;
}

// 发送数据到服务器
ESP8266_Status ESP8266_SendData(const char* data)
{
    char cmd[64];
    uint16_t data_len = strlen(data);
    
    if (!tcp_connected) {
        return ESP8266_ERROR;
    }
    
    // 发送数据长度命令
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", data_len);
    if (ESP8266_SendCommand(cmd, ">", 3000) != ESP8266_OK) {
        printf("Send prepare failed!\r\n");
        return ESP8266_ERROR;
    }
    
    // 发送实际数据
    if (HAL_UART_Transmit(ESP8266_USART, (uint8_t*)data, data_len, 3000) != HAL_OK) {
        printf("Data send failed!\r\n");
        return ESP8266_ERROR;
    }
    
    // 检查发送是否成功
    if (ESP8266_CheckResponse("SEND OK", 3000) != ESP8266_OK) {
        printf("Send confirm failed!\r\n");
        return ESP8266_ERROR;
    }
    
    return ESP8266_OK;
}

// 发送MPU6050数据 (QT格式) - 修改为返回状态
ESP8266_Status ESP8266_SendMPUData(MPU6050_t *data)
{
    char send_buffer[64];
    
    // 使用与串口相同的QT数据格式
    snprintf(send_buffer, sizeof(send_buffer),
             "QT:%d,%d,%d,%d\n", 
             (int16_t)(data->KalmanAngleX * 100),
             (int16_t)(data->KalmanAngleY * 100),
             (int16_t)(data->KalmanAngleZ * 100),
             (int16_t)(data->Vibration_Energy * 1000));
    
    return ESP8266_SendData(send_buffer);
}
