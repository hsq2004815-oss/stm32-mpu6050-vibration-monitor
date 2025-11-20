#include "widget.h"
#include <QApplication>
#include <QSslSocket>
#include <QMessageBox>
#include <QDebug>
#include <QLibraryInfo>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 详细的 SSL 诊断信息
    qDebug() << "=== SSL 详细诊断信息 ===";
    qDebug() << "Qt 版本:" << QT_VERSION_STR;
    qDebug() << "SSL 支持:" << QSslSocket::supportsSsl();
    qDebug() << "SSL 库版本:" << QSslSocket::sslLibraryVersionString();
    qDebug() << "SSL 构建版本:" << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "应用程序是" << (sizeof(void*) == 8 ? "64位" : "32位");

    // 只在 Qt 5.12+ 中检查可用后端
    #if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    qDebug() << "可用 SSL 后端:" << QSslSocket::availableBackends();
    #else
    qDebug() << "Qt 5.11.1 不支持 availableBackends() 方法";
    #endif

    if (!QSslSocket::supportsSsl()) {
        QString errorMsg =
            "系统不支持 SSL！\n\n"
            "检测到的信息：\n"
            "SSL 库版本: " + QSslSocket::sslLibraryVersionString() + "\n"
            "SSL 构建版本: " + QSslSocket::sslLibraryBuildVersionString() + "\n\n"
            "解决方案：\n"
            "1. 确保 libeay32.dll 和 ssleay32.dll 在程序目录中\n"
            "2. 检查 OpenSSL 版本兼容性\n"
            "3. 重新启动应用程序";

        QMessageBox::critical(nullptr, "SSL 错误", errorMsg);
        return -1;
    }

    qDebug() << "SSL 支持正常，启动应用程序...";

    // 设置默认 SSL 配置
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    Widget w;
    w.show();

    return a.exec();
}
