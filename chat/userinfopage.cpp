#include "userinfopage.h"
#include "ui_userinfopage.h"

UserInfoPage::UserInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::UserInfoPage)
{
    ui->setupUi(this);
}

UserInfoPage::~UserInfoPage()
{
    delete ui;
}

// 点击上传按钮
void UserInfoPage::on_user_info_page_upload_btn_clicked()
{

}

