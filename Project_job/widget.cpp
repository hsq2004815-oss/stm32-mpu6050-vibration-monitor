#include "widget.h"
#include "ui_widget.h"
#include <QMessageBox>
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <QSettings>
#include <QFileDialog>
#include <QSerialPortInfo>
Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget),
    serialPort(new QSerialPort(this)),
    dataTimer(new QTimer(this)),
    isCollecting(false),
    showVibration(false),
    tcpServer(new QTcpServer(this)),
    tcpClient(nullptr),
    currentConnectionType(CONNECTION_NONE),
    isAlarmTriggered(false),
    currentAlarmType(ALARM_NONE),
    alarmBlinkTimer(new QTimer(this)),
    alarmBlinkState(false),
    alarmSound(new QSoundEffect(this)),
    alarmThresholdAngleX(30.0),
    alarmThresholdAngleY(30.0),
    alarmThresholdVibration(2.0),
    alarmThresholdTemperature(60.0),
    alarmCount(0),
    deepSeekAnalyzer(new DeepSeekAnalyzer(this))
{
    ui->setupUi(this);

    // 初始化数据向量
    timeData.clear();
    angleDataX.clear();
    angleDataY.clear();
    angleDataZ.clear();
    vibrationData.clear();

    setupChart();
    setupTCPServer();
    setupAlarmSystem();

    // 初始化串口相关 - 不再自动弹出选择对话框
    connect(serialPort, &QSerialPort::readyRead, this, &Widget::readSerialData);
    connect(dataTimer, &QTimer::timeout, this, &Widget::updateChart);

    // 初始化串口列表
    refreshSerialPortList();

    ui->stopButton->setEnabled(false);
    updateConnectionStatus();

    // 加载保存的警报设置
    loadAlarmSettings();

    // 初始化DeepSeek分析器
    setupDeepSeekAnalyzer();
}

Widget::~Widget()
{
    if (serialPort->isOpen()) {
        serialPort->close();
    }
    if (tcpServer->isListening()) {
        tcpServer->close();
    }
    if (tcpClient) {
        tcpClient->close();
    }

    // 保存警报设置
    saveAlarmSettings();

    delete ui;
}
void Widget::setupTCPServer()
{
    // 启动TCP服务器，监听8080端口
    if (tcpServer->listen(QHostAddress::Any, 8080)) {
        qDebug() << "TCP Server started on port 8080";
        connect(tcpServer, &QTcpServer::newConnection, this, &Widget::newTCPConnection);
        updateConnectionStatus();
    } else {
        qDebug() << "Failed to start TCP server:" << tcpServer->errorString();
        updateConnectionStatus();
        QMessageBox::warning(this, "TCP服务器错误",
                           QString("无法启动TCP服务器:\n%1\n\n请检查端口8080是否被其他程序占用。")
                           .arg(tcpServer->errorString()));
    }
}
void Widget::newTCPConnection()
{
    if (tcpClient) {
        // 如果已有客户端连接，拒绝新的连接
        QTcpSocket *newClient = tcpServer->nextPendingConnection();
        QString clientInfo = QString("%1:%2").arg(newClient->peerAddress().toString()).arg(newClient->peerPort());
        newClient->close();
        newClient->deleteLater();
        qDebug() << "WiFi客户端连接被拒绝 - 已有客户端连接:" << clientInfo;
        return;
    }

    tcpClient = tcpServer->nextPendingConnection();
    connect(tcpClient, &QTcpSocket::readyRead, this, &Widget::readTCPData);
    connect(tcpClient, &QTcpSocket::disconnected, this, &Widget::clientDisconnected);

    QString clientInfo = QString("%1:%2").arg(tcpClient->peerAddress().toString()).arg(tcpClient->peerPort());
    qDebug() << "新的WiFi客户端连接:" << clientInfo;
    currentConnectionType = CONNECTION_WIFI;
    updateConnectionStatus();

    // 提示用户
    QMessageBox::information(this, "设备连接",
                           QString("STM32设备已通过WiFi连接:\n%1").arg(clientInfo));
}
void Widget::setupAlarmSystem()
{
    // 设置警报声音
    alarmSound->setSource(QUrl::fromLocalFile(":/sounds/alarm.wav"));
    alarmSound->setLoopCount(QSoundEffect::Infinite);
    alarmSound->setVolume(0.5f);

    // 设置警报闪烁定时器
    connect(alarmBlinkTimer, &QTimer::timeout, this, [this]() {
        alarmBlinkState = !alarmBlinkState;
        updateAlarmDisplay();
    });

    // 初始化警报显示
    ui->alarmStatusLabel->setText("正常");
    ui->alarmStatusLabel->setStyleSheet("QLabel { background-color: green; color: white; padding: 5px; border-radius: 3px; }");
    ui->alarmCountLabel->setText("警报次数: 0");

    // 设置阈值显示
    updateAlarmDisplay();
}

