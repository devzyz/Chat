#include "applyfrienddialog.h"
#include "ui_applyfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"

ApplyFriendDialog::ApplyFriendDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ApplyFriendDialog)
{
    ui->setupUi(this);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setModal(true);

    // 设置编辑框默认文本
    ui->send_apply_user_name_edit->setPlaceholderText(tr("username")); // 我的名字
    ui->send_apply_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::slot_send_apply_cancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::slot_send_apply_sure);
}

ApplyFriendDialog::~ApplyFriendDialog()
{
    delete ui;
    qDebug() << "ApplyFriend is destructed";
}

void ApplyFriendDialog::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    _si = si;
}

/**
 * @brief ApplyFriendDialog::slot_send_apply_sure
 * 当在申请添加好友弹框出点击添加好友后触发的槽函数
 * 发送tcp添加好友请求
 */
void ApplyFriendDialog::slot_send_apply_sure() {
    qDebug() << "slot send apply sure";
    // 设置发送请求的Json参数
    QJsonObject jsonObj;
    auto uid = UserMgr::GetInstance()->GetUid();
    jsonObj["uid"] = uid;
    // 设置申请人的名字
    auto applyname = ui->send_apply_user_name_edit->text();
    // 如果为空，则用默认的名字
    if (applyname.isEmpty()) {
        applyname = ui->send_apply_user_name_edit->placeholderText();
    }
    jsonObj["applyname"] = applyname;

    // 设置备注名
    auto backname = ui->send_apply_user_back_edit->text();
    if (backname.isEmpty()) {
        backname = ui->send_apply_user_back_edit->placeholderText();
    }
    jsonObj["backname"] = backname;

    // 标识要添加的uid是什么，因为我们在搜索的时候已经查出来了，并保存了
    jsonObj["touid"] = _si->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    qDebug() << "apply friend tcp request already";

    // 发送tcp请求
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_ADD_FRIEND_REQ, jsonData);

    this->hide();
    deleteLater();
}

void ApplyFriendDialog::slot_send_apply_cancel() {
    this->hide();
    deleteLater();
}
