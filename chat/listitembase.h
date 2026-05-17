#ifndef LISTITEMBASE_H
#define LISTITEMBASE_H

#include <QWidget>
#include "global.h"

/**
 * @brief The ListItemBase class
 *
 * 主要目的是为了方便qss样式渲染
 * 只要子类继承该类，子类的qss能够正确刷新
 *
 * 所有的item的基类，有几个下属
 * ChatUserWidget 聊天显示列表的item
 *
 */
class ListItemBase : public QWidget
{
    Q_OBJECT
public:
    explicit ListItemBase(QWidget* parent = nullptr);
    void SetItemType(ListItemType itemType);
    ListItemType GetItemType();

protected:
    // 让 ChatPage 能完整显示样式表定义的背景颜色、背景图片、边框
    virtual void paintEvent(QPaintEvent *event) override;

private:
    ListItemType _itemType;
};

#endif // LISTITEMBASE_H
