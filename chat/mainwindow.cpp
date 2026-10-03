#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "tcpmgr.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QStatusBar>
#include "usermgr.h"
#include "submissionexitguard.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _session(this)
{
    ui->setupUi(this);
    connect(&_session, &ClientSession::reconnectChanged, this, /** @brief 保留会话页面并显示自动恢复提示。 */ [this](bool active) {
        if (active) statusBar()->showMessage(tr("连接中断，正在重新连接…"));
        else statusBar()->clearMessage();
    });
    connect(&_session, &ClientSession::reconnectFailed, this, /** @brief 恢复被拒绝或尝试耗尽后明确返回登录页。 */ [this] {
        resetSession(SessionResetReason::UnexpectedDisconnect);
        QMessageBox::information(this, tr("连接已断开"), tr("无法恢复当前会话，请重新登录。"));
    });
    connect(&_session, &ClientSession::logoutFinished, this, /** @brief 撤销成功才结束账号；失败保留界面并允许重试。 */ [this](bool success) {
        setEnabled(true);
        if (!success) {
            _closeAfterLogout = false;
            QMessageBox::warning(this, tr("退出未完成"), tr("服务器尚未确认退出，请检查连接后重试。"));
            return;
        }
        _chat_dlg.clear(); _activeAuthFlowId = 0;
        if (_closeAfterLogout) { _closeAfterLogout = false; close(); }
        else offlineLogin();
    });

    _login_dlg = new LoginDialog(_authFlow, this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);
    _login_dlg->show();

    _ui_status = UIStatus::LOGIN_UI;

    // 创建和注册消息链接
    // 连接登录界面转注册界面信号
    connect(_login_dlg, &LoginDialog::registrationRequested, this, &MainWindow::loginSwitchReg);
    // 连接登录界面转重置界面信号
    connect(_login_dlg, &LoginDialog::passwordResetRequested, this, &MainWindow::loginSwitchReset);
    // 连接登录界面转聊天界面信号
    connect(_login_dlg, &LoginDialog::loginSucceeded,
            this, &MainWindow::loginSwitchChat);
    // 连接服务器通知下线信号
    connect(TcpMgr::instance().get(), &TcpMgr::forcedOffline, this, &MainWindow::notifyOffline);
    // 连接结束后区分预期关闭与异常断线。
    connect(TcpMgr::instance().get(), &TcpMgr::connectionClosed, this, &MainWindow::connectionClose);
}

MainWindow::~MainWindow()
{
    _session.resetSession(SessionResetReason::Logout);
    delete ui;
//     if (_login_dlg) {
//         delete _login_dlg;
//         _login_dlg = nullptr;
//     }

//     if (_register_dlg) {
//         delete _register_dlg;
//         _register_dlg = nullptr;
//     }
}

void MainWindow::loginSwitchReg() {
    // 创建注册界面窗口，因为我在切换到其他界面后，这个界面可能就被析构了
    _register_dlg = new RegisterDialog(_authFlow, this);
    _register_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

    // 连接注册界面返回登录信号
    connect(_register_dlg, &RegisterDialog::loginRequested, this, &MainWindow::regSwitchLogin);

    setCentralWidget(_register_dlg);
    _login_dlg->hide();
    _register_dlg->show();
    _ui_status = UIStatus::REGISTER_UI;
}

void MainWindow::loginSwitchReset()
{
    _reset_dlg = new ResetDialog(_authFlow, this);
    _reset_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_reset_dlg);

    _login_dlg->hide();
    _reset_dlg->show();

    connect(_reset_dlg, &ResetDialog::loginRequested, this, &MainWindow::resetSwitchLogin);
    _ui_status = UIStatus::RESET_UI;
}

void MainWindow::regSwitchLogin() {
    // 创建一个登录页面，因为之前的页面切换后，被析构了
    _login_dlg = new LoginDialog(_authFlow, this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _register_dlg->hide();
    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::registrationRequested, this, &MainWindow::loginSwitchReg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::passwordResetRequested, this, &MainWindow::loginSwitchReset);
    connect(_login_dlg, &LoginDialog::loginSucceeded,
            this, &MainWindow::loginSwitchChat);
    _ui_status = UIStatus::LOGIN_UI;
}

