#include "usermgr.h"
#include "friendinfopage.h"
#include "logmgr.h"
#include "ui_friendinfopage.h"

FriendInfoPage::FriendInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FriendInfoPage)
{
    ui->setupUi(this);
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

/** @brief 保存好友信息并刷新详情页面。 */
void FriendInfoPage::setInfo(std::shared_ptr<UserInfo> friend_info)
{
    _friend_info = friend_info;

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
