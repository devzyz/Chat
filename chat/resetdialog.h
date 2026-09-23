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
    /** @brief 初始化对象，用于维护重置密码表单校验及请求结果。 */
    explicit ResetDialog(AuthFlowCoordinator &authFlow, QWidget *parent = nullptr);

    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ResetDialog();

private slots:
    /** @brief 将 HTTP 重置结果交给认证协调器，忽略过期结果。 */
    void resetModFinish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err);

    /** @brief 校验输入后创建流程并提交密码重置请求。 */
    void on_confirm_btn_clicked();

    /** @brief 校验邮箱后发起密码重置验证码请求。 */
    void on_get_code_btn_clicked();

    /** @brief 取消当前表单操作并通知返回登录页。 */
    void on_cancel_btn_clicked();

signals:
    /** @brief 通知主窗口由密码重置页返回登录页。 */
    void loginRequested();

private:
    Ui::ResetDialog* ui;
    /** @brief 显示表单提示并按成功或失败状态选择样式。 */
    void showTip(QString str, bool isOk);
    /** @brief 将结构化认证错误转换为表单提示。 */
    void showAuthError(AuthError error);
    /** @brief 注册当前表单按请求 ID 分发的 HTTP 处理器。 */
    void initHttpHandlers();
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;

    // 用于显示注册出现的错误
    QMap<TipErr, QString> _tip_errs;
    // 注册错误
    /** @brief 注册错误。 */
    void addTipErr(TipErr te, QString tips);
    // 移除错误
    /** @brief 移除错误。 */
    void delTipErr(TipErr te);

    // 用于检查注册所有输入是否符合要求的
    /** @brief 用于检查注册所有输入是否符合要求的。 */
    bool checkUserValid();
    /** @brief 校验邮箱输入并更新对应错误提示，返回是否通过。 */
    bool checkEmailValid();
    /** @brief 校验密码输入并更新对应错误提示，返回是否通过。 */
    bool checkPasswordValid();
    /** @brief 校验验证码输入并更新提示，返回是否通过。 */
    bool checkVarifyValid();
    AuthFlowCoordinator &_authFlow;

};

#endif // RESETDIALOG_H
