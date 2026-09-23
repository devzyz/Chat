#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <global.h>
#include "clientloginflow.h"

namespace Ui {
class LoginDialog;
}

/** @brief 收集登录凭据，通过 ClientLoginFlow 展示认证结果。 */
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    /** @brief 创建登录表单并连接认证成功和失败事件。 */
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

    ClientLoginFlow _loginFlow;
signals:
    void sig_login_switch_reg();
    void sig_login_switch_reset();
    /** @brief 通知认证成功的流程 ID，供主窗口切换到聊天界面。 */
    void loginSucceeded(AuthFlowId flowId);
private slots:
    void on_login_btn_clicked();
};

#endif // LOGINDIALOG_H