void Widget::processAlarmData(const QString &data)
{
    if (data.startsWith("ALARM:")) {
        QString alarmContent = data.mid(6).trimmed();

        if (alarmContent == "CLEAR") {
            clearAlarm();
            return;
        }

        // 解析警报信息格式: "ALARM:TYPE|X:value|Y:value|Z:value|VIB:value|TEMP:value"
        QStringList parts = alarmContent.split('|');
        if (parts.size() >= 1) {
            QString alarmTypeStr = parts[0];
            AlarmType alarmType = ALARM_NONE;
            QString alarmMessage;

            if (alarmTypeStr == "ANGLE_X") {
                alarmType = ALARM_ANGLE_X;
                alarmMessage = "X轴角度超限";
            } else if (alarmTypeStr == "ANGLE_Y") {
                alarmType = ALARM_ANGLE_Y;
                alarmMessage = "Y轴角度超限";
            } else if (alarmTypeStr == "VIBRATION") {
                alarmType = ALARM_VIBRATION;
                alarmMessage = "振动能量超限";
            } else if (alarmTypeStr == "TEMPERATURE") {
                alarmType = ALARM_TEMPERATURE;
                alarmMessage = "温度超限";
            } else if (alarmTypeStr == "MULTIPLE") {
                alarmType = ALARM_MULTIPLE;
                alarmMessage = "多参数超限";
            }

            // 提取具体数值用于显示
            QString detailMessage = alarmMessage;
            for (int i = 1; i < parts.size(); ++i) {
                QStringList keyValue = parts[i].split(':');
                if (keyValue.size() == 2) {
                    detailMessage += QString(" | %1: %2").arg(keyValue[0]).arg(keyValue[1]);
                }
            }

            triggerAlarm(alarmType, detailMessage);
        }
    }
}

void Widget::triggerAlarm(AlarmType alarmType, const QString &alarmMessage)
{
    if (!isAlarmTriggered) {
        isAlarmTriggered = true;
        currentAlarmType = alarmType;
        alarmCount++;
        alarmStartTime = QDateTime::currentDateTime();

        // 启动警报效果
        alarmBlinkTimer->start(500); // 500ms闪烁间隔
        if (!alarmSound->source().isEmpty()) {
            alarmSound->play();
        }

        // 更新警报统计
        ui->alarmCountLabel->setText(QString("警报次数: %1").arg(alarmCount));

        // 记录警报日志
        QString logEntry = QString("[%1] %2").arg(alarmStartTime.toString("yyyy-MM-dd hh:mm:ss")).arg(alarmMessage);
        ui->alarmLogTextEdit->append(logEntry);

        // 自动滚动到底部
        QTextCursor cursor = ui->alarmLogTextEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        ui->alarmLogTextEdit->setTextCursor(cursor);

        qDebug() << "警报触发:" << alarmMessage;
    }

    // 更新当前警报显示
    ui->alarmStatusLabel->setText(alarmMessage);
}

void Widget::clearAlarm()
{
    if (isAlarmTriggered) {
        isAlarmTriggered = false;
        currentAlarmType = ALARM_NONE;

        // 停止警报效果
        alarmBlinkTimer->stop();
        alarmSound->stop();

        // 更新显示
        ui->alarmStatusLabel->setText("正常");
        ui->alarmStatusLabel->setStyleSheet("QLabel { background-color: green; color: white; padding: 5px; border-radius: 3px; }");

        // 记录警报解除日志
        QString logEntry = QString("[%1] 警报解除").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
        ui->alarmLogTextEdit->append(logEntry);

        qDebug() << "警报解除";
    }
}

void Widget::updateAlarmDisplay()
{
    if (isAlarmTriggered) {
        // 闪烁效果
        if (alarmBlinkState) {
            ui->alarmStatusLabel->setStyleSheet("QLabel { background-color: red; color: white; padding: 5px; border-radius: 3px; }");
        } else {
            ui->alarmStatusLabel->setStyleSheet("QLabel { background-color: darkred; color: white; padding: 5px; border-radius: 3px; }");
        }
    }

    // 更新阈值显示
    ui->angleXThresholdLabel->setText(QString("X角度阈值: %1°").arg(alarmThresholdAngleX));
    ui->angleYThresholdLabel->setText(QString("Y角度阈值: %1°").arg(alarmThresholdAngleY));
    ui->vibrationThresholdLabel->setText(QString("振动阈值: %1g").arg(alarmThresholdVibration));
    ui->temperatureThresholdLabel->setText(QString("温度阈值: %1°C").arg(alarmThresholdTemperature));
}

void Widget::on_alarmTestButton_clicked()
{
    // 测试警报功能
    triggerAlarm(ALARM_ANGLE_X, "测试警报 - X轴角度超限");

    QMessageBox::information(this, "警报测试", "警报测试已触发，检查警报指示灯和声音。");
}

