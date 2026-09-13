#include "authfrienddialog.h"
#include "logmgr.h"
#include "ui_authfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"
#include "clientrequests.h"

AuthFriendDialog::AuthFriendDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AuthFriendDialog)
{
    ui->setupUi(this);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setModal(true);

    // 设置编辑框默认文本
    ui->send_auth_user_description_edit->setPlaceholderText(tr("username")); // 我的名字
    ui->send_auth_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::slot_auth_apply_cancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::slot_auth_apply_sure);
}

AuthFriendDialog::~AuthFriendDialog()
{
    delete ui;
    SPDLOG_DEBUG("AuthFriendDialog destructed");
}

void AuthFriendDialog::SetApplyInfo(std::shared_ptr<ApplyInfo> applyinfo)
{
    _apply_info = applyinfo;
}

void AuthFriendDialog::slot_auth_apply_cancel()
{
    this->hide();
    this->deleteLater();
}

/**
 * @brief AuthFriendDialog::slot_auth_apply_sure
 * 同意添加好友，发送tcp请求进行认证
 */
void AuthFriendDialog::slot_auth_apply_sure()
{
    SPDLOG_DEBUG("friend authentication confirmation submitted");
    // 准备tcp请求，发送认证信息
    const auto self_info = UserMgr::GetInstance()->GetUserInfo();
    QString description = ui->send_auth_user_description_edit->text();
    if (description.isEmpty()) {
        description = "你好！";
    }

    QString back_name = ui->send_auth_user_back_edit->text();
    if (back_name.isEmpty()) {
        back_name = _apply_info->_apply_name;
    }

    const auto jsonData = clientAcceptFriendRequest(*self_info, *_apply_info, description, back_name);

    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_AUTH_FRIEND_REQ, jsonData);

    this->hide();
    this->deleteLater();
}

