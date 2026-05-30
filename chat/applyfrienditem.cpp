#include "applyfrienditem.h"
#include "ui_applyfrienditem.h"

ApplyFriendItem::ApplyFriendItem(QWidget *parent)
    : ListItemBase(parent)
    , ui(new Ui::ApplyFriendItem)
{
    ui->setupUi(this);
    // 设置类型
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    // 添加好友的按钮
    ui->apply_friend_add_friend_btn->SetState("normal", "hover", "press");
    ui->apply_friend_add_friend_btn->hide();

    // 当点击添加后，发送认证成功信号
    connect(ui->apply_friend_add_friend_btn, &ClickedBtn::clicked, [this]() {
        emit this->sig_auth_friend(_apply_info);
    });
}

/**
 * @brief ApplyFriendItem::SetInfo
 * @param apply_info
 * 设置item的属性
 */
void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> apply_info) {
    _apply_info = apply_info;
    // 加载图片
    QPixmap pixmap(_apply_info->_icon);

    // 设置图片大小以及自动缩放
    ui->apply_friend_head_label->setPixmap(pixmap.scaled(ui->apply_friend_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->apply_friend_head_label->setScaledContents(true);

    ui->apply_friend_user_name_label->setText(_apply_info->_name);
    ui->apply_friend_user_chat_label->setText(_apply_info->_description);

    // 根据是否已添加来判断是否显示已添加按钮
    ShowAddBtn(!_apply_info->_status);
}

/**
 * @brief ApplyFriendItem::ShowAddBtn
 * @param bshow
 * 是否显示添加按钮
 * 当点击添加按钮后，将其隐藏，将已添加的label显示
 */
void ApplyFriendItem::ShowAddBtn(bool bshow)
{
    if (bshow) {
        ui->apply_friend_add_friend_btn->show();
        ui->apply_friend_already_add_label->hide();
        _added = false;
    }else {
        ui->apply_friend_add_friend_btn->hide();
        ui->apply_friend_already_add_label->show();
        _added = true;
    }
}

QSize ApplyFriendItem::sizeHint() const
{
    return QSize(250, 80);
}

int ApplyFriendItem::GetUid()
{
    return _apply_info->_uid;
}

ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}
