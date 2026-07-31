#include "mainwindow.h"
#include "logmgr.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    auto logger = LogMgr::GetInstance();
    if (!logger->InitLogMgr()) {
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

    MainWindow w;
    w.show();
    return a.exec();
}
