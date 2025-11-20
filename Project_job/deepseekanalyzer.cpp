#include "deepseekanalyzer.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>
#include <QSslConfiguration>
#include <QSslSocket>

DeepSeekAnalyzer::DeepSeekAnalyzer(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , m_currentMode(RealTimeAnalysis)
    , timeoutTimer(new QTimer(this))
{
    qDebug() << "初始化 DeepSeek 分析器...";

    // 检查 SSL 支持
    if (!QSslSocket::supportsSsl()) {
        qCritical() << "错误: 系统不支持 SSL，HTTPS 功能将不可用";
        emit analysisError("系统不支持 SSL，无法连接 DeepSeek API");
        return;
    }

    qDebug() << "SSL 支持正常，库版本:" << QSslSocket::sslLibraryVersionString();

    // 配置 SSL
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();

    // 设置 TLS 版本（使用兼容的方式）
        #if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);

        #else
        // Qt 5.11.1 使用 SecureProtocols，它包含 TLS 1.0, 1.1, 1.2
        sslConfig.setProtocol(QSsl::SecureProtocols);
        #endif

        sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer); // 生产环境应该验证证书

        QSslConfiguration::setDefaultConfiguration(sslConfig);

    // 处理 SSL 错误
        connect(networkManager, &QNetworkAccessManager::sslErrors,
            this, [](QNetworkReply *reply, const QList<QSslError> &errors) {
        qWarning() << "SSL 错误发生:";
        for (const QSslError &error : errors) {
            qWarning() << "SSL 错误:" << error.errorString();
        }
        // 开发环境下忽略 SSL 错误，生产环境应该正确处理
        reply->ignoreSslErrors();
    });

    // 连接网络响应信号
    connect(networkManager, &QNetworkAccessManager::finished,
            this, &DeepSeekAnalyzer::onNetworkReplyFinished);

    // 设置超时定时器（30秒超时）
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(30000);
    connect(timeoutTimer, &QTimer::timeout, this, &DeepSeekAnalyzer::handleTimeout);

    qDebug() << "DeepSeek分析器初始化完成";
}

DeepSeekAnalyzer::~DeepSeekAnalyzer()
{
    if (timeoutTimer->isActive()) {
        timeoutTimer->stop();
    }
}

void DeepSeekAnalyzer::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
    if (!m_apiKey.isEmpty()) {
        qDebug() << "API密钥设置成功";
        emit analysisStatusChanged("API密钥设置成功");
    } else {
        qWarning() << "API密钥为空";
        emit analysisStatusChanged("API密钥为空");
    }
}

void DeepSeekAnalyzer::setAnalysisMode(AnalysisMode mode)
{
    m_currentMode = mode;
    QString modeStr;
    switch (mode) {
    case RealTimeAnalysis: modeStr = "实时分析模式"; break;
    case HistoricalAnalysis: modeStr = "历史分析模式"; break;
    case AnomalyDetection: modeStr = "异常检测模式"; break;
    }
    qDebug() << "分析模式设置为:" << modeStr;
    emit analysisStatusChanged(modeStr);
}

