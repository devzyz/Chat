#include "resetdialog.h"
#include "logmgr.h"
#include "ui_resetdialog.h"
#include "httpmgr.h"

ResetDialog::ResetDialog(AuthFlowCoordinator &authFlow, QWidget *parent)
    : QDialog(parent), ui(new Ui::ResetDialog), _authFlow(authFlow)
{
    ui->setupUi(this);

    // 密码显示格式
    ui->password_edit->setEchoMode(QLineEdit::Password);

    // 错误提示信息颜色
    ui->err_tip->setProperty("state", "normal");
    repolish(ui->err_tip);

    // 连接单例类的信号与当前类的槽
    connect(HttpMgr::instance().get(), &HttpMgr::passwordResetHttpFinished,
            this, &ResetDialog::resetModFinish);

    initHttpHandlers();

    // 连接信号与槽，当QLabel编辑完成信号触发后，调用对应的检查函数，并进行提示
    connect(ui->user_edit, &QLineEdit::editingFinished, this,
        /** @brief 用户名编辑结束时校验。 */
        [this](){
        checkUserValid();
    });

    connect(ui->email_edit, &QLineEdit::editingFinished, this,
        /** @brief 邮箱编辑结束时校验。 */
        [this](){
        checkEmailValid();
    });

    connect(ui->password_edit, &QLineEdit::editingFinished, this,
        /** @brief 密码编辑结束时校验。 */
        [this](){
        checkPasswordValid();
    });

    connect(ui->varify_edit, &QLineEdit::editingFinished, this,
        /** @brief 验证码编辑结束时校验。 */
        [this](){
        checkVarifyValid();
    });

    ui->password_visible->setState("invisible_leave", "invisible_hover", "",
                                   "visible_leave", "visible_hover", "");

    // 连接槽函数，触发真正的密码的隐藏与显示
    connect(ui->password_visible, &ClickedLabel::clicked, this,
        /** @brief 按标签状态切换密码可见性。 */
        [this]() {
        auto state = ui->password_visible->getCurState();

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

void ResetDialog::resetModFinish(AuthFlowId flowId, ReqId id, QString res, ErrorCodes err)
{
    AuthOutcome outcome;
    outcome.module = static_cast<int>(Modules::RESETMOD);
    outcome.requestId = static_cast<int>(id);
    if(err != ErrorCodes::SUCCESS){
        outcome.kind = AuthOutcomeKind::HttpNetworkError;
        const AuthAction action = _authFlow.reduce(flowId, outcome);
        if (action.kind == AuthActionKind::StayAndShowError) {
            showAuthError(action.error);
        }
        return;
    }
    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    //json解析错误
    if(jsonDoc.isNull()){
        outcome.kind = AuthOutcomeKind::HttpMalformedJson;
        const AuthAction action = _authFlow.reduce(flowId, outcome);
        if (action.kind == AuthActionKind::StayAndShowError) {
            showAuthError(action.error);
        }
        return;
    }
    //json解析错误
    if(!jsonDoc.isObject()){
        outcome.kind = AuthOutcomeKind::HttpMalformedJson;
        const AuthAction action = _authFlow.reduce(flowId, outcome);
        if (action.kind == AuthActionKind::StayAndShowError) {
            showAuthError(action.error);
        }
        return;
    }
    const QJsonObject object = jsonDoc.object();
    const int businessError = object["error"].toInt();
    outcome.businessError = businessError;
    outcome.kind = businessError == ErrorCodes::SUCCESS
        ? AuthOutcomeKind::HttpSuccess : AuthOutcomeKind::HttpBusinessError;
    const AuthAction action = _authFlow.reduce(flowId, outcome);
    if (action.kind == AuthActionKind::StayAndShowError) {
        showAuthError(action.error);
        return;
    }
    const auto handler = _handlers.constFind(id);
    if (action.accepted && handler != _handlers.cend()) {
        (*handler)(object);
    }
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

void ResetDialog::showAuthError(AuthError error)
{
    showTip(error == AuthError::Network ? tr("网络请求错误")
                                        : error == AuthError::MalformedResponse
                                            ? tr("json解析错误") : tr("参数错误"),
            false);
}

// 添加对网络请求返回的json对象的处理
void ResetDialog::initHttpHandlers()
{
    // 重置密码获取验证码回包的逻辑
    _handlers.insert(ReqId::ID_GET_VERIFY_CODE,
        /** @brief 将验证码回复转换为表单提示。 */
        [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            return ;
        }

        showTip(tr("验证码已经发送到邮箱，注意查收"), true);
        SPDLOG_INFO("password-reset verification code sent");
    });

    // 重置密码的回包逻辑
    _handlers.insert(ReqId::ID_RESET_PWD,
        /** @brief 将重置密码回复转换为成功或失败提示。 */
        [this](const QJsonObject& jsonObj) {
        int error = jsonObj["error"].toInt();

        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            return ;
        }

        showTip(tr("密码重置成功"), true);
        SPDLOG_INFO("password reset succeeded");
    });
}

// 添加错误信息到错误map里面
void ResetDialog::addTipErr(TipErr te, QString tips) {
    _tip_errs[te] = tips;
    showTip(tips, false);
}

// 删除错误信息，如果map内还有未处理的错误，则继续显示
void ResetDialog::delTipErr(TipErr te) {
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
        addTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
        return false;
    }
    delTipErr(TipErr::TIP_USER_ERR);
    return true;
}

bool ResetDialog::checkEmailValid() {
    auto email = ui->email_edit->text();

    // 通过正则表达式匹配邮箱格式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();

    if (!match) {
        // 提示邮箱不正确
        addTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱地址不正确"));
        return false;
    }

    delTipErr(TipErr::TIP_EMAIL_ERR);
    return true;
}

bool ResetDialog::checkPasswordValid() {
    auto pass = ui->password_edit->text();

    if (pass.length() < 6 || pass.length() > 15) {
        // 提示长度不匹配
        addTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为6~15"));
        return false;
    }

    // 用正则表达式匹配密码
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
    bool match = regExp.match(pass).hasMatch();

    if (!match) {
        // 提示字符非法
        addTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符"));
        return false;
    }

    delTipErr(TipErr::TIP_PWD_ERR);
    return true;
}

bool ResetDialog::checkVarifyValid() {
    auto varify = ui->varify_edit->text();

    if (varify.isEmpty()) {
        addTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
        return false;
    }

    if (varify.length() != 4) {
        addTipErr(TipErr::TIP_VARIFY_ERR, tr("请输入4位验证码"));
        return false;
    }

    delTipErr(TipErr::TIP_VARIFY_ERR);
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

    AuthOutcome begin;
    begin.kind = AuthOutcomeKind::BeginHttp;
    begin.module = static_cast<int>(Modules::RESETMOD);
    begin.requestId = static_cast<int>(ReqId::ID_RESET_PWD);
    const AuthFlowId flowId = _authFlow.reduce(0, begin).flowId;
    HttpMgr::instance()->postHttpReq(QUrl(gate_url_prefix + "/reset_pwd"), json_obj,
                                        ReqId::ID_RESET_PWD, Modules::RESETMOD,
                                        flowId);
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
    AuthOutcome begin;
    begin.kind = AuthOutcomeKind::BeginHttp;
    begin.module = static_cast<int>(Modules::RESETMOD);
    begin.requestId = static_cast<int>(ReqId::ID_GET_VERIFY_CODE);
    const AuthFlowId flowId = _authFlow.reduce(0, begin).flowId;
    HttpMgr::instance()->postHttpReq(QUrl(gate_url_prefix + "/get_varifycode"), json_obj,
                                        ReqId::ID_GET_VERIFY_CODE, Modules::RESETMOD,
                                        flowId);
}


void ResetDialog::on_cancel_btn_clicked()
{
    emit loginRequested();
}
