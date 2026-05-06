#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include <QDialog>
#include "global.h"

namespace Ui {
    class ResetDialog;
}

class ResetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResetDialog(QWidget *parent = nullptr);

    ~ResetDialog();

private slots:
    void slot_reset_mod_finish(ReqId id, QString res, ErrorCodes err);

    void on_confirm_btn_clicked();

    void on_get_code_btn_clicked();

    void on_cancel_btn_clicked();

signals:
    void sig_reset_switch_login();

private:
    Ui::ResetDialog* ui;
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
    bool checkVarifyValid();

};

#endif // RESETDIALOG_H
