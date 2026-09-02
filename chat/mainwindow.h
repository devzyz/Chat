#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "logindialog.h"
#include "registerdialog.h"
#include "resetdialog.h"
#include "chatdialog.h"
#include "clientsession.h"
#include <QPointer>

/***************************************************
 * @file        mainwindow.h
 * @brief       主窗口
 *
 * @author      Zzzyz
 * @date        2026/03/01
 * @history
 ***************************************************/

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

enum UIStatus{
    LOGIN_UI,
    REGISTER_UI,
    RESET_UI,
    CHAT_UI
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool resetSession(SessionResetReason reason);
public slots:
    // 登录转注册槽函数
    void slot_login_switch_reg();
    // 注册转登录槽函数
    void slot_reg_switch_login();
    // 登录转重置槽函数
    void slot_login_switch_reset();
    // 重置转登录槽函数
    void slot_reset_switch_login();
    // 登录转聊天槽函数
    void slot_login_switch_chat(AuthFlowId flowId);
    // 服务器通知下线槽函数
    void slot_notify_offline();
    // 服务器断开连接
    void slot_connection_close(bool expectedClose);
private:
    void offlineLogin();

    Ui::MainWindow *ui;
    QPointer<LoginDialog> _login_dlg;
    QPointer<RegisterDialog> _register_dlg;
    QPointer<ResetDialog> _reset_dlg;
    QPointer<ChatDialog> _chat_dlg;
    UIStatus _ui_status = UIStatus::LOGIN_UI;
    AuthFlowCoordinator _authFlow;
    AuthFlowId _activeAuthFlowId = 0;
    ClientSession _session;
};
#endif // MAINWINDOW_H
