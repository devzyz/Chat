#include "authfrienddialog.h"
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
    ui->send_auth_user_name_edit->setPlaceholderText(tr("username")); // 我的名字
    ui->send_auth_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::slot_auth_apply_cancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::slot_auth_apply_sure);
}

AuthFriendDialog::~AuthFriendDialog()
{
    delete ui;
    qDebug() << "Auth Friend Dialog is destructed";
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
    qDebug() << "send auth apply sure";
    // 准备tcp请求，发送认证信息
    QJsonObject jsonObj;
    auto uid = UserMgr::GetInstance()->GetUid();
    jsonObj["fromuid"] = uid; // 我的uid
    jsonObj["touid"] = _apply_info->_uid; // 申请人uid
    QString back_name = "";
    if (ui->send_auth_user_back_edit->text().isEmpty()) {
        back_name = ui->send_auth_user_back_edit->placeholderText();
    }else {
        back_name = ui->send_auth_user_back_edit->text();
    }
    jsonObj["backname"] = back_name;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 发送tcp请求给chat server进行认证
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_AUTH_FRIEND_REQ, jsonData);

    this->hide();
    this->deleteLater();
}

