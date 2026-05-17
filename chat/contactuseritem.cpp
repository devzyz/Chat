#include "contactuseritem.h"
#include "ui_contactuseritem.h"

ContactUserItem::ContactUserItem(QWidget *parent)
    : ListItemBase(parent)
    , ui(new Ui::ContactUserItem)
{
    ui->setupUi(this);
    // 设置当前item的类型
    SetItemType(ListItemType::CONTACT_USER_ITEM);
    // 红点显示在最上层
    ui->red_point_label->raise();
    ShowRedPoint(true);
}

ContactUserItem::~ContactUserItem()
{
    delete ui;
}

QSize ContactUserItem::sizeHint() const
{
    return QSize(250, 70);
}

// void ContactUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
// {
//     _info = std::make_shared<UserInfo> (auth_info);
//     // 加载图片
//     QPixmap pixmap(_info->_icon);

//     // 设置图片自动缩放
//     ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
//     ui->icon_label->setScaledContents(true);

//     ui->user_name_label->setText(_info->_name);
// }

// void ContactUserItem::SetInfo(std::shared_ptr<AuthRsp> auth_rsp)
// {
//     _info = std::make_shared<UserInfo> (auth_rsp);
//     QPixmap pixmap(_info->_icon);

//     // 设置图片自动缩放
//     ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
//     ui->icon_label->setScaledContents(true);

//     ui->user_name_label->setText(_info->_name);
// }

// 设置UserInfo信息
void ContactUserItem::SetInfo(int uid, QString name, QString icon)
{
    _info = std::make_shared<UserInfo> (uid, name, icon);

    QPixmap pixmap(_info->_icon);

    // 设置图片自动缩放
    ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_label->setScaledContents(true);

    ui->user_name_label->setText(_info->_name);
}

void ContactUserItem::ShowRedPoint(bool show)
{
    if (show){
        ui->red_point_label->show();
    }else {
        ui->red_point_label->hide();
    }
}