void Widget::on_alarmClearButton_clicked()
{
    clearAlarm();
}

void Widget::on_alarmSettingsButton_clicked()
{
    // 创建警报设置对话框
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle("警报阈值设置");
    settingsDialog.setFixedSize(300, 250);

    QVBoxLayout *layout = new QVBoxLayout(&settingsDialog);

    // X角度阈值
    QHBoxLayout *layoutX = new QHBoxLayout();
    QLabel *labelX = new QLabel("X角度阈值 (°):");
    QDoubleSpinBox *spinX = new QDoubleSpinBox();
    spinX->setRange(0.1, 180.0);
    spinX->setValue(alarmThresholdAngleX);
    spinX->setSingleStep(1.0);
    layoutX->addWidget(labelX);
    layoutX->addWidget(spinX);

    // Y角度阈值
    QHBoxLayout *layoutY = new QHBoxLayout();
    QLabel *labelY = new QLabel("Y角度阈值 (°):");
    QDoubleSpinBox *spinY = new QDoubleSpinBox();
    spinY->setRange(0.1, 180.0);
    spinY->setValue(alarmThresholdAngleY);
    spinY->setSingleStep(1.0);
    layoutY->addWidget(labelY);
    layoutY->addWidget(spinY);

    // 振动阈值
    QHBoxLayout *layoutVib = new QHBoxLayout();
    QLabel *labelVib = new QLabel("振动阈值 (g):");
    QDoubleSpinBox *spinVib = new QDoubleSpinBox();
    spinVib->setRange(0.1, 10.0);
    spinVib->setValue(alarmThresholdVibration);
    spinVib->setSingleStep(0.1);
    layoutVib->addWidget(labelVib);
    layoutVib->addWidget(spinVib);

    // 温度阈值
    QHBoxLayout *layoutTemp = new QHBoxLayout();
    QLabel *labelTemp = new QLabel("温度阈值 (°C):");
    QDoubleSpinBox *spinTemp = new QDoubleSpinBox();
    spinTemp->setRange(0.1, 100.0);
    spinTemp->setValue(alarmThresholdTemperature);
    spinTemp->setSingleStep(1.0);
    layoutTemp->addWidget(labelTemp);
    layoutTemp->addWidget(spinTemp);

    // 按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton("确定");
    QPushButton *cancelButton = new QPushButton("取消");
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    // 添加到主布局
    layout->addLayout(layoutX);
    layout->addLayout(layoutY);
    layout->addLayout(layoutVib);
    layout->addLayout(layoutTemp);
    layout->addLayout(buttonLayout);

    // 连接按钮信号
    connect(okButton, &QPushButton::clicked, &settingsDialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &settingsDialog, &QDialog::reject);

    // 显示对话框
    if (settingsDialog.exec() == QDialog::Accepted) {
        alarmThresholdAngleX = spinX->value();
        alarmThresholdAngleY = spinY->value();
        alarmThresholdVibration = spinVib->value();
        alarmThresholdTemperature = spinTemp->value();

        updateAlarmDisplay();
        saveAlarmSettings();

        QMessageBox::information(this, "设置保存", "警报阈值设置已保存。");
    }
}

void Widget::saveAlarmSettings()
{
    QSettings settings("MyCompany", "MPU6050Monitor");
    settings.setValue("alarmThresholdAngleX", alarmThresholdAngleX);
    settings.setValue("alarmThresholdAngleY", alarmThresholdAngleY);
    settings.setValue("alarmThresholdVibration", alarmThresholdVibration);
    settings.setValue("alarmThresholdTemperature", alarmThresholdTemperature);
}

void Widget::loadAlarmSettings()
{
    QSettings settings("MyCompany", "MPU6050Monitor");
    alarmThresholdAngleX = settings.value("alarmThresholdAngleX", 30.0).toDouble();
    alarmThresholdAngleY = settings.value("alarmThresholdAngleY", 30.0).toDouble();
    alarmThresholdVibration = settings.value("alarmThresholdVibration", 2.0).toDouble();
    alarmThresholdTemperature = settings.value("alarmThresholdTemperature", 60.0).toDouble();
}

