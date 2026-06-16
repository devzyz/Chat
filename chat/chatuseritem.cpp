#include "chatuseritem.h"
#include "ui_chatuseritem.h"
#include "usermgr.h"

ChatUserItem::ChatUserItem(QWidget *parent)
    : ListItemBase(parent), _new_msg_count(0)
    , ui(new Ui::ChatUserItem)
{
    ui->setupUi(this);
    // 设置当前ListItemType的类型
    SetItemType(ListItemType::CHAT_USER_ITEM);
    ui->new_msg_count_label->setAlignment(Qt::AlignCenter); // 设置文字居中

    // 展示新消息提醒
    ShowNewMsgTip();
}

ChatUserItem::~ChatUserItem()
{
    delete ui;
}

// 设置ChatInfo
void ChatUserItem::SetChatInfo(std::shared_ptr<ChatInfo> chat_info)
{
    _chat_info = chat_info;

    if (chat_info->GetChatType() == ChatType::PRIVATE) {
        // 获取到当前用户的信息
        auto info = UserMgr::GetInstance()->GetFriendById(chat_info->GetUid());

        // 加载head路径下的头像图片
        QPixmap pixmap(info->_icon);

        // 将图片缩放为icon_label的大小，并显示
        ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->icon_label->setScaledContents(true);
        // 更新用户名和上次聊天记录
        ui->user_name_label->setText(info->_name);

        auto last_msg_id = chat_info->GetLastMsgId();

        // 最后一条数据
        auto last_msg = chat_info->GetChatDataByMsgId(last_msg_id);
        if (last_msg != nullptr) {
            // 设置最后一条数据的发送时间和发送内容
            ui->time_label->setText(last_msg->GetSendTime().toString("HH:mm"));
            ui->user_chat_label->setText(last_msg->GetContent());
        }else {
            ui->user_chat_label->setText("");
        }
    }else if (chat_info->GetChatType() == ChatType::GROUP) {
        // todo...
    }
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
std::shared_ptr<ChatInfo> ChatUserItem::GetChatInfo()
{
    return _chat_info;
}

// 设置最后一次聊天记录
void ChatUserItem::SetLastTextChatMsg(QString last_text_msg)
{
    ui->user_chat_label->setText(last_text_msg);
}

// 根据_new_msg_count来判断是否显示新消息提醒
void ChatUserItem::ShowNewMsgTip()
{
    ui->new_msg_count_label->setText(QString::number(_new_msg_count));
    if (_new_msg_count > 0) {
        ui->new_msg_count_label->show();
    }else {
        ui->new_msg_count_label->hide();
    }
}

// 更新新消息数量
void ChatUserItem::UpdateNewMsgCount(int count)
{
    _new_msg_count += count;
    ShowNewMsgTip();
}

// 重置新消息
void ChatUserItem::ResetNewMsgCount() {
    _new_msg_count = 0;
    ShowNewMsgTip();
}
