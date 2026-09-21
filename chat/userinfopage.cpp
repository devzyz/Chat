#include "userinfopage.h"
#include "ui_userinfopage.h"
#include "usermgr.h"

UserInfoPage::UserInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::UserInfoPage)
{
    ui->setupUi(this);
    ui->user_info_page_upload_btn->setText(tr("上传头像"));
    ui->user_info_page_upload_btn->setToolTip(tr("上传成功后同步到其他设备"));
    const auto updateAvatar = [this]() {
        ui->user_info_page_head_label->setPixmap(UserMgr::GetInstance()->selfAvatar().scaled(
            ui->user_info_page_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    updateAvatar();
    connect(UserMgr::GetInstance()->localAvatar(), &LocalAvatar::imageChanged, this, updateAvatar);
}

UserInfoPage::~UserInfoPage()
{
    delete ui;
}

// 点击上传按钮
void UserInfoPage::on_user_info_page_upload_btn_clicked()
{
    if (!_edit_avatar_dialog) {
        _edit_avatar_dialog = new EditAvatarDialog(UserMgr::GetInstance()->localAvatar(),
                                                   UserMgr::GetInstance()->selfAvatar(), this);
        _edit_avatar_dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    _edit_avatar_dialog->open();
}
