#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <global.h>
#include "authflowcoordinator.h"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(AuthFlowCoordinator &authFlow, QWidget *parent = nullptr);
    ~LoginDialog();

private:
    Ui::LoginDialog *ui;
    void showTip(QString str, bool isOk);
    // 用于显示注册出现的错误
    QMap<TipErr, QString> _tip_errs;
    // 注册错误
    void AddTipErr(TipErr te, QString tips);
    // 移除错误
    void DelTipErr(TipErr te);

    // 用于检查邮箱和密码有没有错误
    bool checkEmailValid();
    bool checkPasswordValid();

    // 初始化登录头像
    void initHead();

    void showAuthError(AuthError error);

    int _uid;
    QString _token;
    AuthFlowCoordinator &_authFlow;
    AuthFlowId _flowId = 0;
signals:
    void sig_login_switch_reg();
    void sig_login_switch_reset();
    void sig_connect_tcp(ServerInfo si);
    void sig_login_switch_chat(AuthFlowId flowId);
private slots:
    void on_login_btn_clicked();
    void slot_login_mod_finish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);
    void slot_tcp_connect_finish(bool bSuccess);
    void slot_chat_login_failed(int error);
    void slot_chat_login_succeeded();
};

#endif // LOGINDIALOG_H
