#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include <QDialog>
#include "global.h"
#include "authflowcoordinator.h"

namespace Ui {
    class ResetDialog;
}

/** @brief 收集密码重置资料和验证码，提交请求并展示处理结果。 */
class ResetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResetDialog(AuthFlowCoordinator &authFlow, QWidget *parent = nullptr);

    ~ResetDialog();

private slots:
    /** @brief 将 HTTP 重置结果交给认证协调器，忽略过期结果。 */
    void slot_reset_mod_finish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);

    /** @brief 校验输入后创建流程并提交密码重置请求。 */
    void on_confirm_btn_clicked();

    /** @brief 校验邮箱后发起密码重置验证码请求。 */
    void on_get_code_btn_clicked();

    void on_cancel_btn_clicked();

signals:
    void sig_reset_switch_login();

private:
    Ui::ResetDialog* ui;
    void showTip(QString str, bool isOk);
    void showAuthError(AuthError error);
    void initHttpHandlers();
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;

    // 用于显示注册出现的错误
    QMap<TipErr, QString> _tip_errs;
    // 注册错误
    void AddTipErr(TipErr te, QString tips);
    // 移除错误
    void DelTipErr(TipErr te);

    // 用于检查注册所有输入是否符合要求的
    bool checkUserValid();
    bool checkEmailValid();
    bool checkPasswordValid();
    bool checkVarifyValid();
    AuthFlowCoordinator &_authFlow;

};

#endif // RESETDIALOG_H
