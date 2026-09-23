#include "usermgr.h"
#include "findsuccessdialog.h"
#include "logmgr.h"
#include "ui_findsuccessdialog.h"
#include <QDir>
#include "applyfrienddialog.h"

FindSuccessDialog::FindSuccessDialog(QWidget *parent)
    : QDialog(parent), _parent(parent)
    , ui(new Ui::FindSuccessDialog)
{
    ui->setupUi(this);

    // 设置对话框标题
    setWindowTitle("添加");
    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    ui->add_friend_btn->setState("normal", "hover","press");
    this->setModal(true); // 将主窗口等禁用，直到关闭当前窗口
}

FindSuccessDialog::~FindSuccessDialog()
{
    delete ui;
    SPDLOG_DEBUG("FindSuccessDialog destructed");
}

/**
 * @brief FindSuccessDialog::setSearchInfo
 * @param si
 * 搜索到的用户信息
 */
void FindSuccessDialog::setSearchInfo(std::shared_ptr<SearchInfo> si)
{
    ui->name_label->setText(si->_name);
    _si = si;
    UserMgr::instance()->bindAvatar(ui->head_label, si->_uid, si->_icon);
}

/**
 * @brief FindSuccessDialog::on_add_friend_btn_clicked
 * 点击添加到通讯录后，弹出添加好友申请页面
 */
void FindSuccessDialog::on_add_friend_btn_clicked()
{
    // 添加好友界面弹出 todo...
    this->hide();
    // 弹出添加好友界面
    auto applyFriend = new ApplyFriendDialog(_parent);
    applyFriend->setSearchInfo(_si);
    applyFriend->setModal(true);
    applyFriend->show();
}
