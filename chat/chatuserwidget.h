#ifndef CHATUSERWIDGET_H
#define CHATUSERWIDGET_H

#include <QWidget>
#include "listitembase.h"

namespace Ui {
class ChatUserWidget;
}

/**
 * @brief The ChatUserWidget class
 * 聊天对话列表显示的模板
 */
class ChatUserWidget : public ListItemBase
{
    Q_OBJECT

public:
    explicit ChatUserWidget(QWidget *parent = nullptr);
    ~ChatUserWidget();
    void SetInfo(QString name, QString head, QString msg);
    // 目的是为了设置外面的QListItem，因为QListWidget内部只能放这个类，所以要先放这个类，再在这个类内部放自己自定义的
    QSize sizeHint() const override;

private:
    Ui::ChatUserWidget *ui;
    QString _name;
    QString _head;
    QString _message;
};

#endif // CHATUSERWIDGET_H
