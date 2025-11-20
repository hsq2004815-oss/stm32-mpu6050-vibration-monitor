#ifndef __ALARM_H
#define __ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "mpu6050.h"
#include "esp8266.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Exported constants --------------------------------------------------------*/
// 报警阈值配置
#define ALARM_ANGLE_THRESHOLD_X       30.0f    // X轴角度阈值(度)
#define ALARM_ANGLE_THRESHOLD_Y       30.0f    // Y轴角度阈值(度)  
#define ALARM_VIBRATION_THRESHOLD     2.0f     // 振动能量阈值(g)
#define ALARM_TEMPERATURE_THRESHOLD   60.0f    // 温度阈值(℃)

/* Exported types ------------------------------------------------------------*/
// 报警类型枚举
typedef enum {
    ALARM_NONE = 0,
    ALARM_ANGLE_X,
    ALARM_ANGLE_Y, 
    ALARM_VIBRATION,
    ALARM_TEMPERATURE,
    ALARM_MULTIPLE
} AlarmType_t;

// 报警状态结构体
typedef struct {
    uint8_t is_triggered;
    AlarmType_t alarm_type;
    uint32_t trigger_time;
    uint32_t alarm_count;
    uint8_t led_blink_state;
    uint32_t last_blink_time;
    uint8_t buzzer_state;
    uint32_t last_buzzer_time;
} AlarmStatus_t;

// 报警配置结构体
typedef struct {
    float angle_threshold_x;
    float angle_threshold_y;
    float vibration_threshold;
    float temperature_threshold;
    uint8_t wifi_ready;  // 从主程序传递WiFi状态
} AlarmConfig_t;

/* Exported variables --------------------------------------------------------*/
extern AlarmStatus_t alarm_status;
extern AlarmConfig_t alarm_config;

/* Exported functions prototypes ---------------------------------------------*/
void ALARM_Init(void);
void ALARM_Check(MPU6050_t *data);
void ALARM_Update_LED(void);
void ALARM_Update_Buzzer(void);
void ALARM_Send_Notification(MPU6050_t *data, AlarmType_t alarm_type);
void ALARM_Send_Clear(void);
void ALARM_Print_Statistics(void);
void ALARM_Set_Thresholds(float angle_x, float angle_y, float vibration, float temp);
void ALARM_Get_Thresholds(float *angle_x, float *angle_y, float *vibration, float *temp);

#ifdef __cplusplus
}
#endif

#endif /* __ALARM_H */

