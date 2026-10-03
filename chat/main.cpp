#include "mainwindow.h"
#include "logmgr.h"
#include "tcpmgr.h"
#include "usermgr.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QHostAddress>
#include <QSslConfiguration>
#include <QSslSocket>

/** @brief 加载客户端配置和日志，创建主窗口并运行 Qt 事件循环。 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    auto logger = LogMgr::instance();
    if (!logger->initLogMgr()) {
        QMessageBox::critical(
            nullptr,
            QObject::tr("日志初始化失败"),
            QObject::tr("无法创建或打开日志文件，客户端将退出。"));
        return EXIT_FAILURE;
    }

    QFile qss(":/style/stylesheet.qss");

    if (qss.open(QFile::ReadOnly)) {
        SPDLOG_DEBUG("stylesheet loaded");
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);
        qss.close();
    }else {
        SPDLOG_WARN("stylesheet could not be opened");
    }

    // 通过config.ini配置url
    QString fileName = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + fileName);
    QSettings settings(config_path, QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();
    const auto scheme = settings.value("GateServer/scheme", "http").toString();
    const QUrl resources(settings.value("ResourceServer/Url", "http://127.0.0.1:8090").toString());
    const bool loopback = (gate_host == "localhost" || QHostAddress(gate_host).isLoopback())
        && (resources.host() == "localhost" || QHostAddress(resources.host()).isLoopback());
    bool portOk = false;
    const auto gatePort = gate_port.toInt(&portOk);
    if (!portOk || gatePort < 1 || gatePort > 65535 || gate_host.isEmpty() || !resources.isValid()
        || resources.host().isEmpty() || (scheme != "http" && scheme != "https") || (scheme == "http" && !loopback)
        || (scheme == "https" && (resources.scheme() != "https" || !QSslSocket::supportsSsl()))) {
        QMessageBox::critical(nullptr, QObject::tr("连接配置错误"),
            QObject::tr("局域网连接必须为 Gate 和 Resource 配置 HTTPS，并安装可用的 TLS 运行库。"));
        return EXIT_FAILURE;
    }
    if (scheme == "https") {
        auto security = QSslConfiguration::defaultConfiguration();
        const auto caFile = settings.value("Security/CAFile").toString();
        if (!caFile.isEmpty()) {
            const auto certificates = QSslCertificate::fromPath(QDir(app_path).absoluteFilePath(caFile));
            if (certificates.isEmpty()) {
                QMessageBox::critical(nullptr, QObject::tr("证书配置错误"), QObject::tr("无法读取受信任的 CA 证书。"));
                return EXIT_FAILURE;
            }
            auto authorities = security.caCertificates(); authorities.append(certificates);
            security.setCaCertificates(authorities);
        }
        security.setProtocol(QSsl::TlsV1_2OrLater);
        security.setPeerVerifyMode(QSslSocket::VerifyPeer);
        QSslConfiguration::setDefaultConfiguration(security);
    }
    QUrl gateUrl; gateUrl.setScheme(scheme); gateUrl.setHost(gate_host); gateUrl.setPort(gatePort);
    gate_url_prefix = gateUrl.toString();

    int exitCode = 0;
    {
        MainWindow w;
        w.show();
        exitCode = a.exec();
    }

    TcpMgr::releaseInstance();
    UserMgr::releaseInstance();
    logger->close();
    logger.reset();
    LogMgr::releaseInstance();
    return exitCode;
}
