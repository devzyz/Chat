#include "resetdialog.h"
#include "ui_resetdialog.h"
#include "httpmgr.h"

ResetDialog::ResetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ResetDialog)
{
    ui->setupUi(this);

    // 密码显示格式
    ui->password_edit->setEchoMode(QLineEdit::Password);

    // 错误提示信息颜色
    ui->err_tip->setProperty("state", "normal");
    repolish(ui->err_tip);

    // 连接单例类的信号与当前类的槽
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reset_mod_finish,
            this, &ResetDialog::slot_reset_mod_finish);

    initHttpHandlers();

    // 连接信号与槽，当QLabel编辑完成信号触发后，调用对应的检查函数，并进行提示
    connect(ui->user_edit, &QLineEdit::editingFinished, this, [this](){
        checkUserValid();
    });

    connect(ui->email_edit, &QLineEdit::editingFinished, this, [this](){
        checkEmailValid();
    });

    connect(ui->password_edit, &QLineEdit::editingFinished, this, [this](){
        checkPasswordValid();
    });

    connect(ui->varify_edit, &QLineEdit::editingFinished, this, [this](){
        checkVarifyValid();
    });

    ui->password_visible->SetState("invisible_leave", "invisible_hover", "",
                                   "visible_leave", "visible_hover", "");

    // 连接槽函数，触发真正的密码的隐藏与显示
    connect(ui->password_visible, &ClickedLabel::clicked, this, [this]() {
        auto state = ui->password_visible->GetCurState();

        // 当为隐藏状态时，切换编辑框为密码模式;否则为显示模式
        if (state == ClickLabelState::Normal) {
            ui->password_edit->setEchoMode(QLineEdit::Password);
        }else {
            ui->password_edit->setEchoMode(QLineEdit::Normal);
        }
    });
}

/**
 * @brief ResetDialog::~ResetDialog
 * 析构函数
 */
ResetDialog::~ResetDialog()
{
    delete ui;
}

void ResetDialog::slot_reset_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    if(err != ErrorCodes::SUCCESS){
        showTip(tr("网络请求错误"),false);
        return;
    }
    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    //json解析错误
    if(jsonDoc.isNull()){
        showTip(tr("json解析错误"),false);
        return;
    }
    //json解析错误
    if(!jsonDoc.isObject()){
        showTip(tr("json解析错误"),false);
        return;
    }
    //调用对应的逻辑,根据id回调。
    _handlers[id](jsonDoc.object());
    return;
}

// 用于显示错误信息
void ResetDialog::showTip(QString str, bool isOk) {
    if (isOk) {
        ui->err_tip->setProperty("state", "normal");
    }else {
        ui->err_tip->setProperty("state", "err");
    }

    ui->err_tip->setText(str);
    repolish(ui->err_tip);
}

// 添加对网络请求返回的json对象的处理
void ResetDialog::initHttpHandlers()
{
    // 重置密码获取验证码回包的逻辑
    _handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            return ;
        }

        auto email = jsonObj["email"].toString();
        showTip(tr("验证码已经发送到邮箱，注意查收"), true);
        qDebug() << "email is " << email;
    });

    // 重置密码的回包逻辑
    _handlers.insert(ReqId::ID_RESET_PWD, [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();

        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            return ;
        }

        auto email = jsonObj["email"].toString();
        showTip(tr("密码重置成功"), true);
        qDebug() << "email is " << email;
        qDebug() << "user uid is " << jsonObj["uid"].toString();
    });
}

// 添加错误信息到错误map里面
void ResetDialog::AddTipErr(TipErr te, QString tips) {
    _tip_errs[te] = tips;
    showTip(tips, false);
}

// 删除错误信息，如果map内还有未处理的错误，则继续显示
void ResetDialog::DelTipErr(TipErr te) {
    _tip_errs.remove(te);
    if (_tip_errs.empty()) {
        ui->err_tip->setText("");
        return ;
    }
    showTip(_tip_errs.first(), false);
}

bool ResetDialog::checkUserValid()
{
    if (ui->user_edit->text() == "") {
        AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
        return false;
    }
    DelTipErr(TipErr::TIP_USER_ERR);
    return true;
}

bool ResetDialog::checkEmailValid() {
    auto email = ui->email_edit->text();

    // 通过正则表达式匹配邮箱格式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();

    if (!match) {
        // 提示邮箱不正确
        AddTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱地址不正确"));
        return false;
    }

    DelTipErr(TipErr::TIP_EMAIL_ERR);
    return true;
}

bool ResetDialog::checkPasswordValid() {
    auto pass = ui->password_edit->text();

    if (pass.length() < 6 || pass.length() > 15) {
        // 提示长度不匹配
        AddTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为6~15"));
        return false;
    }

    // 用正则表达式匹配密码
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
    bool match = regExp.match(pass).hasMatch();

    if (!match) {
        // 提示字符非法
        AddTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符"));
        return false;
    }

    DelTipErr(TipErr::TIP_PWD_ERR);
    return true;
}

bool ResetDialog::checkVarifyValid() {
    auto varify = ui->varify_edit->text();

    if (varify.isEmpty()) {
        AddTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
        return false;
    }

    if (varify.length() != 4) {
        AddTipErr(TipErr::TIP_VARIFY_ERR, tr("请输入4位验证码"));
        return false;
    }

    DelTipErr(TipErr::TIP_VARIFY_ERR);
    return true;
}


void ResetDialog::on_confirm_btn_clicked()
{
    auto user_match = checkUserValid();
    if (!user_match) {
        return ;
    }
    auto user_email = checkEmailValid();
    if (!user_email) {
        return ;
    }
    auto user_varify = checkVarifyValid();
    if (!user_varify) {
        return ;
    }
    auto user_password = checkPasswordValid();
    if (!user_password) {
        return ;
    }

    QJsonObject json_obj;
    json_obj["user"] = ui->user_edit->text();
    json_obj["email"] = ui->email_edit->text();
    json_obj["varify"] = ui->varify_edit->text();
    json_obj["password"] = xorString(ui->password_edit->text());

    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/reset_pwd"), json_obj,
                                        ReqId::ID_RESET_PWD, Modules::RESETMOD);
}


void ResetDialog::on_get_code_btn_clicked()
{
    auto email = ui->email_edit->text();
    auto match = checkEmailValid();

    if (!match) {
        return ;
    }

    // 发送http请求获取验证码
    QJsonObject json_obj;
    json_obj["email"] = email;
    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_varifycode"), json_obj,
                                        ReqId::ID_GET_VERIFY_CODE, Modules::RESETMOD);
}


void ResetDialog::on_cancel_btn_clicked()
{
    emit sig_reset_switch_login();
}

