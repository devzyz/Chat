#include "applyfrienddialog.h"
#include "logmgr.h"
#include "ui_applyfrienddialog.h"
#include <QJsonObject>
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"
#include "clientrequests.h"
#include "socialui.h"
#include <QLabel>

ApplyFriendDialog::ApplyFriendDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ApplyFriendDialog)
{
    ui->setupUi(this);
    auto *status = new QLabel(this); status->setObjectName("socialStatus"); status->setWordWrap(true); layout()->addWidget(status);
    ui->send_apply_user_description_edit->setMaxLength(255); ui->send_apply_user_back_edit->setMaxLength(255);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setModal(true);

    // 设置编辑框默认文本
    ui->send_apply_user_description_edit->setPlaceholderText(tr("description")); // 我发送的申请描述信息
    ui->send_apply_user_back_edit->setPlaceholderText("backanme"); // 给对方的备注名

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::sendApplyCancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &ApplyFriendDialog::sendApplySure);
}

ApplyFriendDialog::~ApplyFriendDialog()
{
    delete ui;
    SPDLOG_DEBUG("ApplyFriendDialog destructed");
}

void ApplyFriendDialog::setSearchInfo(std::shared_ptr<SearchInfo> si)
{
    _si = si; _revision.clear(); ui->sure_btn->setEnabled(false);
    TcpMgr::instance()->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"},{"target_uid",si->_uid}}, this,
        /** @brief 保存打开表单时的申请版本，禁止迟到操作覆盖新申请。 */
        [this](QJsonObject response) {
            if (response["error"].toInt(-1) != 0) { findChild<QLabel*>("socialStatus")->setText(socialResultText(response)); return; }
            const auto profile = response["profile"].toObject();
            _revision = profile["outgoing_revision"].toString();
            const bool pending = profile["outgoing_status"].toInt() == 0;
            ui->sure_btn->setEnabled(!pending && !profile["relationship_active"].toBool());
            if (pending) findChild<QLabel*>("socialStatus")->setText(tr("申请已发送，等待对方处理"));
        });
}

/**
 * @brief ApplyFriendDialog::sendApplySure
 * 当在申请添加好友弹框出点击添加好友后触发的槽函数
 * 发送tcp添加好友请求
 */
void ApplyFriendDialog::sendApplySure() {
    if (_busy || !_si || _revision.isEmpty()) return;
    _busy = true; ui->sure_btn->setEnabled(false);
    for (auto *edit : {ui->send_apply_user_description_edit,ui->send_apply_user_back_edit}) edit->setEnabled(false);
    const QJsonObject request{{"operation","apply"},{"target_uid",_si->_uid},{"expected_revision",_revision},
        {"description",ui->send_apply_user_description_edit->text()}, {"backname",ui->send_apply_user_back_edit->text()}};
    TcpMgr::instance()->socialRequest(ID_FRIEND_MANAGE_REQ, request, this,
        /** @brief 成功后关闭表单，失败保留输入并明确显示结果。 */
        [this](QJsonObject response) {
            _busy = false; ui->sure_btn->setEnabled(true);
            for (auto *edit : {ui->send_apply_user_description_edit,ui->send_apply_user_back_edit}) edit->setEnabled(true);
            findChild<QLabel*>("socialStatus")->setText(socialResultText(response));
            if (response["error"].toInt(-1) == 0) { accept(); deleteLater(); }
        });
}

void ApplyFriendDialog::sendApplyCancel() {
    this->hide();
    deleteLater();
}
