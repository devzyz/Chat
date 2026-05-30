#include "adduseritem.h"
#include "ui_adduseritem.h"

AddUserItem::AddUserItem(QWidget *parent)
    : ListItemBase(parent)
    , ui(new Ui::AddUserItem)
{
    ui->setupUi(this);
    // 设置类型
    SetItemType(ListItemType::ADD_USER_TIP_ITEM);
}

AddUserItem::~AddUserItem()
{
    delete ui;
}

QSize AddUserItem::sizeHint()
{
    return QSize(250, 70);
}