void DeepSeekAnalyzer::analyzeSensorData(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo)
{
    if (m_apiKey.isEmpty()) {
        QString error = "API密钥未设置，请先调用setApiKey()设置密钥";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    if (!QSslSocket::supportsSsl()) {
        QString error = "SSL不支持，无法进行网络请求";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    emit analysisStatusChanged("正在分析传感器数据...");
    qDebug() << "开始分析传感器数据 - X:" << angleX << "Y:" << angleY << "Z:" << angleZ << "振动:" << vibration;

    // 构建请求
    QJsonObject requestData = buildAnalysisRequest(angleX, angleY, angleZ, vibration, additionalInfo);

    // 设置网络请求
    QNetworkRequest request;
    request.setUrl(QUrl("https://api.deepseek.com/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    // 设置 SSL 配置
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    request.setSslConfiguration(sslConfig);

    // 发送请求
    QNetworkReply *reply = networkManager->post(request, QJsonDocument(requestData).toJson());
    if (!reply) {
        QString error = "创建网络请求失败";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    // 启动超时定时器
    timeoutTimer->start();
    qDebug() << "数据分析请求已发送";
}

void DeepSeekAnalyzer::analyzeHistoricalData(const QVector<double> &angleXData,
                                           const QVector<double> &angleYData,
                                           const QVector<double> &vibrationData)
{
    if (m_apiKey.isEmpty()) {
        QString error = "API密钥未设置";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    if (!QSslSocket::supportsSsl()) {
        QString error = "SSL不支持，无法进行网络请求";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    if (angleXData.isEmpty() || angleYData.isEmpty() || vibrationData.isEmpty()) {
        QString error = "历史数据为空";
        qWarning() << error;
        emit analysisError(error);
        return;
    }

    emit analysisStatusChanged("正在分析历史数据...");
    qDebug() << "开始分析历史数据 - X数据点:" << angleXData.size()
             << "Y数据点:" << angleYData.size()
             << "振动数据点:" << vibrationData.size();

    QJsonObject requestData = buildHistoricalAnalysisRequest(angleXData, angleYData, vibrationData);

    QNetworkRequest request;
    request.setUrl(QUrl("https://api.deepseek.com/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    request.setSslConfiguration(sslConfig);

    QNetworkReply *reply = networkManager->post(request, QJsonDocument(requestData).toJson());
    if (!reply) {
        QString error = "创建历史数据分析请求失败";
        qCritical() << error;
        emit analysisError(error);
        return;
    }

    timeoutTimer->start();
    qDebug() << "历史数据分析请求已发送";
}

QJsonObject DeepSeekAnalyzer::buildAnalysisRequest(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo)
{
    QString prompt = generateAnalysisPrompt(angleX, angleY, angleZ, vibration, additionalInfo);

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject requestData;
    requestData["model"] = "deepseek-chat";
    requestData["messages"] = messages;
    requestData["max_tokens"] = 1000;
    requestData["temperature"] = 0.3; // 较低的温度以获得更确定的输出

    return requestData;
}

QJsonObject DeepSeekAnalyzer::buildHistoricalAnalysisRequest(const QVector<double> &angleXData,
                                                           const QVector<double> &angleYData,
                                                           const QVector<double> &vibrationData)
{
    // 构建历史数据统计信息
    double avgX = 0, avgY = 0, avgVib = 0;
    double maxX = 0, maxY = 0, maxVib = 0;
    double minX = 0, minY = 0, minVib = 0;

    if (!angleXData.isEmpty()) {
        avgX = std::accumulate(angleXData.begin(), angleXData.end(), 0.0) / angleXData.size();
        maxX = *std::max_element(angleXData.begin(), angleXData.end());
        minX = *std::min_element(angleXData.begin(), angleXData.end());
    }

    if (!angleYData.isEmpty()) {
        avgY = std::accumulate(angleYData.begin(), angleYData.end(), 0.0) / angleYData.size();
        maxY = *std::max_element(angleYData.begin(), angleYData.end());
        minY = *std::min_element(angleYData.begin(), angleYData.end());
    }

    if (!vibrationData.isEmpty()) {
        avgVib = std::accumulate(vibrationData.begin(), vibrationData.end(), 0.0) / vibrationData.size();
        maxVib = *std::max_element(vibrationData.begin(), vibrationData.end());
        minVib = *std::min_element(vibrationData.begin(), vibrationData.end());
    }

    QString prompt = QString(
        "请分析以下STM32传感器的历史数据统计信息：\n"
        "X轴角度 - 平均值: %1°, 最大值: %2°, 最小值: %3°, 数据点数: %4\n"
        "Y轴角度 - 平均值: %5°, 最大值: %6°, 最小值: %7°, 数据点数: %8\n"
        "振动能量 - 平均值: %9g, 最大值: %10g, 最小值: %11g, 数据点数: %12\n\n"
        "请提供：\n"
        "1. 设备运行状态评估\n"
        "2. 潜在问题分析\n"
        "3. 维护建议\n"
        "4. 异常模式识别\n"
        "请用清晰的结构化格式回复。"
    ).arg(avgX, 0, 'f', 2).arg(maxX, 0, 'f', 2).arg(minX, 0, 'f', 2).arg(angleXData.size())
     .arg(avgY, 0, 'f', 2).arg(maxY, 0, 'f', 2).arg(minY, 0, 'f', 2).arg(angleYData.size())
     .arg(avgVib, 0, 'f', 3).arg(maxVib, 0, 'f', 3).arg(minVib, 0, 'f', 3).arg(vibrationData.size());

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject requestData;
    requestData["model"] = "deepseek-chat";
    requestData["messages"] = messages;
    requestData["max_tokens"] = 1500;
    requestData["temperature"] = 0.3;

    return requestData;
}

QString DeepSeekAnalyzer::generateAnalysisPrompt(double angleX, double angleY, double angleZ, double vibration, const QString &additionalInfo)
{
    return QString(
        "你是一个工业设备监测专家。请分析以下STM32传感器数据：\n\n"
        "实时传感器读数：\n"
        "- X轴角度：%1°\n"
        "- Y轴角度：%2°\n"
        "- Z轴角度：%3°\n"
        "- 振动能量：%4g\n"
        "%5\n\n"
        "请从以下方面进行分析：\n"
        "1. **设备状态评估**：当前设备是否运行正常\n"
        "2. **异常检测**：是否存在角度异常或振动异常\n"
        "3. **根本原因分析**：可能导致当前读数的原因\n"
        "4. **维护建议**：是否需要立即检查或维护\n"
        "5. **安全风险**：是否存在安全隐患\n\n"
        "请用清晰的结构化格式回复，如果发现异常请明确标注严重程度（正常/警告/严重）。"
    ).arg(angleX, 0, 'f', 2)
     .arg(angleY, 0, 'f', 2)
     .arg(angleZ, 0, 'f', 2)
     .arg(vibration, 0, 'f', 3)
     .arg(additionalInfo.isEmpty() ? "" : "附加信息：" + additionalInfo);
}

void DeepSeekAnalyzer::onNetworkReplyFinished(QNetworkReply *reply)
{
    timeoutTimer->stop();

    if (reply->error() != QNetworkReply::NoError) {
        handleApiError(reply->error(), reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QString analysisResult = parseApiResponse(responseData);
    emit analysisComplete(analysisResult);
    emit analysisStatusChanged("分析完成");

    // 简单关键词检测，提供警报建议
    if (analysisResult.contains("严重") || analysisResult.contains("紧急") || analysisResult.contains("立即停止")) {
        emit alarmSuggestion("AI分析建议立即关注设备状态", 2);
    } else if (analysisResult.contains("警告") || analysisResult.contains("注意")) {
        emit alarmSuggestion("AI检测到潜在问题建议检查", 1);
    } else {
        emit alarmSuggestion("设备运行状态正常", 0);
    }
}

QString DeepSeekAnalyzer::parseApiResponse(const QByteArray &responseData)
{
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull()) {
        return "解析API响应失败";
    }

    QJsonObject rootObj = doc.object();
    if (!rootObj.contains("choices")) {
        if (rootObj.contains("error")) {
            QJsonObject errorObj = rootObj["error"].toObject();
            return QString("API错误: %1").arg(errorObj["message"].toString());
        }
        return "无效的API响应格式";
    }

    QJsonArray choices = rootObj["choices"].toArray();
    if (choices.isEmpty()) {
        return "API返回空结果";
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject message = firstChoice["message"].toObject();
    QString content = message["content"].toString();

    return content;
}

void DeepSeekAnalyzer::handleTimeout()
{
    emit analysisError("API请求超时，请检查网络连接");
    emit analysisStatusChanged("请求超时");
}

void DeepSeekAnalyzer::handleApiError(int errorCode, const QString &errorMessage)
{
    QString errorStr;
    switch (errorCode) {
    case QNetworkReply::AuthenticationRequiredError:
        errorStr = "API密钥认证失败，请检查密钥是否正确";
        break;
    case QNetworkReply::ConnectionRefusedError:
        errorStr = "连接被拒绝，请检查网络连接";
        break;
    case QNetworkReply::TimeoutError:
        errorStr = "连接超时，请检查网络连接";
        break;
    default:
        errorStr = QString("网络错误[%1]: %2").arg(errorCode).arg(errorMessage);
        break;
    }

    emit analysisError(errorStr);
    emit analysisStatusChanged("分析失败");
}
