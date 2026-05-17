#include "logindialog.h"
#include "ui_logindialog.h"
#include "httpmgr.h"
#include <QPainter>
#include <QPainterPath>
#include "tcpmgr.h"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);

    // 页面切换逻辑，登录切换注册，登录切换忘记密码
    connect(ui->register_btn, &QPushButton::clicked, this, &LoginDialog::sig_login_switch_reg);

    connect(ui->forget_label, &ClickedLabel::clicked, this, &LoginDialog::sig_login_switch_reset);

    // 忘记密码高亮显示逻辑
    ui->forget_label->SetState("invisible_leave","invisible_hover","","visible_leave","visible_hover","");

    // 错误提示信息颜色
    ui->err_tip->setProperty("state", "normal");
    repolish(ui->err_tip);
    // 绑定email和password实时检验信号
    connect(ui->email_edit, &QLineEdit::editingFinished, [this]() {
        checkEmailValid();
    });
    connect(ui->password_edit, &QLineEdit::editingFinished, [this]() {
        checkPasswordValid();
    });

    // 密码隐藏逻辑
    ui->password_visible->SetState("invisible_leave","invisible_hover","","visible_leave","visible_hover","");
    ui->password_edit->setEchoMode(QLineEdit::Password);
    connect(ui->password_visible, &ClickedLabel::clicked, [this](){
        auto curState = ui->password_visible->GetCurState();

        if (curState == ClickLabelState::Normal) {
            ui->password_edit->setEchoMode(QLineEdit::Password);
        }else {
            ui->password_edit->setEchoMode(QLineEdit::Normal);
        }
    });

    // 头像处理逻辑
    initHead();
    // 网络请求回包逻辑
    initHttpHandlers();

    // 连接信号与槽，httpmgr中发送登录信号处理完成后，调用slot_login_mod_finish槽函数，触发网络请求回包逻辑
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_login_mod_finish, this, &LoginDialog::slot_login_mod_finish);

    // 连接tcp连接请求的信号和槽函数
    connect(this, &LoginDialog::sig_connect_tcp, TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);

    // 连接tcp连接完成信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_tcp_connect_success, this, &LoginDialog::slot_tcp_connect_finish);
}

// 用于显示错误信息
void LoginDialog::showTip(QString str, bool isOk) {
    if (isOk) {
        ui->err_tip->setProperty("state", "normal");
    }else {
        ui->err_tip->setProperty("state", "err");
    }

    ui->err_tip->setText(str);
    repolish(ui->err_tip);
}

// 添加错误信息到错误map里面
void LoginDialog::AddTipErr(TipErr te, QString tips) {
    _tip_errs[te] = tips;
    showTip(tips, false);
}

// 删除错误信息，如果map内还有未处理的错误，则继续显示
void LoginDialog::DelTipErr(TipErr te) {
    _tip_errs.remove(te);
    if (_tip_errs.empty()) {
        ui->err_tip->setText("");
        return ;
    }
    showTip(_tip_errs.first(), false);
}

bool LoginDialog::checkEmailValid() {
    auto email = ui->email_edit->text();

    if (email.isEmpty()) {
        // 提示邮箱不正确
        AddTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱不能为空"));
        return false;
    }

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

bool LoginDialog::checkPasswordValid() {
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

/**
 * @brief LoginDialog::initHead
 *
 * 加载原图片 --> 缩放原图片与显示框匹配 --> 创建与缩放后图片尺寸等大的新透明画布 --> 创建绘画者绑定透明画布
 * --> 对绘画者添加裁剪区域限制 --> 通过绘画者的drawPixmap函数，将原始图像绘制到其绑定的画布上
 */
void LoginDialog::initHead()
{
    // 加载图片
    QPixmap originalPixmap(":/res/head_1.jpg");
    // 设置图片自动缩放
    qDebug() << originalPixmap.size() << ui->head_label->size();

    // 缩放到head_label的尺寸。保持原来的宽高比。采用平滑缩放算法，避免锯齿
    originalPixmap = originalPixmap.scaled(ui->head_label->size(),
                                           Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 创建一个和原始图片相同大小的QPixmap, 用于绘制圆角图片
    QPixmap rounderPixmap(originalPixmap.size());
    rounderPixmap.fill(Qt::transparent); // 用透明色填充

    // 绑定画布，以及设置以后变换时的策略
    QPainter painter(&rounderPixmap);
    painter.setRenderHint(QPainter::Antialiasing); // 开启抗锯齿
    painter.setRenderHint(QPainter::SmoothPixmapTransform); // 采用平滑算法

    // 使用QPainterPath设置圆角
    QPainterPath path;
    path.addRoundedRect(0, 0, originalPixmap.width(), originalPixmap.height(), 10, 10); // 设置四个角的圆角
    painter.setClipPath(path);

    // 将原始图片绘制到rounderPixmap上
    painter.drawPixmap(0, 0, originalPixmap);

    // 设置绘制好的圆角图片到QLabel上
    ui->head_label->setPixmap(rounderPixmap);
}

void LoginDialog::initHttpHandlers()
{
    // 登录的回包逻辑
    _handlers.insert(ReqId::ID_LOGIN_UESR, [this](QJsonObject jsonObj) {
        int err = jsonObj["error"].toInt();

        if (err != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            ui->login_btn->setEnabled(true);
            return;
        }

        auto email = jsonObj["email"].toString();
        // 发送信号通知tcpMgr发送长连接
        ServerInfo si;
        si.Uid = jsonObj["uid"].toInt();
        si.Host = jsonObj["host"].toString();
        si.Port = jsonObj["port"].toString();
        si.Token = jsonObj["token"].toString();

        _uid = si.Uid;
        _token = si.Token;

        qDebug() << "email is " << email << " uid is " << si.Uid << " host is " << si.Host
                 << " Port is " << si.Port << " Token is " << si.Token;
        emit sig_connect_tcp(si);
    });
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::on_login_btn_clicked()
{
    if (!checkEmailValid()) {
        return ;
    }
    if (!checkPasswordValid()) {
        return ;
    }

    ui->login_btn->setEnabled(false);
    auto email = ui->email_edit->text();
    auto password = ui->password_edit->text();

    QJsonObject json_obj;
    json_obj["email"] = email;
    json_obj["password"] = xorString(password);

    HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix + "/user_login"), json_obj,
                                        ReqId::ID_LOGIN_UESR, Modules::LOGINMOD);
}

void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
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

    _handlers[id](jsonDoc.object());
    return;
}

/**
 * @brief LoginDialog::slot_tcp_connect_finish
 * @param bSuccess
 * tcpMgr发送连接结束，在这里进行连接结束的处理
 */
void LoginDialog::slot_tcp_connect_finish(bool bSuccess)
{
    if (bSuccess) {
        showTip(tr("聊天服务器连接成功，正在登录..."), true);

        QJsonObject jsonObj;
        jsonObj["uid"] = _uid;
        jsonObj["token"] = _token;

        QJsonDocument doc(jsonObj);
        QString jsonString = doc.toJson(QJsonDocument::Indented);

        // 发送tcp请求给chat server请求连接
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CHAT_LOGIN, jsonString);
    }else {
        showTip(tr("网络异常"), false);
        ui->login_btn->setEnabled(true);
    }
}

