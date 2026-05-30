#include "findsuccessdialog.h"
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

    // 获取当前应用程序的路径
    QString app_path = QCoreApplication::applicationDirPath();
    // 头像需要去请求服务器，然后将头像资源下载下来，保存到static下
    QString pix_path = QDir::toNativeSeparators(app_path + QDir::separator() +
                                                "static" + QDir::separator() + "head_5.jpg");

    // 将拿到的头像添加到搜索弹框上
    QPixmap head_pix(pix_path);
    // 缩放
    head_pix = head_pix.scaled(ui->head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->head_label->setPixmap(head_pix);
    ui->add_friend_btn->SetState("normal", "hover","press");
    this->setModal(true); // 将主窗口等禁用，直到关闭当前窗口
}

FindSuccessDialog::~FindSuccessDialog()
{
    delete ui;
    qDebug() << "FindSuccessDialog is destructed";
}

/**
 * @brief FindSuccessDialog::SetSearchInfo
 * @param si
 * 搜索到的用户信息
 */
void FindSuccessDialog::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    ui->name_label->setText(si->_name);
    _si = si;
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
    applyFriend->SetSearchInfo(_si);
    applyFriend->setModal(true);
    applyFriend->show();
}

