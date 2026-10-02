#include "usermgr.h"
#include "friendinfopage.h"
#include "logmgr.h"
#include "tcpmgr.h"
#include "socialui.h"
#include "messageservice.h"
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include "ui_friendinfopage.h"

FriendInfoPage::FriendInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FriendInfoPage)
{
    ui->setupUi(this);
    auto *remove = new QPushButton(tr("删除好友"),this); remove->setObjectName("deleteFriendButton"); layout()->addWidget(remove);
    auto *status = new QLabel(this); status->setWordWrap(true); layout()->addWidget(status);
    remove->setEnabled(TcpMgr::instance()->supportsSocial());
    connect(remove,&QPushButton::clicked,this,/** @brief 明确确认双向解除关系后提交带版本命令。 */ [this,remove,status] {
        if (!_friend_info) return;
        const int peer = _friend_info->_uid;
        const auto relation = UserMgr::instance()->socialProfile(peer);
        if (QMessageBox::question(this,tr("删除好友"),tr("解除双方好友关系并停止新消息；聊天记录仍然保留。是否继续？")) != QMessageBox::Yes) return;
        remove->setEnabled(false);
        TcpMgr::instance()->socialRequest(ID_FRIEND_MANAGE_REQ, {{"operation","delete"},{"target_uid",peer},
            {"expected_revision",relation["relationship_revision"]}}, this,
            /** @brief 结果只更新当前查看的好友页面。 */ [this,peer,remove,status](QJsonObject response) {
                if (!_friend_info || _friend_info->_uid != peer) { remove->setEnabled(true); return; }
                remove->setEnabled(response["error"].toInt(-1) != 0); status->setText(socialResultText(response));
            });
    });
    connect(UserMgr::instance()->messages(),&MessageService::directoryChanged,this,
        /** @brief 当前详情同步到已落盘的新资料或解除状态。 */ [this](const QJsonObject &) {
            if (!_friend_info) return;
            const auto current = UserMgr::instance()->friendById(_friend_info->_uid);
            if (current) setInfo(current);
            else findChild<QPushButton*>("deleteFriendButton")->setEnabled(false);
        });
    ui->info_voice_label->hide();
    ui->info_video_label->hide();
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

/** @brief 保存好友信息并刷新详情页面。 */
void FriendInfoPage::setInfo(std::shared_ptr<UserInfo> friend_info)
{
    _friend_info = friend_info;
    findChild<QPushButton*>("deleteFriendButton")->setEnabled(TcpMgr::instance()->supportsSocial() && UserMgr::instance()->isFriend(friend_info->_uid));

    ui->info_name_label->setText(_friend_info->_name);

    // 设置头像

    UserMgr::instance()->bindAvatar(ui->info_icon_label, _friend_info->_uid, _friend_info->_icon);
    ui->info_icon_label->setScaledContents(true);
}

void FriendInfoPage::on_info_chat_label_clicked()
{
    SPDLOG_DEBUG("chat action selected from friend information page");
    emit chatRequested(_friend_info);
}
