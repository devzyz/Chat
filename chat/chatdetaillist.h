#ifndef CHATDETAILLIST_H
#define CHATDETAILLIST_H

#include <QListWidget>

class ChatDetailList : public QListWidget
{
    Q_OBJECT
public:
    ChatDetailList(QWidget *parent = nullptr);

    // 在末尾添加一个新的聊天对话
    void appendChatItem(QWidget *item);
    // 删除所有的item
    void removeAllItem();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // CHATDETAILLIST_H
