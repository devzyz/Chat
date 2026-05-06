#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <global.h>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
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

    // 请求回包后的处理逻辑
    void initHttpHandlers();
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;

    int _uid;
    QString _token;
signals:
    void sig_login_switch_reg();
    void sig_login_switch_reset();
    void sig_connect_tcp(ServerInfo si);
private slots:
    void on_login_btn_clicked();
    void slot_login_mod_finish(ReqId id, QString res, ErrorCodes err);
    void slot_tcp_connect_finish(bool bSuccess);
};

#endif // LOGINDIALOG_H
