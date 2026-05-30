#include "chatuseritem.h"
#include "ui_chatuseritem.h"

ChatUserItem::ChatUserItem(QWidget *parent)
    : ListItemBase(parent), _new_msg_count(0)
    , ui(new Ui::ChatUserItem)
{
    ui->setupUi(this);
    // 设置当前ListItemType的类型
    SetItemType(ListItemType::CHAT_USER_ITEM);
    ui->new_msg_count_label->setAlignment(Qt::AlignCenter); // 设置文字居中

    ShowNewMsgTip();
}

ChatUserItem::~ChatUserItem()
{
    delete ui;
}

// 已经是好友添加的条目
void ChatUserItem::SetInfo(std::shared_ptr<FriendInfo> friend_info)
{
    _chat_info = friend_info;

    // 加载head路径下的头像图片
    QPixmap pixmap(_chat_info->_icon);

    // 将图片缩放为icon_label的大小，并显示
    ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_label->setScaledContents(true);
    // 更新用户名和上次聊天记录
    ui->user_name_label->setText(_chat_info->_name);
    ui->user_chat_label->setText(_chat_info->_last_msg);
}

// 认证添加的条目
void ChatUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
{
    _chat_info = std::make_shared<FriendInfo> (auth_info);

    // 加载head路径下的头像图片
    QPixmap pixmap(_chat_info->_icon);

    // 将图片缩放为icon_label的大小，并显示
    ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_label->setScaledContents(true);
    // 更新用户名和上次聊天记录
    ui->user_name_label->setText(_chat_info->_name);
    ui->user_chat_label->setText(_chat_info->_last_msg);
}

/**
 * @brief ChatUserItem::sizeHint
 * @return
 * 返回默认尺寸大小
 */
QSize ChatUserItem::sizeHint() const {
    return QSize(250, 70);
}

/**
 * @brief ChatUserItem::GetChatInfo
 * @return
 * 获取当前item对应的ChatInfo信息
 */
std::shared_ptr<FriendInfo> ChatUserItem::GetChatInfo()
{
    return _chat_info;
}

void ChatUserItem::SetLastTextChatMsg(QString &last_text_msg)
{
    ui->user_chat_label->setText(last_text_msg);
}

void ChatUserItem::ShowNewMsgTip()
{
    ui->new_msg_count_label->setText(QString::number(_new_msg_count));
    if (_new_msg_count > 0) {
        ui->new_msg_count_label->show();
    }else {
        ui->new_msg_count_label->hide();
    }
}

void ChatUserItem::UpdateNewMsgCount(int count)
{
    _new_msg_count += count;
    ShowNewMsgTip();
}

void ChatUserItem::ResetNewMsgCount() {
    _new_msg_count = 0;
    ShowNewMsgTip();
}
