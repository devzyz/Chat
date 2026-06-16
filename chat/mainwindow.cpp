#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "tcpmgr.h"
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);
    _login_dlg->show();

    _ui_status = UIStatus::LOGIN_UI;

    // 创建和注册消息链接
    // 连接登录界面转注册界面信号
    connect(_login_dlg, &LoginDialog::sig_login_switch_reg, this, &MainWindow::slot_login_switch_reg);
    // 连接登录界面转重置界面信号
    connect(_login_dlg, &LoginDialog::sig_login_switch_reset, this, &MainWindow::slot_login_switch_reset);
    // 连接登录界面转聊天界面信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_login_switch_chat, this, &MainWindow::slot_login_switch_chat);
    // 连接服务器通知下线信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_notify_offline, this, &MainWindow::slot_notify_offline);
    // 连接服务器断开lian
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_close, this, &MainWindow::slot_connection_close);
    // emit TcpMgr::GetInstance()->sig_login_switch_chat();
}

MainWindow::~MainWindow()
{
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

void MainWindow::slot_login_switch_reg() {
    // 创建注册界面窗口，因为我在切换到其他界面后，这个界面可能就被析构了
    _register_dlg = new RegisterDialog(this);
    _register_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

    // 连接注册界面返回登录信号
    connect(_register_dlg, &RegisterDialog::sig_reg_switch_login, this, &MainWindow::slot_reg_switch_login);

    setCentralWidget(_register_dlg);
    _login_dlg->hide();
    _register_dlg->show();
    _ui_status = UIStatus::REGISTER_UI;
}

void MainWindow::slot_login_switch_reset()
{
    _reset_dlg = new ResetDialog(this);
    _reset_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_reset_dlg);

    _login_dlg->hide();
    _reset_dlg->show();

    connect(_reset_dlg, &ResetDialog::sig_reset_switch_login, this, &MainWindow::slot_reset_switch_login);
    _ui_status = UIStatus::RESET_UI;
}

void MainWindow::slot_reg_switch_login() {
    // 创建一个登录页面，因为之前的页面切换后，被析构了
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _register_dlg->hide();
    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reg, this, &MainWindow::slot_login_switch_reg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reset, this, &MainWindow::slot_login_switch_reset);
    _ui_status = UIStatus::LOGIN_UI;
}

void MainWindow::slot_reset_switch_login() {
    // 创建一个登录页面，因为之前的页面切换后，被析构了
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _reset_dlg->hide();
    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reg, this, &MainWindow::slot_login_switch_reg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reset, this, &MainWindow::slot_login_switch_reset);
    _ui_status = UIStatus::LOGIN_UI;
}

void MainWindow::slot_login_switch_chat() {
    _chat_dlg = new ChatDialog(this);
    _chat_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_chat_dlg);

    _login_dlg->hide();
    _chat_dlg->show();

    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _ui_status = UIStatus::CHAT_UI;
}

void MainWindow::slot_notify_offline()
{
    // 出现弹窗，并阻塞操作，直到用户点击确定
    QMessageBox::information(this, "下线提示", "同账号异地登录，该终端下线！");
    TcpMgr::GetInstance()->CloseConnection();
    offlineLogin();
}

void MainWindow::slot_connection_close()
{
    // 出现弹窗，并阻塞操作，直到用户点击确定
    QMessageBox::information(this, "下线提示", "心跳检测超时，该终端下线！");
    TcpMgr::GetInstance()->CloseConnection();
    offlineLogin();
}

// 离线后切换到登录状态
void MainWindow::offlineLogin()
{
    if (_ui_status == UIStatus::LOGIN_UI) {
        return;
    }

    // 离线后切换到登录状态
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    // 设置当前minwindow的最大值与最小值
    this->setMaximumSize(300,500);
    this->setMinimumSize(300,500);
    this->resize(300, 500);

    _chat_dlg->hide();
    _login_dlg->show();
    // 连接登录界面和注册界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reg, this, &MainWindow::slot_login_switch_reg);
    // 连接登录界面和忘记密码界面
    connect(_login_dlg, &LoginDialog::sig_login_switch_reset, this, &MainWindow::slot_login_switch_reset);
    _ui_status = UIStatus::LOGIN_UI;
}
