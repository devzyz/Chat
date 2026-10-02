#include "authfrienddialog.h"
#include "logmgr.h"
#include "ui_authfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"
#include "clientrequests.h"
#include "socialui.h"
#include <QPushButton>
#include <QLabel>

AuthFriendDialog::AuthFriendDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AuthFriendDialog)
{
    ui->setupUi(this);
    auto *status = new QLabel(this); status->setObjectName("socialStatus"); status->setWordWrap(true); layout()->addWidget(status);
    auto *reject = new QPushButton(tr("拒绝申请"),this); reject->setObjectName("rejectApplicationButton"); layout()->addWidget(reject);
    connect(reject,&QPushButton::clicked,this,/** @brief 拒绝与取消表单是独立操作。 */ [this] { manageApplication("reject"); });
    ui->send_auth_user_description_edit->setMaxLength(255); ui->send_auth_user_back_edit->setMaxLength(255);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setModal(true);

    // 设置编辑框默认文本
    ui->send_auth_user_description_edit->setPlaceholderText(tr("username")); // 我的名字
    ui->send_auth_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::authApplyCancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &AuthFriendDialog::authApplySure);
}

AuthFriendDialog::~AuthFriendDialog()
{
    delete ui;
    SPDLOG_DEBUG("AuthFriendDialog destructed");
}

void AuthFriendDialog::setApplyInfo(std::shared_ptr<ApplyInfo> applyinfo)
{
    _apply_info = applyinfo;
    _revision = UserMgr::instance()->applicationRevision(applyinfo->_apply_uid);
    const bool enabled = TcpMgr::instance()->supportsSocial() && !_revision.isEmpty();
    ui->sure_btn->setEnabled(enabled); findChild<QPushButton*>("rejectApplicationButton")->setEnabled(enabled);
    if (!enabled) findChild<QLabel*>("socialStatus")->setText(tr("请等待最新申请目录；旧服务器需要更新"));
}

void AuthFriendDialog::authApplyCancel()
{
    this->hide();
    this->deleteLater();
}

/**
 * @brief AuthFriendDialog::authApplySure
 * 同意添加好友，发送tcp请求进行认证
 */
void AuthFriendDialog::authApplySure()
{
    manageApplication("accept");
}

void AuthFriendDialog::manageApplication(const QString &operation)
{
    if (_busy || !_apply_info || _revision.isEmpty()) return;
    _busy = true; ui->sure_btn->setEnabled(false);
    for (auto *edit : {ui->send_auth_user_description_edit,ui->send_auth_user_back_edit}) edit->setEnabled(false); findChild<QPushButton*>("rejectApplicationButton")->setEnabled(false);
    TcpMgr::instance()->socialRequest(ID_FRIEND_MANAGE_REQ, {{"operation",operation},{"target_uid",_apply_info->_apply_uid},
        {"expected_revision",_revision},{"description",ui->send_auth_user_description_edit->text()},
        {"backname",ui->send_auth_user_back_edit->text()}}, this,
        /** @brief 失败保留审批内容，成功由后续目录刷新更新列表。 */
        [this](QJsonObject response) {
            _busy = false; ui->sure_btn->setEnabled(true);
            for (auto *edit : {ui->send_auth_user_description_edit,ui->send_auth_user_back_edit}) edit->setEnabled(true); findChild<QPushButton*>("rejectApplicationButton")->setEnabled(true);
            findChild<QLabel*>("socialStatus")->setText(socialResultText(response));
            if (response["error"].toInt(-1) == 0) { accept(); deleteLater(); }
        });
}
