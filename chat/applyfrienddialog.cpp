#include "applyfrienddialog.h"
#include "logmgr.h"
#include "ui_applyfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"
#include "clientrequests.h"

ApplyFriendDialog::ApplyFriendDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ApplyFriendDialog)
{
    ui->setupUi(this);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setModal(true);

    // 设置编辑框默认文本
    ui->send_apply_user_description_edit->setPlaceholderText(tr("description")); // 我发送的申请描述信息
    ui->send_apply_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::slot_send_apply_cancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::slot_send_apply_sure);
}

ApplyFriendDialog::~ApplyFriendDialog()
{
    delete ui;
    SPDLOG_DEBUG("ApplyFriendDialog destructed");
}

void ApplyFriendDialog::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    _si = si;
}

/**
 * @brief ApplyFriendDialog::slot_send_apply_sure
 * 当在申请添加好友弹框出点击添加好友后触发的槽函数
 * 发送tcp添加好友请求
 */
void ApplyFriendDialog::slot_send_apply_sure() {
    SPDLOG_DEBUG("friend application confirmation submitted");
    // 设置发送请求的Json参数
    const auto user_info = UserMgr::GetInstance()->GetUserInfo();
    auto description = ui->send_apply_user_description_edit->text();
    // 如果为空，则用默认申请语句
    if (description.isEmpty()) {
        description = "你好！";
    }


    // 设置备注名
    auto backname = ui->send_apply_user_back_edit->text();
    if (backname.isEmpty()) {
        backname = _si->_name;
    }

    const auto jsonData = clientFriendRequest(*user_info, _si->_uid, description, backname);
    SPDLOG_DEBUG("friend application TCP request prepared");

    // 发送tcp请求
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_ADD_FRIEND_REQ, jsonData);

    this->hide();
    deleteLater();
}

void ApplyFriendDialog::slot_send_apply_cancel() {
    this->hide();
    deleteLater();
}
