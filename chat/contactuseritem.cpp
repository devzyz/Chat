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
    ui->red_point->raise();
    // 默认不显示红点
    ShowRedPoint(false);
}

ContactUserItem::~ContactUserItem()
{
    delete ui;
}

/**
 * @brief ContactUserItem::sizeHint
 * @return
 * 用于设置外部的QListWidgetIem
 */
QSize ContactUserItem::sizeHint() const
{
    return QSize(250, 70);
}

// 设置当前widget的一些信息
void ContactUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
{
    _friend_info = std::make_shared<FriendInfo> (auth_info);

    // 加载图片
    QPixmap pixmap(_friend_info->_icon);

    // 设置图片自动缩放
    ui->contact_user_head_label->setPixmap(pixmap.scaled(ui->contact_user_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->contact_user_head_label->setScaledContents(true);

    ui->contact_user_name_label->setText(_friend_info->_name);
}

/**
 * @brief ContactUserItem::SetInfo
 * @param uid
 * @param name
 * @param icon
 * 这个专门为新的朋友item开放的接口
 */
void ContactUserItem::SetInfo(int uid, QString name, QString icon)
{
    _friend_info = std::make_shared<FriendInfo> (uid, name, icon);

    QPixmap pixmap(_friend_info->_icon);

    // 设置图片自动缩放
    ui->contact_user_head_label->setPixmap(pixmap.scaled(ui->contact_user_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->contact_user_head_label->setScaledContents(true);

    ui->contact_user_name_label->setText(_friend_info->_name);
}

/**
 * @brief ContactUserItem::SetInfo
 * @param friend_info
 * 设置当前的用户列表的信息，并且每个contactUserItem内部保存一个FriendInfo用于管理当前item
 */
void ContactUserItem::SetInfo(std::shared_ptr<FriendInfo> friend_info)
{
    _friend_info = friend_info;

    QPixmap pixmap(_friend_info->_icon);

    // 设置图片自动缩放
    ui->contact_user_head_label->setPixmap(pixmap.scaled(ui->contact_user_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->contact_user_head_label->setScaledContents(true);

    ui->contact_user_name_label->setText(_friend_info->_name);
}

/**
 * @brief ContactUserItem::ShowRedPoint
 * @param show
 * 根据show是否显示红点
 */
void ContactUserItem::ShowRedPoint(bool show)
{
    if (show){
        ui->red_point->show();
    }else {
        ui->red_point->hide();
    }
}

std::shared_ptr<FriendInfo> ContactUserItem::GetFriendInfo()
{
    return _friend_info;
}


