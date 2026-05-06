#include "registerdialog.h"
#include "ui_registerdialog.h"
#include "global.h"
#include "httpmgr.h"

RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
{
    ui->setupUi(this);

    // 密码显示格式
    ui->password_edit->setEchoMode(QLineEdit::Password);
    ui->confirm_edit->setEchoMode(QLineEdit::Password);

    // 错误提示信息颜色
    ui->err_tip->setProperty("state", "normal");
    repolish(ui->err_tip);

    // 连接单例类的信号与当前类的槽
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reg_mod_finish,
            this, &RegisterDialog::slot_reg_mod_finish);

    // 网络请求的回调函数
    initHttpHandlers();
    ui->err_tip->setText("");

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

    connect(ui->confirm_edit, &QLineEdit::editingFinished, this, [this](){
        checkConfirmValid();
    });

    connect(ui->varify_edit, &QLineEdit::editingFinished, this, [this](){
        checkVarifyValid();
    });

    ui->password_visible->SetState("invisible_leave", "invisible_hover", "",
                                   "visible_leave", "visible_hover", "");

    ui->confirm_visible->SetState("invisible_leave", "invisible_hover", "",
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

    connect(ui->confirm_visible, &ClickedLabel::clicked, this, [this]() {
        auto state = ui->confirm_visible->GetCurState();

        // 当为隐藏状态时，切换编辑框为密码模式;否则为显示模式
        if (state == ClickLabelState::Normal) {
            ui->confirm_edit->setEchoMode(QLineEdit::Password);
        }else {
            ui->confirm_edit->setEchoMode(QLineEdit::Normal);
        }
    });

    // 创建定时器，用于注册成功后等待5s返回登录界面
    _return_login_counter = 5;
    _return_login_timer = new QTimer(this);

    // 连接信号与槽
    connect(_return_login_timer, &QTimer::timeout, [this]() {
        if (_return_login_counter == 0) {
            _return_login_timer->stop();
            emit sig_reg_switch_login();
            return;
        }
        _return_login_counter --;
        auto str = QString("注册成功，%l s后返回登录").arg(_return_login_counter);
        ui->return_label_tip_1->setText(str);
    });
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

// 获取验证码按钮的点击事件
void RegisterDialog::on_get_code_clicked()
{
    auto match = checkVarifyValid();
    auto email = ui->email_edit->text();
    if (match) {
        // 发送http验证码
        QJsonObject json_obj;
        json_obj["email"] = email;
        HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/get_varifycode"),
                                            json_obj, ReqId::ID_GET_VERIFY_CODE, Modules::REGISTERMOD);
    }else {
        showTip("邮箱地址不正确", false);
    }
}

// 与httpmgr内sig_reg_mod_finish连接的槽函数，用于处理http完成后的工作
void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    if (err != ErrorCodes::SUCCESS) {
        showTip(tr("网络请求错误"), false);
        return ;
    }

    // 解析JSON 字符串，res转化为QByteArray类型
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());

    if (jsonDoc.isNull()) {
        showTip(tr("json解析失败"), false);
        return ;
    }

    //json 解析错误
    if (!jsonDoc.isObject()) {
        showTip(tr("json解析失败"), false);
        return ;
    }

    // 处理
    _handlers[id](jsonDoc.object());
    return ;

}

// 用于显示错误信息
void RegisterDialog::showTip(QString str, bool isOk) {
    if (isOk) {
        ui->err_tip->setProperty("state", "normal");
    }else {
        ui->err_tip->setProperty("state", "err");
    }

    ui->err_tip->setText(str);
    repolish(ui->err_tip);
}

// 添加对网络请求返回的json对象的处理
void RegisterDialog::initHttpHandlers()
{
    // 注册获取验证码回包的逻辑
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

    // 注册的回包逻辑
    _handlers.insert(ReqId::ID_REG_USER, [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();

        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            return ;
        }

        auto email = jsonObj["email"].toString();
        changeTipPage();
        showTip(tr("用户注册成功"), true);
        qDebug() << "email is " << email;
        qDebug() << "user uid is " << jsonObj["uid"].toString();
    });
}

