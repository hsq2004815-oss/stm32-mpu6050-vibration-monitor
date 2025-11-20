#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QSerialPort>
#include <QTimer>
#include <QVector>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSoundEffect>
#include "deepseekanalyzer.h"


// 如果使用QCustomPlot，确保正确包含
#include "qcustomplot.h"

namespace Ui {
class Widget;
}

// 连接类型枚举
enum ConnectionType {
    CONNECTION_NONE = 0,
    CONNECTION_SERIAL,
    CONNECTION_WIFI
};

// 警报类型枚举
enum AlarmType {
    ALARM_NONE = 0,
    ALARM_ANGLE_X,
    ALARM_ANGLE_Y,
    ALARM_VIBRATION,
    ALARM_TEMPERATURE,
    ALARM_MULTIPLE
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_startButton_clicked();
    void on_stopButton_clicked();
    void on_vibrationButton_clicked();
    void readSerialData();
    void updateChart();
    void updateDisplay(float xAngle, float yAngle, float zAngle);

    // TCP服务器相关槽函数
    void newTCPConnection();
    void readTCPData();
    void clientDisconnected();

    // 警报相关槽函数
    void on_alarmTestButton_clicked();
    void on_alarmClearButton_clicked();
    void on_alarmSettingsButton_clicked();

    void on_connectSerialButton_clicked();  // 新增：连接串口按钮
    void on_refreshSerialButton_clicked();  // 新增：刷新串口列表


    void on_analyzeButton_clicked();
    void on_analyzeHistoricalButton_clicked();
    void on_deepSeekAnalysisComplete(const QString &result);
    void on_deepSeekAnalysisError(const QString &errorMessage);
    void on_deepSeekStatusChanged(const QString &status);
    void on_alarmSuggestion(const QString &suggestion, int severity);


private:

    DeepSeekAnalyzer *deepSeekAnalyzer;
    QPushButton *analyzeButton;
    QPushButton *analyzeHistoricalButton;
    QTextEdit *analysisResultTextEdit;
    QLineEdit *apiKeyLineEdit;
    QLabel *analysisStatusLabel;

    Ui::Widget *ui;
    QSerialPort *serialPort;
    QTimer *dataTimer;
    bool isCollecting;
    bool showVibration;

    // TCP服务器
    QTcpServer *tcpServer;
    QTcpSocket *tcpClient;

    // 连接状态
    ConnectionType currentConnectionType;

    // 警报相关成员
    bool isAlarmTriggered;
    AlarmType currentAlarmType;
    QTimer *alarmBlinkTimer;
    bool alarmBlinkState;
    QSoundEffect *alarmSound;

    // 警报阈值配置
    double alarmThresholdAngleX;
    double alarmThresholdAngleY;
    double alarmThresholdVibration;
    double alarmThresholdTemperature;

    // 警报统计
    int alarmCount;
    QDateTime alarmStartTime;

    // 数据存储
    QVector<double> timeData, angleDataX, angleDataY, angleDataZ, vibrationData;
    qint64 startTime;

    void setupChart();
    void setupSerialPort();
    void setupTCPServer();
    void setupAlarmSystem();
    void processDataLine(const QString &data, const QString &source);
    void processAlarmData(const QString &data);
    void updateConnectionStatus();
    void triggerAlarm(AlarmType alarmType, const QString &alarmMessage);
    void clearAlarm();
    void updateAlarmDisplay();
    void saveAlarmSettings();
    void loadAlarmSettings();
    void refreshSerialPortList();  // 新增：刷新串口列表函数
    void testSSLFix();
    void setupDeepSeekAnalyzer();  // 初始化DeepSeek分析器

    void performLocalAnalysis(double angleX, double angleY, double angleZ, double vibration);
};

#endif // WIDGET_H