// 在 processDataLine 函数中添加警报数据处理
void Widget::processDataLine(const QString &data, const QString &source)
{
    // 检查是否是警报数据
    if (data.startsWith("ALARM:")) {
        processAlarmData(data);
        return;
    }

    // 原有的传感器数据处理
    if (data.startsWith("QT:")) {
        QStringList values = data.mid(3).split(',');
        if (values.size() >= 4) {
            bool ok1, ok2, ok3, ok4;
            int16_t angleX = values[0].toInt(&ok1);
            int16_t angleY = values[1].toInt(&ok2);
            int16_t angleZ = values[2].toInt(&ok3);
            int16_t vibration = values[3].toInt(&ok4);

            if (ok1 && ok2 && ok3 && ok4) {
                qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
                double elapsedSeconds = (currentTime - startTime) / 1000.0;

                // 存储数据
                timeData.append(elapsedSeconds);
                angleDataX.append(angleX / 100.0);
                angleDataY.append(angleY / 100.0);
                angleDataZ.append(angleZ / 100.0);
                vibrationData.append(vibration / 1000.0);

                // 检查是否需要触发警报（本地检查）
                bool localAlarm = false;
                QString alarmMsg;

                if (fabs(angleX / 100.0) > alarmThresholdAngleX) {
                    localAlarm = true;
                    alarmMsg = QString("本地警报: X角度超限 (%1°)").arg(angleX / 100.0, 0, 'f', 1);
                } else if (fabs(angleY / 100.0) > alarmThresholdAngleY) {
                    localAlarm = true;
                    alarmMsg = QString("本地警报: Y角度超限 (%1°)").arg(angleY / 100.0, 0, 'f', 1);
                } else if (fabs(vibration / 1000.0) > alarmThresholdVibration) {
                    localAlarm = true;
                    alarmMsg = QString("本地警报: 振动超限 (%1g)").arg(vibration / 1000.0, 0, 'f', 3);
                }

                if (localAlarm && !isAlarmTriggered) {
                    triggerAlarm(ALARM_MULTIPLE, alarmMsg);
                }

                qDebug() << "收到" << source << "数据 - 时间:" << elapsedSeconds
                         << "X:" << (angleX / 100.0) << "°"
                         << "Y:" << (angleY / 100.0) << "°"
                         << "Z:" << (angleZ / 100.0) << "°"
                         << "振动:" << (vibration / 1000.0) << "g";

                // 更新显示
                updateDisplay(angleX / 100.0, angleY / 100.0, angleZ / 100.0);

                // 限制数据量
                if (timeData.size() > 1000) {
                    timeData.removeFirst();
                    angleDataX.removeFirst();
                    angleDataY.removeFirst();
                    angleDataZ.removeFirst();
                    vibrationData.removeFirst();
                }
            }
        }
    }
}

// 在 readSerialData 和 readTCPData 中确保能处理警报数据
void Widget::readSerialData()
{
    if (!isCollecting) return;

    static QByteArray buffer;

    while (serialPort->bytesAvailable()) {
        buffer.append(serialPort->readAll());

        int newlineIndex;
        while ((newlineIndex = buffer.indexOf('\n')) != -1) {
            QByteArray line = buffer.left(newlineIndex).trimmed();
            buffer = buffer.mid(newlineIndex + 1);

            if (!line.isEmpty()) {
                QString lineStr = QString::fromUtf8(line);
                // 处理传感器数据和警报数据
                processDataLine(lineStr, "串口");
            }
        }
    }
}


void Widget::readTCPData()
{
    if (!tcpClient || !isCollecting) return;

    while (tcpClient->bytesAvailable()) {
        QByteArray data = tcpClient->readAll();
        QString dataStr = QString::fromUtf8(data);

        // 按行分割处理数据 - 兼容性写法
        QStringList lines = dataStr.split('\n');
        for (const QString &line : lines) {
            QString trimmedLine = line.trimmed();
            if (!trimmedLine.isEmpty() && trimmedLine.startsWith("QT:")) {
                processDataLine(trimmedLine, "WiFi");
            }
        }
    }
}

void Widget::clientDisconnected()
{
    if (tcpClient) {
        QString clientInfo = QString("%1:%2").arg(tcpClient->peerAddress().toString()).arg(tcpClient->peerPort());
        qDebug() << "WiFi客户端断开连接:" << clientInfo;
        tcpClient->deleteLater();
        tcpClient = nullptr;
        currentConnectionType = CONNECTION_NONE;
        updateConnectionStatus();

        // 提示用户
        QMessageBox::information(this, "连接断开",
                               QString("设备连接已断开:\n%1").arg(clientInfo));
    }
}

