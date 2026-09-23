#ifndef ADDUSERITEM_H
#define ADDUSERITEM_H

#include <QWidget>
#include "listitembase.h"

namespace Ui {
class AddUserItem;
}

/**
 * @brief The AddUserItem class
 * 添加新用户的Item
 */
class AddUserItem : public ListItemBase
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于展示搜索列表中的添加用户入口。 */
    explicit AddUserItem(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~AddUserItem();
    /** @brief 返回当前条目建议尺寸，供列表布局使用。 */
    QSize sizeHint();

private:
    Ui::AddUserItem *ui;
};

#endif // ADDUSERITEM_H
