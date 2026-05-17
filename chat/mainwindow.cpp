#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "tcpmgr.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);
    _login_dlg->show();

    // 创建和注册消息链接
    connect(_login_dlg, &LoginDialog::sig_login_switch_reg, this, &MainWindow::slot_login_switch_reg);
    connect(_login_dlg, &LoginDialog::sig_login_switch_reset, this, &MainWindow::slot_login_switch_reset);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_login_switch_chat, this, &MainWindow::slot_login_switch_chat);

    emit TcpMgr::GetInstance()->sig_login_switch_chat();
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
}

void MainWindow::slot_login_switch_reset()
{
    _reset_dlg = new ResetDialog(this);
    _reset_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_reset_dlg);

    _login_dlg->hide();
    _reset_dlg->show();

    connect(_reset_dlg, &ResetDialog::sig_reset_switch_login, this, &MainWindow::slot_reset_switch_login);
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
}

void MainWindow::slot_login_switch_chat() {
    _chat_dlg = new ChatDialog(this);
    _chat_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_chat_dlg);

    _login_dlg->hide();
    _chat_dlg->show();

    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}
