#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>
#include "global.h"

namespace Ui {
    class RegisterDialog;
}

class RegisterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegisterDialog(QWidget *parent = nullptr);
    ~RegisterDialog();

private slots:
    void on_get_code_clicked();
    void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);

    void on_confirm_btn_clicked();

    void on_return_btn_clicked();

    void on_cancel_btn_clicked();

private:
    Ui::RegisterDialog *ui;
    void showTip(QString str, bool isOk);
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
    bool checkConfirmValid();
    bool checkVarifyValid();

    // 用于从注册成功后的页面，返回登录页面的定时器
    QTimer* _return_login_timer;
    int _return_login_counter;
    void changeTipPage();

signals:
    void sig_reg_switch_login();
};

#endif // REGISTERDIALOG_H
