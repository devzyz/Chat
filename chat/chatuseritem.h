#ifndef CHATUSERITEM_H
#define CHATUSERITEM_H

#include <QWidget>
#include "listitembase.h"
#include "userdata.h"
#include <memory>

namespace Ui {
class ChatUserItem;
}

/**
 * @brief The ChatUserWidget class
 * 聊天对话列表显示的模板
 */
class ChatUserItem : public ListItemBase
{
    Q_OBJECT

public:
    explicit ChatUserItem(QWidget *parent = nullptr);
    ~ChatUserItem();
    void SetInfo(std::shared_ptr<FriendInfo>);
    // 当点击认证后，添加条目
    void SetInfo(std::shared_ptr<AuthInfo>);
    // 目的是为了设置外面的QListItem，因为QListWidget内部只能放这个类，所以要先放这个类，再在这个类内部放自己自定义的
    QSize sizeHint() const override;

    std::shared_ptr<FriendInfo> GetChatInfo();
    // 设置上一次聊天数据
    void SetLastTextChatMsg(QString& last_text_msg);
    // 通过判断_new_msg_count来决定是否显示新消息提醒
    void ShowNewMsgTip();
    // 更新_new_msg_count的数量
    void UpdateNewMsgCount(int);
    // 重置新消息数量
    void ResetNewMsgCount();
private:
    Ui::ChatUserItem *ui;
    std::shared_ptr<FriendInfo> _chat_info;
    int _new_msg_count;
};

#endif // CHATUSERITEM_H
