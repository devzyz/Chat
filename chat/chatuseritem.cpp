#include "chatuseritem.h"
#include "ui_chatuseritem.h"
#include "usermgr.h"

ChatUserItem::ChatUserItem(QWidget *parent)
    : ListItemBase(parent), _new_msg_count(0)
    , ui(new Ui::ChatUserItem)
{
    ui->setupUi(this);
    // 设置当前ListItemType的类型
    setItemType(ListItemType::CHAT_USER_ITEM);
    ui->new_msg_count_label->setAlignment(Qt::AlignCenter); // 设置文字居中

    // 展示新消息提醒
    showNewMsgTip();
}

ChatUserItem::~ChatUserItem()
{
    delete ui;
}

// 设置ChatInfo
/** @brief 绑定会话数据并刷新昵称、头像及最近消息。 */
void ChatUserItem::setChatInfo(std::shared_ptr<ChatInfo> chat_info)
{
    _chat_info = chat_info;

    if (chat_info->getChatType() == ChatType::PRIVATE) {
        // 获取到当前用户的信息
        auto info = UserMgr::instance()->friendById(chat_info->getUid());

        // 加载head路径下的头像图片


        // 将图片缩放为icon_label的大小，并显示
        UserMgr::instance()->bindAvatar(ui->icon_label, chat_info->getUid(), info ? info->_icon : QString(":/res/head_1.jpg"));
        ui->icon_label->setScaledContents(true);
        // 更新用户名和上次聊天记录
        ui->user_name_label->setText(info ? (info->_backname.isEmpty() ? info->_name : info->_backname) : QString::number(chat_info->getUid()));

        auto last_msg_id = chat_info->getLastMsgId();

        // 最后一条数据
        auto last_msg = chat_info->getChatDataByMsgId(last_msg_id);
        if (last_msg != nullptr) {
            // 设置最后一条数据的发送时间和发送内容
            ui->time_label->setText(last_msg->getSendTime().toString("HH:mm"));
            ui->user_chat_label->setText(last_msg->getContent());
        }else {
            ui->user_chat_label->setText("");
        }
    }else if (chat_info->getChatType() == ChatType::GROUP) {
        ui->user_name_label->setText(tr("群 · %1").arg(chat_info->name()));
        ui->icon_label->setPixmap(QPixmap(":/res/head_1.jpg"));
        ui->icon_label->setScaledContents(true);
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
 * @brief ChatUserItem::getChatInfo
 * @return
 * 获取当前item对应的ChatInfo信息
 */
std::shared_ptr<ChatInfo> ChatUserItem::getChatInfo()
{
    return _chat_info;
}

// 设置最后一次聊天记录
void ChatUserItem::setLastTextChatMsg(QString last_text_msg)
{
    ui->user_chat_label->setText(last_text_msg);
}

// 根据_new_msg_count来判断是否显示新消息提醒
void ChatUserItem::showNewMsgTip()
{
    ui->new_msg_count_label->setText(QString::number(_new_msg_count));
    if (_new_msg_count > 0) {
        ui->new_msg_count_label->show();
    }else {
        ui->new_msg_count_label->hide();
    }
}

void ChatUserItem::setNewMsgCount(qint64 count)
{
    _new_msg_count = qMax(qint64(0), count);
    showNewMsgTip();
}