void Widget::setupChart()
{
    // 设置图表 - 创建4个图表系列
    ui->customPlot->addGraph();
    ui->customPlot->graph(0)->setPen(QPen(Qt::blue, 2));
    ui->customPlot->graph(0)->setName("X轴角度");

    ui->customPlot->addGraph();
    ui->customPlot->graph(1)->setPen(QPen(Qt::red, 2));
    ui->customPlot->graph(1)->setName("Y轴角度");

    ui->customPlot->addGraph();
    ui->customPlot->graph(2)->setPen(QPen(Qt::green, 2));
    ui->customPlot->graph(2)->setName("Z轴角度");

    ui->customPlot->addGraph();
    ui->customPlot->graph(3)->setPen(QPen(Qt::magenta, 2));
    ui->customPlot->graph(3)->setName("振动能量");
    ui->customPlot->graph(3)->setVisible(false);

    // 设置坐标轴
    ui->customPlot->xAxis->setLabel("时间 (秒)");
    ui->customPlot->yAxis->setLabel("角度 (°)");

    // 设置初始范围
    ui->customPlot->xAxis->setRange(0, 10);
    ui->customPlot->yAxis->setRange(-180, 180);

    // 显示图例
    ui->customPlot->legend->setVisible(true);
    ui->customPlot->legend->setBrush(QColor(255, 255, 255, 150));

    // 启用交互
    ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // 网格线
    ui->customPlot->xAxis->grid()->setVisible(true);
    ui->customPlot->yAxis->grid()->setVisible(true);
}

void Widget::setupSerialPort()
{
    // 获取所有可用串口
    QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();

    if (availablePorts.isEmpty()) {
        qDebug() << "未找到可用的串口设备";
        QMessageBox::warning(this, "串口错误", "未找到可用的串口设备，请检查连接！");
        return;
    }

    // 让用户选择串口
    QStringList portNames;
    QMap<QString, QSerialPortInfo> portMap;  // 用于存储端口名和对应的信息

    for (const QSerialPortInfo &portInfo : availablePorts) {
        QString displayName = QString("%1 - %2")
                                .arg(portInfo.portName())
                                .arg(portInfo.description());
        portNames << displayName;
        portMap[displayName] = portInfo;

        qDebug() << "可用串口:" << displayName
                 << "制造商:" << portInfo.manufacturer()
                 << "位置:" << portInfo.systemLocation();
    }

    // 弹出选择对话框
    bool ok;
    QString selectedDisplayName = QInputDialog::getItem(this,
        "选择串口",
        "请选择STM32设备连接的串口:",
        portNames, 0, false, &ok);

    if (!ok || selectedDisplayName.isEmpty()) {
        qDebug() << "用户取消选择串口";
        return;
    }

    // 获取选择的串口信息
    QSerialPortInfo selectedPortInfo = portMap[selectedDisplayName];
    QString selectedPort = selectedPortInfo.portName();

    // 配置串口参数
    serialPort->setPortName(selectedPort);
    serialPort->setBaudRate(QSerialPort::Baud115200);
    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setParity(QSerialPort::NoParity);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::NoFlowControl);

    // 尝试打开串口
    if (serialPort->open(QIODevice::ReadWrite)) {
        qDebug() << "串口打开成功:" << selectedPort;

        // 更新UI显示
        ui->statusLabel->setText(QString("串口已连接: %1").arg(selectedDisplayName));

        // 连接数据接收信号
        connect(serialPort, &QSerialPort::readyRead, this, &Widget::readSerialData);

    } else {
        qDebug() << "串口打开失败:" << selectedPort << "错误:" << serialPort->errorString();
        QMessageBox::critical(this, "串口错误",
                            QString("无法打开串口 %1:\n%2").arg(selectedDisplayName).arg(serialPort->errorString()));
    }
}
void Widget::on_startButton_clicked()
{
    // 检查是否有任何连接
    if (currentConnectionType == CONNECTION_NONE && !tcpClient) {
        QMessageBox::warning(this, "连接错误",
                           "请确保设备已连接：\n"
                           "1. 通过串口连接（点击连接串口按钮）\n"
                           "2. 或通过WiFi连接（TCP端口8080）");
        return;
    }

    // 清空数据向量
    timeData.clear();
    angleDataX.clear();
    angleDataY.clear();
    angleDataZ.clear();
    vibrationData.clear();

    startTime = QDateTime::currentMSecsSinceEpoch();
    isCollecting = true;

    dataTimer->start(50);

    ui->startButton->setEnabled(false);
    ui->stopButton->setEnabled(true);

    updateConnectionStatus();
    qDebug() << "开始采集数据...";
}

void Widget::on_stopButton_clicked()
{
    // 注意：这里不再关闭串口，因为串口连接是独立的
    isCollecting = false;
    dataTimer->stop();

    ui->startButton->setEnabled(true);
    ui->stopButton->setEnabled(false);

    // 注意：这里不重置连接类型，因为串口/WiFi连接仍然保持
    updateConnectionStatus();
    qDebug() << "停止采集数据";
}

void Widget::on_vibrationButton_clicked()
{
    if (showVibration) {
        // 切换回角度数据
        ui->customPlot->graph(0)->setVisible(true);
        ui->customPlot->graph(1)->setVisible(true);
        ui->customPlot->graph(2)->setVisible(true);
        ui->customPlot->graph(3)->setVisible(false);

        ui->customPlot->yAxis->setLabel("角度 (°)");
        ui->vibrationButton->setText("显示振动能量");
    } else {
        // 切换到振动能量
        ui->customPlot->graph(0)->setVisible(false);
        ui->customPlot->graph(1)->setVisible(false);
        ui->customPlot->graph(2)->setVisible(false);
        ui->customPlot->graph(3)->setVisible(true);

        ui->customPlot->yAxis->setLabel("振动能量 (g)");
        ui->vibrationButton->setText("显示角度数据");
    }
    showVibration = !showVibration;
    ui->customPlot->replot();
}




