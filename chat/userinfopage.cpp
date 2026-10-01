#include "userinfopage.h"
#include "ui_userinfopage.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "socialui.h"
#include <QLabel>

UserInfoPage::UserInfoPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::UserInfoPage)
{
    ui->setupUi(this);
    auto *status = new QLabel(this); status->setObjectName("profileStatus"); status->setWordWrap(true); ui->verticalLayout->addWidget(status);
    ui->user_info_page_name_edit->setMaxLength(255); ui->user_info_page_description_edit->setMaxLength(255);
    const auto refresh = /** @brief 仅在表单未编辑时接收服务端资料，保留用户未提交输入。 */ [this] {
        if (_profileBusy || ui->user_info_page_name_edit->isModified() || ui->user_info_page_description_edit->isModified()) return;
        const auto profile = UserMgr::instance()->userInfo(); if (!profile) return;
        ui->user_info_page_name_edit->setText(profile->_name); ui->user_info_page_description_edit->setText(profile->_description);
        _profileRevision = UserMgr::instance()->socialProfile(profile->_uid)["profile_revision"].toString();
        ui->user_info_page_submit_btn->setEnabled(TcpMgr::instance()->supportsSocial() && !_profileRevision.isEmpty());
    };
    auto *reload = new QPushButton(tr("重新载入资料"),this); reload->setObjectName("reloadProfileButton"); ui->verticalLayout->addWidget(reload);
    connect(reload,&QPushButton::clicked,this,/** @brief 用户显式重新载入最新资料，用于处理冲突或不确定结果。 */ [this,status] {
        if (_profileBusy) return;
        setProfileBusy(true);
        TcpMgr::instance()->socialRequest(ID_SOCIAL_DIRECTORY_REQ, {{"kind","profile"}}, this,
            /** @brief 读取成功才替换表单内容和编辑版本。 */ [this,status](QJsonObject response) {
                setProfileBusy(false);
                if (response["error"].toInt(-1) != 0) { status->setText(socialResultText(response)); return; }
                const auto row = response["profile"].toObject(); _profileRevision = row["profile_revision"].toString();
                ui->user_info_page_name_edit->setText(row["name"].toString());
                ui->user_info_page_description_edit->setText(row["description"].toString());
                ui->user_info_page_submit_btn->setEnabled(true); status->clear();
            });
    });
    refresh(); connect(UserMgr::instance().get(),&UserMgr::profileChanged,this,refresh);
    if (!TcpMgr::instance()->supportsSocial()) status->setText(tr("服务器需要更新后才能编辑资料"));
    connect(ui->user_info_page_submit_btn,&QPushButton::clicked,this,/** @brief 提交本人资料时固定原版本并保留失败内容。 */ [this,status] {
        if (_profileBusy) return;
        setProfileBusy(true);
        TcpMgr::instance()->socialRequest(ID_PROFILE_UPDATE_REQ, {{"name",ui->user_info_page_name_edit->text()},
            {"description",ui->user_info_page_description_edit->text()},{"expected_revision",_profileRevision}}, this,
            /** @brief 成功后解除编辑标记，失败保持输入供用户处理。 */ [this,status](QJsonObject response) {
                status->setText(socialResultText(response)); setProfileBusy(false);
                if (response["error"].toInt(-1) == 0) {
                    ui->user_info_page_name_edit->setModified(false); ui->user_info_page_description_edit->setModified(false);
                    _profileRevision = response["profile"].toObject()["profile_revision"].toString();
                }
            });
    });
    auto *logout = new QPushButton(tr("退出登录"), this);
    auto *switchAccount = new QPushButton(tr("切换账号"), this);
    logout->setObjectName("logoutButton"); switchAccount->setObjectName("switchAccountButton");
    ui->verticalLayout->addWidget(logout); ui->verticalLayout->addWidget(switchAccount);
    connect(logout, &QPushButton::clicked, this, /** @brief 转交主动退出意图。 */
        [this] { emit logoutRequested(false); });
    connect(switchAccount, &QPushButton::clicked, this, /** @brief 转交换账号意图。 */
        [this] { emit logoutRequested(true); });
    ui->user_info_page_upload_btn->setText(tr("上传头像"));
    ui->user_info_page_upload_btn->setToolTip(tr("上传成功后同步到其他设备"));
    const auto updateAvatar =
        /** @brief 头像变化后刷新本人资料页的头像显示。 */
        [this]() {
        ui->user_info_page_head_label->setPixmap(UserMgr::instance()->selfAvatar().scaled(
            ui->user_info_page_head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    updateAvatar();
    connect(UserMgr::instance()->localAvatar(), &LocalAvatar::imageChanged, this, updateAvatar);
}

UserInfoPage::~UserInfoPage()
{
    delete ui;
}

// 点击上传按钮
void UserInfoPage::on_user_info_page_upload_btn_clicked()
{
    if (!_edit_avatar_dialog) {
        _edit_avatar_dialog = new EditAvatarDialog(UserMgr::instance()->localAvatar(),
                                                   UserMgr::instance()->selfAvatar(), this);
        _edit_avatar_dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    _edit_avatar_dialog->open();
}

void UserInfoPage::setProfileBusy(bool busy)
{
    _profileBusy = busy;
    ui->user_info_page_name_edit->setEnabled(!busy);
    ui->user_info_page_description_edit->setEnabled(!busy);
    ui->user_info_page_submit_btn->setEnabled(!busy && TcpMgr::instance()->supportsSocial() && !_profileRevision.isEmpty());
    findChild<QPushButton*>("reloadProfileButton")->setEnabled(!busy && TcpMgr::instance()->supportsSocial());
}
