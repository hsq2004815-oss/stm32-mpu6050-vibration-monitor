#include "alarm.h"

// 全局变量定义
AlarmStatus_t alarm_status = {0};
AlarmConfig_t alarm_config = {
    .angle_threshold_x = ALARM_ANGLE_THRESHOLD_X,
    .angle_threshold_y = ALARM_ANGLE_THRESHOLD_Y,
    .vibration_threshold = ALARM_VIBRATION_THRESHOLD,
    .temperature_threshold = ALARM_TEMPERATURE_THRESHOLD,
    .wifi_ready = 0
};

// 报警模块初始化
void ALARM_Init(void)
{
    // 初始化报警状态
    memset(&alarm_status, 0, sizeof(AlarmStatus_t));
    
    // GPIO初始化已经在gpio.c的MX_GPIO_Init()中完成
    // 这里只需要确保引脚初始状态正确
    
    // 确保LED和蜂鸣器初始状态为关闭
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);  // 报警LED
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);  // 蜂鸣器
    
    printf("Alarm Module Initialized\n");
    printf("Thresholds - Angle: %.1f°/%.1f°, Vibration: %.3fg, Temp: %.1f°C\n",
           alarm_config.angle_threshold_x, alarm_config.angle_threshold_y,
           alarm_config.vibration_threshold, alarm_config.temperature_threshold);
}

// 报警检测函数
void ALARM_Check(MPU6050_t *data)
{
    uint8_t current_alarm = ALARM_NONE;
    uint8_t alarm_count = 0;
    
    // 检查各项指标
    if (fabs(data->KalmanAngleX) > alarm_config.angle_threshold_x) {
        current_alarm = ALARM_ANGLE_X;
        alarm_count++;
    }
    if (fabs(data->KalmanAngleY) > alarm_config.angle_threshold_y) {
        if (alarm_count > 0) current_alarm = ALARM_MULTIPLE;
        else current_alarm = ALARM_ANGLE_Y;
        alarm_count++;
    }
    if (data->Vibration_Energy > alarm_config.vibration_threshold) {
        if (alarm_count > 0) current_alarm = ALARM_MULTIPLE;
        else current_alarm = ALARM_VIBRATION;
        alarm_count++;
    }
    if (data->Temperature > alarm_config.temperature_threshold) {
        if (alarm_count > 0) current_alarm = ALARM_MULTIPLE;
        else current_alarm = ALARM_TEMPERATURE;
        alarm_count++;
    }
    
    // 处理报警状态变化
    if (alarm_count > 0 && !alarm_status.is_triggered) {
        // 新报警触发
        alarm_status.is_triggered = 1;
        alarm_status.alarm_type = current_alarm;
        alarm_status.trigger_time = HAL_GetTick();
        alarm_status.alarm_count++;
        
        // 发送报警信息
        ALARM_Send_Notification(data, current_alarm);
        
        printf("! ALARM TRIGGERED: Type=%d, Count=%lu\n", 
               current_alarm, alarm_status.alarm_count);
    }
    else if (alarm_count == 0 && alarm_status.is_triggered) {
        // 报警解除
        alarm_status.is_triggered = 0;
        ALARM_Send_Clear();
        printf("! ALARM CLEARED\n");
    }
}

// 报警LED指示
void ALARM_Update_LED(void)
{
    if (alarm_status.is_triggered) {
        // 报警状态：快速闪烁 (200ms间隔)
        if (HAL_GetTick() - alarm_status.last_blink_time > 200) {
            alarm_status.last_blink_time = HAL_GetTick();
            alarm_status.led_blink_state = !alarm_status.led_blink_state;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, 
                             alarm_status.led_blink_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    } else {
        // 正常状态：LED熄灭
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
    }
}

// 蜂鸣器控制
void ALARM_Update_Buzzer(void)
{
    if (alarm_status.is_triggered) {
        // 报警状态：蜂鸣器间歇鸣响 (500ms周期)
        if (HAL_GetTick() - alarm_status.last_buzzer_time > 500) {
            alarm_status.last_buzzer_time = HAL_GetTick();
            alarm_status.buzzer_state = !alarm_status.buzzer_state;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, 
                             alarm_status.buzzer_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    } else {
        // 正常状态：蜂鸣器关闭
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
        alarm_status.buzzer_state = 0;
    }
}

// 发送报警通知
void ALARM_Send_Notification(MPU6050_t *data, AlarmType_t alarm_type)
{
    char alarm_buffer[128];
    const char* alarm_type_str = "";
    
    switch(alarm_type) {
        case ALARM_ANGLE_X:
            alarm_type_str = "ANGLE_X";
            break;
        case ALARM_ANGLE_Y:
            alarm_type_str = "ANGLE_Y";
            break;
        case ALARM_VIBRATION:
            alarm_type_str = "VIBRATION";
            break;
        case ALARM_TEMPERATURE:
            alarm_type_str = "TEMPERATURE";
            break;
        case ALARM_MULTIPLE:
            alarm_type_str = "MULTIPLE";
            break;
        default:
            alarm_type_str = "UNKNOWN";
    }
    
    snprintf(alarm_buffer, sizeof(alarm_buffer),
             "ALARM:%s|X:%.1f|Y:%.1f|Z:%.1f|VIB:%.3f|TEMP:%.1f\n",
             alarm_type_str,
             data->KalmanAngleX,
             data->KalmanAngleY,
             data->KalmanAngleZ,
             data->Vibration_Energy,
             data->Temperature);
    
    // 通过WiFi发送报警
    if (alarm_config.wifi_ready) {
        ESP8266_SendData(alarm_buffer);
    }
    
    // 同时通过串口发送（用于调试）
    printf("%s", alarm_buffer);
}

// 发送报警解除通知
void ALARM_Send_Clear(void)
{
    char clear_msg[32] = "ALARM:CLEAR\n";
    
    if (alarm_config.wifi_ready) {
        ESP8266_SendData(clear_msg);
    }
    printf("%s", clear_msg);
}

// 报警统计信息
void ALARM_Print_Statistics(void)
{
    static uint32_t last_stat_time = 0;
    if (HAL_GetTick() - last_stat_time > 60000) { // 每分钟打印一次统计
        last_stat_time = HAL_GetTick();
        printf("=== Alarm Statistics ===\n");
        printf("Total Alarms: %lu\n", alarm_status.alarm_count);
        printf("Current Thresholds: Angle=%.1f°/%.1f°, Vib=%.3fg, Temp=%.1f°C\n",
               alarm_config.angle_threshold_x, alarm_config.angle_threshold_y,
               alarm_config.vibration_threshold, alarm_config.temperature_threshold);
        printf("========================\n");
    }
}

// 设置报警阈值
void ALARM_Set_Thresholds(float angle_x, float angle_y, float vibration, float temp)
{
    alarm_config.angle_threshold_x = angle_x;
    alarm_config.angle_threshold_y = angle_y;
    alarm_config.vibration_threshold = vibration;
    alarm_config.temperature_threshold = temp;
    
    printf("Alarm thresholds updated: Angle=%.1f°/%.1f°, Vib=%.3fg, Temp=%.1f°C\n",
           angle_x, angle_y, vibration, temp);
}

// 获取当前报警阈值
void ALARM_Get_Thresholds(float *angle_x, float *angle_y, float *vibration, float *temp)
{
    if (angle_x) *angle_x = alarm_config.angle_threshold_x;
    if (angle_y) *angle_y = alarm_config.angle_threshold_y;
    if (vibration) *vibration = alarm_config.vibration_threshold;
    if (temp) *temp = alarm_config.temperature_threshold;
}