void Widget::updateChart()
{
    if (timeData.isEmpty()) return;

    // 更新图表数据
    ui->customPlot->graph(0)->setData(timeData, angleDataX);
    ui->customPlot->graph(1)->setData(timeData, angleDataY);
    ui->customPlot->graph(2)->setData(timeData, angleDataZ);
    ui->customPlot->graph(3)->setData(timeData, vibrationData);

    // 自动调整X轴范围
    double currentTime = timeData.isEmpty() ? 0 : timeData.last();
    if (currentTime < 10) {
        ui->customPlot->xAxis->setRange(0, 10);
    } else {
        ui->customPlot->xAxis->setRange(currentTime - 10, currentTime);
    }

    if (showVibration) {
        // 振动能量显示模式
        if (!vibrationData.isEmpty()) {
            double minVib = *std::min_element(vibrationData.constBegin(), vibrationData.constEnd());
            double maxVib = *std::max_element(vibrationData.constBegin(), vibrationData.constEnd());
            double margin = (maxVib - minVib) * 0.1;
            ui->customPlot->yAxis->setRange(minVib - margin, maxVib + margin);

            double currentVib = vibrationData.last();
            ui->customPlot->graph(3)->setName(QString("振动能量: %1 g").arg(currentVib, 0, 'f', 3));
        }
    } else {
        // 角度数据显示模式
        if (!angleDataX.isEmpty() && !angleDataY.isEmpty() && !angleDataZ.isEmpty()) {
            double minAngle = qMin(qMin(
                *std::min_element(angleDataX.constBegin(), angleDataX.constEnd()),
                *std::min_element(angleDataY.constBegin(), angleDataY.constEnd())),
                *std::min_element(angleDataZ.constBegin(), angleDataZ.constEnd()));

            double maxAngle = qMax(qMax(
                *std::max_element(angleDataX.constBegin(), angleDataX.constEnd()),
                *std::max_element(angleDataY.constBegin(), angleDataY.constEnd())),
                *std::max_element(angleDataZ.constBegin(), angleDataZ.constEnd()));

            double margin = (maxAngle - minAngle) * 0.1;
            ui->customPlot->yAxis->setRange(minAngle - margin, maxAngle + margin);

            // 更新图例显示当前角度值
            ui->customPlot->graph(0)->setName(QString("X角度: %1°").arg(angleDataX.last(), 0, 'f', 1));
            ui->customPlot->graph(1)->setName(QString("Y角度: %1°").arg(angleDataY.last(), 0, 'f', 1));
            ui->customPlot->graph(2)->setName(QString("Z角度: %1°").arg(angleDataZ.last(), 0, 'f', 1));
        }
    }

    ui->customPlot->replot();
}



void Widget::updateDisplay(float xAngle, float yAngle, float zAngle)
{
    ui->compressionAngleLabel->setText(QString("X角度：%1°").arg(xAngle, 0, 'f', 2));
    ui->descentAngleLabel->setText(QString("Y角度：%1°").arg(yAngle, 0, 'f', 2));
    ui->otherAngleLabel->setText(QString("Z角度：%1°").arg(zAngle, 0, 'f', 2));
}

void Widget::updateConnectionStatus()
{
    QString status;

    switch (currentConnectionType) {
    case CONNECTION_NONE:
        status = "等待连接... (支持串口和WiFi)";
        break;
    case CONNECTION_SERIAL:
        status = QString("串口连接: %1").arg(serialPort->portName());
        break;
    case CONNECTION_WIFI:
        if (tcpClient) {
            status = QString("WiFi连接: %1").arg(tcpClient->peerAddress().toString());
        }
        break;
    }

    // 添加TCP服务器状态
    if (tcpServer->isListening()) {
        status += " | TCP服务器运行中(端口8080)";
    } else {
        status += " | TCP服务器未启动";
    }

    ui->statusLabel->setText(status);
}

void Widget::refreshSerialPortList()
{
    ui->serialComboBox->clear();

    QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();

    if (availablePorts.isEmpty()) {
        ui->serialComboBox->addItem("未找到可用串口");
        ui->connectSerialButton->setEnabled(false);
        qDebug() << "未找到可用的串口设备";
        return;
    }

    for (const QSerialPortInfo &portInfo : availablePorts) {
        QString displayName = QString("%1 - %2")
                                .arg(portInfo.portName())
                                .arg(portInfo.description());
        ui->serialComboBox->addItem(displayName, portInfo.portName());

        qDebug() << "可用串口:" << displayName
                 << "制造商:" << portInfo.manufacturer()
                 << "位置:" << portInfo.systemLocation();
    }

    ui->connectSerialButton->setEnabled(true);
}


