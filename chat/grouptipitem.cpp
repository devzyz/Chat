#include "grouptipitem.h"
#include "ui_grouptipitem.h"

GroupTipItem::GroupTipItem(QWidget *parent)
    : ListItemBase(parent), _tip("")
    , ui(new Ui::GroupTipItem)
{
    ui->setupUi(this);
    // 设置当前type的类型
    SetItemType(ListItemType::GROUP_TIP_ITEM);
}

GroupTipItem::~GroupTipItem()
{
    delete ui;
}

QSize GroupTipItem::sizeHint() const
{
    return QSize(250, 25);
}

void GroupTipItem::SetGroupTip(QString str)
{
    ui->group_tip_label->setText(str);
}


