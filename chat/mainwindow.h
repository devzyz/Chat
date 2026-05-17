#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "logindialog.h"
#include "registerdialog.h"
#include "resetdialog.h"
#include "chatdialog.h"

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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void slot_login_switch_reg();
    void slot_reg_switch_login();
    void slot_login_switch_reset();
    void slot_reset_switch_login();
    void slot_login_switch_chat();
private:
    Ui::MainWindow *ui;
    LoginDialog * _login_dlg;
    RegisterDialog * _register_dlg;
    ResetDialog * _reset_dlg;
    ChatDialog * _chat_dlg;
};
#endif // MAINWINDOW_H