void Widget::on_connectSerialButton_clicked()
{
    if (serialPort->isOpen()) {
        // 如果串口已经打开，则关闭它
        serialPort->close();
        currentConnectionType = CONNECTION_NONE;
        ui->connectSerialButton->setText("连接串口");
        ui->serialComboBox->setEnabled(true);
        ui->refreshSerialButton->setEnabled(true);
        updateConnectionStatus();
        qDebug() << "串口已断开";
        return;
    }

    if (ui->serialComboBox->currentIndex() < 0) {
        QMessageBox::warning(this, "串口错误", "请先选择串口！");
        return;
    }

    QString selectedPort = ui->serialComboBox->currentData().toString();
    QString selectedDisplayName = ui->serialComboBox->currentText();

    // 配置串口参数
    serialPort->setPortName(selectedPort);
    serialPort->setBaudRate(QSerialPort::Baud115200);
    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setParity(QSerialPort::NoParity);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::NoFlowControl);

    // 尝试打开串口
    if (serialPort->open(QIODevice::ReadWrite)) {
        currentConnectionType = CONNECTION_SERIAL;
        ui->connectSerialButton->setText("断开串口");
        ui->serialComboBox->setEnabled(false);
        ui->refreshSerialButton->setEnabled(false);

        qDebug() << "串口打开成功:" << selectedPort;
        updateConnectionStatus();

        QMessageBox::information(this, "连接成功",
                               QString("串口连接成功:\n%1").arg(selectedDisplayName));
    } else {
        qDebug() << "串口打开失败:" << selectedPort << "错误:" << serialPort->errorString();
        QMessageBox::critical(this, "串口错误",
                            QString("无法打开串口 %1:\n%2").arg(selectedDisplayName).arg(serialPort->errorString()));
    }
}

void Widget::on_refreshSerialButton_clicked()
{
    refreshSerialPortList();
    QMessageBox::information(this, "刷新完成", "串口列表已刷新");
}


void Widget::setupDeepSeekAnalyzer()
{
    // 连接信号槽
    connect(deepSeekAnalyzer, &DeepSeekAnalyzer::analysisComplete,
            this, &Widget::on_deepSeekAnalysisComplete);
    connect(deepSeekAnalyzer, &DeepSeekAnalyzer::analysisError,
            this, &Widget::on_deepSeekAnalysisError);
    connect(deepSeekAnalyzer, &DeepSeekAnalyzer::analysisStatusChanged,
            this, &Widget::on_deepSeekStatusChanged);
    connect(deepSeekAnalyzer, &DeepSeekAnalyzer::alarmSuggestion,
            this, &Widget::on_alarmSuggestion);

    // 设置API密钥（可以从配置文件或用户输入获取）
    // deepSeekAnalyzer->setApiKey("your_api_key_here");
}



void Widget::on_analyzeButton_clicked()
{
    if (angleDataX.isEmpty() || angleDataY.isEmpty() || vibrationData.isEmpty()) {
        QMessageBox::warning(this, "数据分析", "没有可用的传感器数据进行实时分析");
        return;
    }

    // 获取最新的传感器数据
    double currentX = angleDataX.last();
    double currentY = angleDataY.last();
    double currentZ = angleDataZ.isEmpty() ? 0 : angleDataZ.last();
    double currentVib = vibrationData.last();

    // 检查 API 密钥
    QString apiKey = ui->apiKeyLineEdit->text().trimmed();

    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, "API配置", "请输入 DeepSeek API 密钥");
        return;
    }

    // 检查 SSL 支持
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::information(this, "SSL 不可用",
                               "系统 SSL 支持不可用，将使用本地智能分析。\n\n"
                               "本地分析基于规则引擎，提供基本的设备状态评估。");
        performLocalAnalysis(currentX, currentY, currentZ, currentVib);
        return;
    }

    // 如果 SSL 可用，尝试使用 DeepSeek API
    deepSeekAnalyzer->setApiKey(apiKey);

    QString additionalInfo = QString("数据时间范围: %1秒, 数据点数: %2")
                            .arg(timeData.isEmpty() ? 0 : timeData.last(), 0, 'f', 1)
                            .arg(timeData.size());

    deepSeekAnalyzer->analyzeSensorData(currentX, currentY, currentZ, currentVib, additionalInfo);
}

void Widget::on_analyzeHistoricalButton_clicked()
{
    if (angleDataX.size() < 10) {
        QMessageBox::warning(this, "历史分析", "历史数据不足，至少需要10个数据点");
        return;
    }

    QString apiKey = ui->apiKeyLineEdit->text().trimmed();
    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, "API配置", "请输入DeepSeek API密钥");
        return;
    }
    deepSeekAnalyzer->setApiKey(apiKey);

    deepSeekAnalyzer->analyzeHistoricalData(angleDataX, angleDataY, vibrationData);
}

