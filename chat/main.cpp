#include "mainwindow.h"
#include "logmgr.h"
#include "tcpmgr.h"
#include "usermgr.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>

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
    gate_url_prefix = "http://" + gate_host + ":" + gate_port;

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
