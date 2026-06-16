#include "editavatardialog.h"
#include "ui_editavatardialog.h"

EditAvatarDialog::EditAvatarDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EditAvatarDialog)
{
    ui->setupUi(this);

    // 设置按钮的属性
    ui->edit_avatar_cancel_btn->SetState("leave", "hover", "select");
    ui->edit_avatar_confirm_btn->SetState("leave", "hover", "select");

    // 设置头像
    QPixmap pixmap(":/res/chat.ico");
    ui->edit_avatar_soft_icon_label->setPixmap(pixmap.scaled(ui->edit_avatar_upload_icon_label->size(),
                                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->edit_avatar_soft_icon_label->setScaledContents(true);

    ui->edit_avatar_dialog_name_label->setText("头像编辑弹框");
}

EditAvatarDialog::~EditAvatarDialog()
{
    delete ui;
}