void Widget::on_deepSeekAnalysisComplete(const QString &result)
{
    ui->analysisResultTextEdit->setPlainText(result);
    ui->analysisResultTextEdit->append("\n\n--- 分析时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
}

void Widget::on_deepSeekAnalysisError(const QString &errorMessage)
{
    QMessageBox::warning(this, "分析错误", errorMessage);
    ui->analysisResultTextEdit->append("错误: " + errorMessage);
}

void Widget::on_deepSeekStatusChanged(const QString &status)
{
    ui->analysisStatusLabel->setText("状态: " + status);
}

void Widget::on_alarmSuggestion(const QString &suggestion, int severity)
{
    QString color;
    switch (severity) {
    case 0: color = "green"; break;  // 正常
    case 1: color = "orange"; break; // 警告
    case 2: color = "red"; break;    // 严重
    default: color = "black"; break;
    }

    ui->analysisStatusLabel->setText(QString("AI建议: <font color='%1'>%2</font>").arg(color).arg(suggestion));
}
void Widget::testSSLFix()
{
    qDebug() << "=== SSL 支持测试 ===";
    qDebug() << "SSL 支持:" << QSslSocket::supportsSsl();
    qDebug() << "SSL 库版本:" << QSslSocket::sslLibraryVersionString();
    qDebug() << "SSL 构建版本:" << QSslSocket::sslLibraryBuildVersionString();

    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(this, "SSL 测试失败",
                            "SSL 支持未启用！\n\n"
                            "请确保：\n"
                            "1. OpenSSL v3.6.0 已正确安装\n"
                            "2. DLL 文件已复制到程序目录\n"
                            "3. 系统已重启（如果需要）");
        return;
    }

    // 测试 HTTPS 连接
    QMessageBox::information(this, "SSL 测试",
                           "SSL 支持已启用！\n\n"
                           "现在可以测试 DeepSeek API 连接。");

    // 可选：自动测试 API 连接
   // if (!ui->apiKeyLineEdit->text().isEmpty()) {
      //  on_testApiButton_clicked();
    //}
}
void Widget::performLocalAnalysis(double angleX, double angleY, double angleZ, double vibration)
{
    QString result = "🔍 本地智能分析结果:\n\n";

    // 角度分析
    if (qAbs(angleX) > alarmThresholdAngleX) {
        result += "⚠️ **X轴角度异常**\n";
        result += QString("   当前值: %1° | 阈值: %2°\n").arg(angleX, 0, 'f', 1).arg(alarmThresholdAngleX);
        result += "   可能原因: 设备倾斜、安装不稳\n";
    }

    if (qAbs(angleY) > alarmThresholdAngleY) {
        result += "⚠️ **Y轴角度异常**\n";
        result += QString("   当前值: %1° | 阈值: %2°\n").arg(angleY, 0, 'f', 1).arg(alarmThresholdAngleY);
        result += "   可能原因: 水平度偏差、机械松动\n";
    }

    // 振动分析
    if (vibration > alarmThresholdVibration) {
        result += "⚠️ **振动能量异常**\n";
        result += QString("   当前值: %1g | 阈值: %2g\n").arg(vibration, 0, 'f', 3).arg(alarmThresholdVibration);
        result += "   可能原因: 轴承磨损、不平衡、松动\n";
    }

    // 综合评估
    int issueCount = 0;
    if (qAbs(angleX) > alarmThresholdAngleX) issueCount++;
    if (qAbs(angleY) > alarmThresholdAngleY) issueCount++;
    if (vibration > alarmThresholdVibration) issueCount++;

    if (issueCount == 0) {
        result += "✅ **设备状态**: 运行正常\n";
        result += "   所有参数均在安全范围内\n";
    } else if (issueCount == 1) {
        result += "🟡 **设备状态**: 需要注意\n";
        result += "   发现1个参数异常，建议检查\n";
    } else {
        result += "🔴 **设备状态**: 需要立即关注\n";
        result += QString("   发现%1个参数异常，建议立即检查\n").arg(issueCount);
    }

    // 维护建议
    result += "\n📋 **维护建议**:\n";
    if (vibration > 1.5) {
        result += "   • 检查轴承和机械连接\n";
    }
    if (qAbs(angleX) > 20 || qAbs(angleY) > 20) {
        result += "   • 检查设备安装水平度\n";
    }
    if (issueCount == 0) {
        result += "   • 继续正常监控\n";
    }

    ui->analysisResultTextEdit->setPlainText(result);
    ui->analysisResultTextEdit->append("\n--- 本地分析时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
}
