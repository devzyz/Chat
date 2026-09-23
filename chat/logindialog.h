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
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~LoginDialog();

private:
    Ui::LoginDialog *ui;
    /** @brief 显示表单提示并按成功或失败状态选择样式。 */
    void showTip(QString str, bool isOk);
    // 用于显示注册出现的错误
    QMap<TipErr, QString> _tip_errs;
    // 注册错误
    /** @brief 注册错误。 */
    void addTipErr(TipErr te, QString tips);
    // 移除错误
    /** @brief 移除错误。 */
    void delTipErr(TipErr te);

    // 用于检查邮箱和密码有没有错误
    /** @brief 用于检查邮箱和密码有没有错误。 */
    bool checkEmailValid();
    /** @brief 校验密码输入并更新对应错误提示，返回是否通过。 */
    bool checkPasswordValid();

    // 初始化登录头像
    /** @brief 初始化登录头像。 */
    void initHead();

    /** @brief 将结构化认证错误转换为表单提示。 */
    void showAuthError(AuthError error);

    ClientLoginFlow _loginFlow;
signals:
    /** @brief 通知主窗口从登录页切换到注册页。 */
    void registrationRequested();
    /** @brief 通知主窗口从登录页切换到密码重置页。 */
    void passwordResetRequested();
    /** @brief 通知认证成功的流程 ID，供主窗口切换到聊天界面。 */
    void loginSucceeded(AuthFlowId flowId);
private slots:
    /** @brief 校验表单后发起关联认证流程的登录请求。 */
    void on_login_btn_clicked();
};

#endif // LOGINDIALOG_H
