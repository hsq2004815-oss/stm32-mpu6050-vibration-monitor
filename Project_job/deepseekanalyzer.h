#ifndef DEEPSEEKANALYZER_H
#define DEEPSEEKANALYZER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QDateTime>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslError>
#include <QVector>
#include <numeric>
#include <algorithm>

class DeepSeekAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekAnalyzer(QObject *parent = nullptr);
    ~DeepSeekAnalyzer();

    // 设置API密钥
    void setApiKey(const QString &apiKey);

    // 分析传感器数据
    void analyzeSensorData(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo = "");

    // 批量分析历史数据
    void analyzeHistoricalData(const QVector<double> &angleXData,
                              const QVector<double> &angleYData,
                              const QVector<double> &vibrationData);

    // 设置分析模式
    enum AnalysisMode {
        RealTimeAnalysis,    // 实时分析
        HistoricalAnalysis,  // 历史数据分析
        AnomalyDetection     // 异常检测
    };
    void setAnalysisMode(AnalysisMode mode);

signals:
    // 分析完成信号
    void analysisComplete(const QString &result);
    void analysisError(const QString &errorMessage);
    void analysisStatusChanged(const QString &status);

    // 警报建议信号
    void alarmSuggestion(const QString &suggestion, int severity); // severity: 0-正常, 1-警告, 2-严重

private slots:
    void onNetworkReplyFinished(QNetworkReply *reply);
    void handleTimeout();

private:
    QNetworkAccessManager *networkManager;
    QString m_apiKey;
    AnalysisMode m_currentMode;
    QTimer *timeoutTimer;

    // 构建请求JSON
    QJsonObject buildAnalysisRequest(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo);
    QJsonObject buildHistoricalAnalysisRequest(const QVector<double> &angleXData,
                                              const QVector<double> &angleYData,
                                              const QVector<double> &vibrationData);

    // 解析API响应
    QString parseApiResponse(const QByteArray &responseData);

    // 错误处理
    void handleApiError(int errorCode, const QString &errorMessage);

    // 生成分析提示词
    QString generateAnalysisPrompt(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo);
};

#endif // DEEPSEEKANALYZER_H
