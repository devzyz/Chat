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
    explicit AddUserItem(QWidget *parent = nullptr);
    ~AddUserItem();
    QSize sizeHint();

private:
    Ui::AddUserItem *ui;
};

#endif // ADDUSERITEM_H