// 添加错误信息到错误map里面
void RegisterDialog::AddTipErr(TipErr te, QString tips) {
    _tip_errs[te] = tips;
    showTip(tips, false);
}

// 删除错误信息，如果map内还有未处理的错误，则继续显示
void RegisterDialog::DelTipErr(TipErr te) {
    _tip_errs.remove(te);
    if (_tip_errs.empty()) {
        ui->err_tip->setText("");
        return ;
    }
    showTip(_tip_errs.first(), false);
}

bool RegisterDialog::checkUserValid()
{
    if (ui->user_edit->text() == "") {
        AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
        return false;
    }
    DelTipErr(TipErr::TIP_USER_ERR);
    return true;
}

bool RegisterDialog::checkEmailValid() {
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

bool RegisterDialog::checkPasswordValid() {
    auto pass = ui->password_edit->text();
    auto confirm = ui->confirm_edit->text();

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

    if (pass != confirm) {
        // 提示密码与确认密码不匹配
        AddTipErr(TipErr::TIP_PWD_CONFIRM, tr("密码和确认密码不匹配"));
        return false;
    }else {
        DelTipErr(TipErr::TIP_PWD_CONFIRM);
    }

    return true;
}

bool RegisterDialog::checkConfirmValid() {
    auto pass = ui->password_edit->text();
    auto confirm = ui->confirm_edit->text();

    if (confirm.length() < 6 || confirm.length() > 15) {
        // 提示长度不匹配
        AddTipErr(TipErr::TIP_CONFIRM_ERR, tr("确认密码长度应为6~15"));
        return false;
    }

    // 用正则表达式匹配密码
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
    bool match = regExp.match(confirm).hasMatch();

    if (!match) {
        // 提示字符非法
        AddTipErr(TipErr::TIP_CONFIRM_ERR, tr("不能包含非法字符"));
        return false;
    }

    DelTipErr(TipErr::TIP_CONFIRM_ERR);

    if (pass != confirm) {
        // 提示密码与确认密码不匹配
        AddTipErr(TipErr::TIP_PWD_CONFIRM, tr("确认密码和密码不匹配"));
        return false;
    }else {
        DelTipErr(TipErr::TIP_PWD_CONFIRM);
    }

    return true;
}

bool RegisterDialog::checkVarifyValid() {
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

// 定时器时间到，
void RegisterDialog::changeTipPage()
{
    _return_login_timer->stop();
    ui->stackedWidget->setCurrentWidget(ui->page_2);

    // 启动定时器
    _return_login_counter = 5;
    _return_login_timer->start(1000);
}

// 点击确认注册按钮后的事件
void RegisterDialog::on_confirm_btn_clicked()
{
    auto check_user = checkUserValid();
    if(!check_user){
        return;
    }
    auto check_email = checkEmailValid();
    if(!check_email){
        return;
    }
    auto check_password = checkPasswordValid();
    if(!check_password){
        return;
    }
    auto check_confirm = checkConfirmValid();
    if(!check_confirm){
        return;
    }
    auto check_varify = checkVarifyValid();
    if(!check_varify){
        return;
    }

    QJsonObject json_obj;
    json_obj["user"] = ui->user_edit->text();
    json_obj["email"] = ui->email_edit->text();
    // 对密码明文进行加密存储
    json_obj["passwd"] = xorString(ui->password_edit->text());
    json_obj["confirm"] = xorString(ui->confirm_edit->text());
    json_obj["varifycode"] = ui->varify_edit->text();

    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/user_register"),
                                        json_obj, ReqId::ID_REG_USER, Modules::REGISTERMOD);
}

// 点击返回登录按钮的事件
void RegisterDialog::on_return_btn_clicked()
{
    _return_login_timer->stop();
    emit sig_reg_switch_login();
}

// 点击取消登录，也进行跳转
void RegisterDialog::on_cancel_btn_clicked()
{
    _return_login_timer->stop();
    emit sig_reg_switch_login();
}

