#include "authfrienddialog.h"
#include "logmgr.h"
#include "ui_authfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"

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
    QJsonObject jsonObj;
    auto uid = UserMgr::GetInstance()->GetUid();
    jsonObj["authuid"] = uid; // 被申请人uid
    jsonObj["applyuid"] = _apply_info->_apply_uid; // 申请人uid

    QJsonObject applyinfo;
    QJsonObject authinfo;

    // 将申请人信息添加上
    applyinfo["applyuid"] = _apply_info->_apply_uid;
    applyinfo["applyname"] = _apply_info->_apply_name;
    applyinfo["applydescription"] = _apply_info->_apply_description;
    applyinfo["applyicon"] = _apply_info->_apply_icon;
    applyinfo["applysex"] = _apply_info->_apply_sex;
    applyinfo["touid"] = _apply_info->_to_uid;
    applyinfo["description"] = _apply_info->_description;
    applyinfo["backname"] = _apply_info->_backname;
    jsonObj["applyinfo"] = applyinfo;


    auto self_info = UserMgr::GetInstance()->GetUserInfo();
    // 将被申请人的信息添加上
    authinfo["authuid"] = self_info->_uid;
    authinfo["authname"] = self_info->_name;
    authinfo["authdescription"] = self_info->_description;
    authinfo["authicon"] = self_info->_icon;
    authinfo["authsex"] = self_info->_sex;
    authinfo["touid"] = _apply_info->_apply_uid;

    QString description = ui->send_auth_user_description_edit->text();
    if (description.isEmpty()) {
        description = "你好！";
    }

    QString back_name = ui->send_auth_user_back_edit->text();
    if (back_name.isEmpty()) {
        back_name = _apply_info->_apply_name;
    }

    authinfo["description"] = description;
    authinfo["backname"] = back_name;
    jsonObj["authinfo"] = authinfo;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 发送tcp请求给chat server进行认证
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_AUTH_FRIEND_REQ, jsonData);

    this->hide();
    this->deleteLater();
}

