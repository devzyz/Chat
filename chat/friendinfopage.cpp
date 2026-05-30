#include "friendinfopage.h"
#include "ui_friendinfopage.h"

FriendInfoPage::FriendInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FriendInfoPage)
{
    ui->setupUi(this);
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

void FriendInfoPage::SetInfo(std::shared_ptr<FriendInfo> friend_info)
{
    _friend_info = friend_info;

    ui->info_name_label->setText(_friend_info->_name);

    // 设置头像
    QPixmap pixmap(_friend_info->_icon);
    ui->info_icon_label->setPixmap(pixmap.scaled(ui->info_icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->info_icon_label->setScaledContents(true);
}

void FriendInfoPage::on_info_chat_label_clicked()
{
    qDebug() << "on info chat label clicked";
    emit sig_jump_chat_item(_friend_info);
}
