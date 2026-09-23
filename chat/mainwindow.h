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

/** @brief 管理认证页面切换和账号会话根对象的生命周期。 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** @brief 创建登录入口并连接会话关闭和页面切换事件。 */
    MainWindow(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~MainWindow();
    /** @brief 幂等结束账号会话并清理连接、模型及临时账号状态，返回是否执行了重置。 */
    bool resetSession(SessionResetReason reason);
public slots:
    // 登录转注册槽函数
    /** @brief 登录转注册槽函数。 */
    void slot_login_switch_reg();
    /** @brief 从注册页面返回登录页面并恢复登录事件连接。 */
    void slot_reg_switch_login();
    // 登录转重置槽函数
    /** @brief 登录转重置槽函数。 */
    void slot_login_switch_reset();
    /** @brief 从密码重置页面返回登录页面。 */
    void slot_reset_switch_login();
    // 登录转聊天槽函数
    /** @brief 登录转聊天槽函数。 */
    void slot_login_switch_chat(AuthFlowId flowId);
    // 服务器通知下线槽函数
    /** @brief 服务器通知下线槽函数。 */
    void slot_notify_offline();
    /** @brief 处理连接结束，仅异常断线触发账号会话重置。 */
    void slot_connection_close(bool expectedClose);
private:
    /** @brief 重新创建并展示登录页，恢复页面切换连接。 */
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
