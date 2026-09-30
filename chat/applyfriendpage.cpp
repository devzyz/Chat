#include "applyfriendpage.h"
#include "ui_applyfriendpage.h"
#include "applyfriendlist.h"
#include "tcpmgr.h"
#include <QPainter>
#include <QStyleOption>
#include "usermgr.h"
#include "messageservice.h"
#include "authfrienddialog.h"

ApplyFriendPage::ApplyFriendPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ApplyFriendPage)
{
    ui->setupUi(this);

    // connect(ui->apply_friend_list, &ApplyFriendList::searchRequested, this, &ApplyFriendPage::searchRequested);

    loadApplyList();

    auto *messages = UserMgr::instance()->messages();
    connect(messages, &MessageService::directoryRestored, this,
        /** @brief 初始本地目录恢复后展示申请。 */
        [this](const QJsonObject &) { loadApplyList(); });
    connect(messages, &MessageService::directoryChanged, this,
        /** @brief 仅在本地申请记录提交后刷新申请页。 */
        [this](const QJsonObject &directory) {
            if (!directory["applications"].toArray().isEmpty()) loadApplyList();
        });
}

ApplyFriendPage::~ApplyFriendPage()
{
    delete ui;
}

// 添加一条申请信息
/** @brief 添加好友申请展示项并更新申请列表。 */
void ApplyFriendPage::addNewApply(std::shared_ptr<ApplyInfo> applyInfo)
{
    if (_apply_items_map.contains(applyInfo->_apply_uid)) {
        _apply_items_map.value(applyInfo->_apply_uid)->setInfo(applyInfo);
        return;
    }
    auto * apply_item = new ApplyFriendItem();
    apply_item->setInfo(applyInfo);

    QListWidgetItem * item = new QListWidgetItem();
    item->setSizeHint(apply_item->sizeHint());
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    ui->apply_friend_list->insertItem(0, item);
    ui->apply_friend_list->setItemWidget(item, apply_item);
    apply_item->showAddBtn(!applyInfo->_status);

    // 将item添加上apply_list上去
    _apply_items_map.insert(applyInfo->_apply_uid, apply_item);

    // 收到审核好友信号
    connect(apply_item, &ApplyFriendItem::friendApprovalRequested, this,
        /** @brief 为选中申请创建有父对象的审批对话框。 */
        [this](std::shared_ptr<ApplyInfo> apply_info) {
        auto *authFriendDialog =  new AuthFriendDialog(this);
        authFriendDialog->setModal(true);
        authFriendDialog->setApplyInfo(apply_info);
        authFriendDialog->show();
    });
}

/**
 * @brief ApplyFriendPage::paintEvent
 * @param event
 * 重写喷绘
 */
void ApplyFriendPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

/**
 * @brief ApplyFriendPage::loadApplyList
 * 加载初始好友申请信息
 */
void ApplyFriendPage::loadApplyList()
{
    std::vector<std::shared_ptr<ApplyInfo>> apply_list;
    UserMgr::instance()->appendFriendApplicationsTo(apply_list);

    for (const auto &application : apply_list) addNewApply(application);
}

// 当本客户端同意认证后，在服务器回包后，将添加按钮，变为已添加
/** @brief 按认证结果更新对应的申请项。 */
void ApplyFriendPage::authFinish(std::shared_ptr<AuthInfo> auth_info)
{
    auto uid = auth_info->_auth_uid;
    auto find_iter = _apply_items_map.find(uid);
    if (find_iter == _apply_items_map.end()) {
        return ;
    }
    (*find_iter)->showAddBtn(false);
}
