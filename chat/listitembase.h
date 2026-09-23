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
    /** @brief 初始化对象，用于为列表展示组件保存条目类别并绘制样式背景。 */
    explicit ListItemBase(QWidget* parent = nullptr);
    /** @brief 保存列表条目的业务类别，用于点击路由。 */
    void SetItemType(ListItemType itemType);
    /** @brief 返回当前列表条目类别。 */
    ListItemType GetItemType();

protected:
    // 让 ChatPage 能完整显示样式表定义的背景颜色、背景图片、边框
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
    virtual void paintEvent(QPaintEvent *event) override;

private:
    ListItemType _itemType;
};

#endif // LISTITEMBASE_H
