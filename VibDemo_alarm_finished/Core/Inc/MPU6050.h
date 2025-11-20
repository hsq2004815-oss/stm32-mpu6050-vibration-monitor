#ifndef MPU6050_H
#define MPU6050_H

#include "main.h"
#include <stdint.h>

// MPU6050寄存器地址定义
#define MPU6050_ADDR                0xD0
#define WHO_AM_I_REG                0x75
#define PWR_MGMT_1_REG              0x6B
#define SMPLRT_DIV_REG              0x19
#define ACCEL_CONFIG_REG            0x1C
#define GYRO_CONFIG_REG             0x1B
#define CONFIG_REG                  0x1A
#define ACCEL_XOUT_H_REG            0x3B

#define RAD_TO_DEG                  (180.0 / M_PI)
#define M_PI                        3.14159265358979323846

// 卡尔曼滤波器结构体
typedef struct {
    double Q_angle;
    double Q_bias;
    double R_measure;
    double angle;
    double bias;
    double P[2][2];
} Kalman_t;

// MPU6050数据结构体
typedef struct {
    // 原始数据
    int16_t Accel_X_RAW;
    int16_t Accel_Y_RAW;
    int16_t Accel_Z_RAW;
    int16_t Gyro_X_RAW;
    int16_t Gyro_Y_RAW;
    int16_t Gyro_Z_RAW;
    
    // 处理后的物理量
    double Ax, Ay, Az;
    double Gx, Gy, Gz;
    float Temperature;
    
    // 卡尔曼滤波角度
    double KalmanAngleX;
    double KalmanAngleY;
    double KalmanAngleZ;
    
    // 内部状态
    Kalman_t KalmanX;
    Kalman_t KalmanY;
    Kalman_t KalmanZ;
    uint32_t timer;
    double yaw_angle;
		
		// 振动能量相关
    float Vibration_Energy;      // 总振动能量
    float Vibration_X;           // X轴振动能量
    float Vibration_Y;           // Y轴振动能量  
    float Vibration_Z;           // Z轴振动能量
    float Acceleration_Magnitude;// 加速度幅值
    
    // 振动分析缓冲区
    float accel_buffer[3][32];   // 三轴加速度缓冲区
    uint8_t buffer_index;        // 缓冲区索引
    uint32_t vibration_timer;    // 振动分析计时器
} MPU6050_t;

#define INT_ENABLE_REG 0x38


typedef enum {
    MOTION_STATIONARY,
    MOTION_SLOW, 
    MOTION_FAST
} MotionState_t;

// ??????
void MPU6050_Calculate_Angles_Quaternion(MPU6050_t *DataStruct, float dt);
MotionState_t MPU6050_Detect_Motion(MPU6050_t *DataStruct);
void MPU6050_Calibrate(I2C_HandleTypeDef *I2Cx, MPU6050_t *DataStruct);
float MPU6050_Get_Temperature(MPU6050_t *DataStruct);
uint8_t MPU6050_Is_Healthy(I2C_HandleTypeDef *I2Cx);
void MPU6050_Calculate_Angles_Complementary(MPU6050_t *DataStruct, float dt);
// 函数声明
uint8_t MPU6050_Init(I2C_HandleTypeDef *I2Cx);
void MPU6050_Process_Data(MPU6050_t *DataStruct, const uint8_t *raw_data);
void MPU6050_Read_All(I2C_HandleTypeDef *I2Cx, MPU6050_t *DataStruct);
void MPU6050_Reset_Yaw(MPU6050_t *DataStruct);
double Kalman_getAngle(Kalman_t *Kalman, double newAngle, double newRate, double dt);
void MPU6050_DataStruct_Init(MPU6050_t *DataStruct);
void MPU6050_Calculate_Vibration(MPU6050_t *DataStruct);
#endif