void MainWindow::resetSwitchLogin() {
    // 创建一个登录页面，因为之前的页面切换后，被析构了
    _login_dlg = new LoginDialog(_authFlow, this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _reset_dlg->hide();
    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::registrationRequested, this, &MainWindow::loginSwitchReg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::passwordResetRequested, this, &MainWindow::loginSwitchReset);
    connect(_login_dlg, &LoginDialog::loginSucceeded,
            this, &MainWindow::loginSwitchChat);
    _ui_status = UIStatus::LOGIN_UI;
}

void MainWindow::loginSwitchChat(AuthFlowId flowId) {
    _activeAuthFlowId = flowId;
    _chat_dlg = new ChatDialog(this);
    connect(_chat_dlg, &ChatDialog::logoutRequested, this,
        /** @brief 账号按钮复用统一退出保护及完整会话清理。 */ [this](bool change) {
        resetSession(change ? SessionResetReason::SwitchAccount : SessionResetReason::Logout);
    });
    _chat_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_chat_dlg);

    _login_dlg->hide();
    _chat_dlg->show();

    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _ui_status = UIStatus::CHAT_UI;
    _session.beginSession(_chat_dlg);
}

void MainWindow::notifyOffline()
{
    if (_session.isLoggingOut()) return;
    if (!_session.isActive()) {
        return;
    }
    // 出现弹窗，并阻塞操作，直到用户点击确定
    QMessageBox::information(this, "下线提示", "同账号异地登录，该终端下线！");
    resetSession(SessionResetReason::Kicked);
}

void MainWindow::connectionClose(bool expectedClose)
{
    if (expectedClose || !_session.isActive() || _session.isReconnecting() || _session.isLoggingOut()) {
        return;
    }
    AuthOutcome outcome;
    outcome.kind = AuthOutcomeKind::AbnormalDisconnect;
    const AuthAction action = _authFlow.reduce(_activeAuthFlowId, outcome);
    if (action.kind != AuthActionKind::ShowLogin) {
        return;
    }
    // 出现弹窗，并阻塞操作，直到用户点击确定
    QMessageBox::information(this, "下线提示", "心跳检测超时，该终端下线！");
    resetSession(SessionResetReason::UnexpectedDisconnect);
}

bool MainWindow::resetSession(SessionResetReason reason)
{
    if ((reason == SessionResetReason::Logout || reason == SessionResetReason::SwitchAccount)
        && !confirmSubmissionExit(this, UserMgr::instance()->hasPendingSubmissions()
            || (_chat_dlg && _chat_dlg->hasDrafts()))) return false;
    if (reason == SessionResetReason::Logout || reason == SessionResetReason::SwitchAccount) {
        if (!_session.requestLogout(reason)) return false;
        setEnabled(false); return true;
    }
    if (!_session.resetSession(reason)) {
        return false;
    }
    _chat_dlg.clear();
    _activeAuthFlowId = 0;
    offlineLogin();
    return true;
}

// 离线后切换到登录状态
void MainWindow::offlineLogin()
{
    if (_ui_status == UIStatus::LOGIN_UI) {
        return;
    }

    // 离线后切换到登录状态
    _login_dlg = new LoginDialog(_authFlow, this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    // 设置当前minwindow的最大值与最小值
    this->setMaximumSize(300,500);
    this->setMinimumSize(300,500);
    this->resize(300, 500);

    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::registrationRequested, this, &MainWindow::loginSwitchReg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::passwordResetRequested, this, &MainWindow::loginSwitchReset);
    connect(_login_dlg, &LoginDialog::loginSucceeded,
            this, &MainWindow::loginSwitchChat);
    _ui_status = UIStatus::LOGIN_UI;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmSubmissionExit(this, UserMgr::instance()->hasPendingSubmissions()
        || (_chat_dlg && _chat_dlg->hasDrafts()))) {
        event->ignore(); return;
    }
    if (_session.isActive()) {
        event->ignore(); _closeAfterLogout = true;
        if (_session.requestLogout(SessionResetReason::Logout)) setEnabled(false);
        else _closeAfterLogout = false;
        return;
    }
    QMainWindow::closeEvent(event);
}
