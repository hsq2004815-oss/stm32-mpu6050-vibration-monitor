#include "mpu6050.h"
#include <math.h>
#include <string.h>

// 常量定义
static const double ACCEL_SCALE_FACTOR = 16384.0;      // ±2g量程
static const double GYRO_SCALE_FACTOR = 131.0;         // ±250°/s量程  
static const double ACCEL_Z_CORRECTOR = 14418.0;
static const double TEMP_SCALE_FACTOR = 340.0;
static const double TEMP_OFFSET = 36.53;
static const double GYRO_Z_OFFSET = 2.5;               // 零偏校准值
static const uint16_t I2C_TIMEOUT = 100;

// 卡尔曼滤波器默认参数
static const Kalman_t KALMAN_DEFAULT = {
    .Q_angle = 0.001,
    .Q_bias = 0.003,
    .R_measure = 0.03,
    .angle = 0.0,
    .bias = 0.0,
    .P = {{0.0, 0.0}, {0.0, 0.0}}
};

uint8_t MPU6050_Init(I2C_HandleTypeDef *I2Cx)
{
    uint8_t check;
    uint8_t config_data;

    // 检查设备ID
    if (HAL_I2C_Mem_Read(I2Cx, MPU6050_ADDR, WHO_AM_I_REG, 1, &check, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }

    if (check != 0x68) {
        return 1;
    }

    // 电源管理 - 唤醒设备
    config_data = 0x00;
    if (HAL_I2C_Mem_Write(I2Cx, MPU6050_ADDR, PWR_MGMT_1_REG, 1, &config_data, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }
    HAL_Delay(100);

    // 设置采样率 (1kHz / (1 + 7) = 125Hz)
    config_data = 0x07;
    if (HAL_I2C_Mem_Write(I2Cx, MPU6050_ADDR, SMPLRT_DIV_REG, 1, &config_data, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }

    // 设置加速度计配置 (±2g)
    config_data = 0x00;
    if (HAL_I2C_Mem_Write(I2Cx, MPU6050_ADDR, ACCEL_CONFIG_REG, 1, &config_data, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }

    // 设置陀螺仪配置 (±250°/s)
    config_data = 0x00;
    if (HAL_I2C_Mem_Write(I2Cx, MPU6050_ADDR, GYRO_CONFIG_REG, 1, &config_data, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }

    // 配置DLPF (带宽 44Hz)
    config_data = 0x03;
    if (HAL_I2C_Mem_Write(I2Cx, MPU6050_ADDR, CONFIG_REG, 1, &config_data, 1, I2C_TIMEOUT) != HAL_OK) {
        return 1;
    }

    return 0;
}

void MPU6050_Process_Data(MPU6050_t *DataStruct, const uint8_t *raw_data)
{
    // 解析14字节原始数据
    DataStruct->Accel_X_RAW = (int16_t)((raw_data[0] << 8) | raw_data[1]);
    DataStruct->Accel_Y_RAW = (int16_t)((raw_data[2] << 8) | raw_data[3]);
    DataStruct->Accel_Z_RAW = (int16_t)((raw_data[4] << 8) | raw_data[5]);
    
    int16_t temp_raw = (int16_t)((raw_data[6] << 8) | raw_data[7]);
    
    DataStruct->Gyro_X_RAW = (int16_t)((raw_data[8] << 8) | raw_data[9]);
    DataStruct->Gyro_Y_RAW = (int16_t)((raw_data[10] << 8) | raw_data[11]);
    DataStruct->Gyro_Z_RAW = (int16_t)((raw_data[12] << 8) | raw_data[13]);

    // 转换为实际物理量
    DataStruct->Ax = DataStruct->Accel_X_RAW / ACCEL_SCALE_FACTOR;
    DataStruct->Ay = DataStruct->Accel_Y_RAW / ACCEL_SCALE_FACTOR;
    DataStruct->Az = DataStruct->Accel_Z_RAW / ACCEL_Z_CORRECTOR;
    
    DataStruct->Temperature = (float)temp_raw / TEMP_SCALE_FACTOR + TEMP_OFFSET;
    
    DataStruct->Gx = DataStruct->Gyro_X_RAW / GYRO_SCALE_FACTOR;
    DataStruct->Gy = DataStruct->Gyro_Y_RAW / GYRO_SCALE_FACTOR;
    DataStruct->Gz = DataStruct->Gyro_Z_RAW / GYRO_SCALE_FACTOR + GYRO_Z_OFFSET;

    // 陀螺仪零漂处理
    if (fabs(DataStruct->Gz) < 1.0) {
        DataStruct->Gz = 0.0;
    }

    // 计算时间间隔
    uint32_t current_time = HAL_GetTick();
    double dt = (current_time - DataStruct->timer) / 1000.0;
    DataStruct->timer = current_time;

    // 确保时间间隔合理
    if (dt <= 0.0 || dt > 1.0) {
        dt = 0.01; // 默认10ms
    }

    // 从加速度计计算滚转和俯仰角
    double roll_acc, pitch_acc;
    
    // 计算滚转角 (X轴)
    double denominator = sqrt(DataStruct->Accel_X_RAW * DataStruct->Accel_X_RAW + 
                             DataStruct->Accel_Z_RAW * DataStruct->Accel_Z_RAW);
    if (denominator > 0.0) {
        roll_acc = atan(DataStruct->Accel_Y_RAW / denominator) * RAD_TO_DEG;
    } else {
        roll_acc = 0.0;
    }
    
    // 计算俯仰角 (Y轴) - 使用atan2避免象限问题
    pitch_acc = atan2(-DataStruct->Accel_X_RAW, DataStruct->Accel_Z_RAW) * RAD_TO_DEG;

    // 卡尔曼滤波处理俯仰角
    if ((pitch_acc < -90.0 && DataStruct->KalmanAngleY > 90.0) || 
        (pitch_acc > 90.0 && DataStruct->KalmanAngleY < -90.0)) {
        // 角度突变，重置滤波器
        DataStruct->KalmanY.angle = pitch_acc;
        DataStruct->KalmanAngleY = pitch_acc;
    } else {
        DataStruct->KalmanAngleY = Kalman_getAngle(&DataStruct->KalmanY, pitch_acc, DataStruct->Gy, dt);
    }
    
    // 当俯仰角超过±90度时，补偿陀螺仪读数
    if (fabs(DataStruct->KalmanAngleY) > 90.0) {
        DataStruct->Gx = -DataStruct->Gx;
    }
    
    // 卡尔曼滤波处理滚转角
    DataStruct->KalmanAngleX = Kalman_getAngle(&DataStruct->KalmanX, roll_acc, DataStruct->Gx, dt);
    
    // Yaw角度估计（陀螺仪积分）
    DataStruct->yaw_angle += DataStruct->Gz * dt;
    
    // 限制yaw角度在0-360度范围内
    if (DataStruct->yaw_angle >= 360.0) {
        DataStruct->yaw_angle -= 360.0;
    } else if (DataStruct->yaw_angle < 0.0) {
        DataStruct->yaw_angle += 360.0;
    }
    
    DataStruct->KalmanAngleZ = DataStruct->yaw_angle;
		
		// 添加振动能量计算
    MPU6050_Calculate_Vibration(DataStruct);
}

double Kalman_getAngle(Kalman_t *Kalman, double newAngle, double newRate, double dt)
{
    // 预测步骤
    double rate = newRate - Kalman->bias;
    Kalman->angle += dt * rate;

    // 更新预测误差协方差
    Kalman->P[0][0] += dt * (dt * Kalman->P[1][1] - Kalman->P[0][1] - Kalman->P[1][0] + Kalman->Q_angle);
    Kalman->P[0][1] -= dt * Kalman->P[1][1];
    Kalman->P[1][0] -= dt * Kalman->P[1][1];
    Kalman->P[1][1] += Kalman->Q_bias * dt;

    // 计算卡尔曼增益
    double S = Kalman->P[0][0] + Kalman->R_measure;
    double K[2];
    K[0] = Kalman->P[0][0] / S;
    K[1] = Kalman->P[1][0] / S;

    // 更新步骤
    double y = newAngle - Kalman->angle; // 测量残差
    Kalman->angle += K[0] * y;
    Kalman->bias += K[1] * y;

    // 更新估计误差协方差
    double P00_temp = Kalman->P[0][0];
    double P01_temp = Kalman->P[0][1];
    
    Kalman->P[0][0] -= K[0] * P00_temp;
    Kalman->P[0][1] -= K[0] * P01_temp;
    Kalman->P[1][0] -= K[1] * P00_temp;
    Kalman->P[1][1] -= K[1] * P01_temp;

    return Kalman->angle;
}

void MPU6050_Read_All(I2C_HandleTypeDef *I2Cx, MPU6050_t *DataStruct)
{
    uint8_t raw_data[14];
    
    if (HAL_I2C_Mem_Read(I2Cx, MPU6050_ADDR, ACCEL_XOUT_H_REG, 1, raw_data, 14, I2C_TIMEOUT) == HAL_OK) {
        MPU6050_Process_Data(DataStruct, raw_data);
    }
}

void MPU6050_Reset_Yaw(MPU6050_t *DataStruct)
{
    DataStruct->yaw_angle = 0.0;
    DataStruct->KalmanAngleZ = 0.0;
}

// 初始化MPU6050数据结构体
void MPU6050_DataStruct_Init(MPU6050_t *DataStruct)
{
    if (DataStruct == NULL) return;
    
    memset(DataStruct, 0, sizeof(MPU6050_t));
    
    // 初始化卡尔曼滤波器
    DataStruct->KalmanX = KALMAN_DEFAULT;
    DataStruct->KalmanY = KALMAN_DEFAULT;  
    DataStruct->KalmanZ = KALMAN_DEFAULT;
    
    DataStruct->timer = HAL_GetTick();
}

// 计算振动能量
// 在mpu6050.c中修改振动计算函数
void MPU6050_Calculate_Vibration(MPU6050_t *DataStruct)
{
    static float last_ax = 0, last_ay = 0, last_az = 0;
    static uint32_t last_vibration_time = 0;
    
    uint32_t current_time = HAL_GetTick();
    if (current_time - last_vibration_time < 10) {
        return;
    }
    last_vibration_time = current_time;
    
    // 使用高通滤波：检测加速度的变化率，而不是绝对值
    float alpha = 0.8; // 高通滤波系数
    
    static float filtered_ax = 0, filtered_ay = 0, filtered_az = 0;
    
    // 高通滤波去除重力分量
    filtered_ax = alpha * (filtered_ax + DataStruct->Ax - last_ax);
    filtered_ay = alpha * (filtered_ay + DataStruct->Ay - last_ay);
    filtered_az = alpha * (filtered_az + DataStruct->Az - last_az);
    
    // 更新上一次的加速度值
    last_ax = DataStruct->Ax;
    last_ay = DataStruct->Ay;
    last_az = DataStruct->Az;
    
    // 计算振动能量（使用滤波后的数据）
    float vib_x = fabs(filtered_ax);
    float vib_y = fabs(filtered_ay);
    float vib_z = fabs(filtered_az);
    
    // 设置振动阈值，忽略微小变化
    float threshold = 0.02;
    if (vib_x < threshold) vib_x = 0;
    if (vib_y < threshold) vib_y = 0;
    if (vib_z < threshold) vib_z = 0;
    
    DataStruct->Vibration_X = vib_x;
    DataStruct->Vibration_Y = vib_y;
    DataStruct->Vibration_Z = vib_z;
    
    // 计算总振动能量
    DataStruct->Vibration_Energy = sqrt(vib_x*vib_x + vib_y*vib_y + vib_z*vib_z);
}

