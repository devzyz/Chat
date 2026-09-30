#pragma once

#include "tcpmgr.h"
#include "usermgr.h"
#include "messageservice.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

/** @brief 展示单群资料和有界管理操作；网络与持久化仍由现有管理器负责。 */
class GroupPanel final : public QDialog {
public:
    /** @brief 绑定固定会话，关闭窗口销毁所有请求观察回调。 */
    GroupPanel(int chat, QWidget *parent) : QDialog(parent), _chat(chat) {
        setObjectName(QString("group-details-%1").arg(chat));
        setAttribute(Qt::WA_DeleteOnClose); setWindowTitle(tr("群资料")); resize(450, 520);
        auto *layout = new QVBoxLayout(this);
        _status = new QLabel(this); _status->setWordWrap(true); layout->addWidget(_status);
        _members = new QListWidget(this); layout->addWidget(_members);
        auto *refresh = new QPushButton(tr("刷新资料"), this); layout->addWidget(refresh);
        connect(refresh, &QPushButton::clicked, this, /** @brief 从第一页重新读取一致群资料。 */ [this] { refreshInfo(); });
        auto *actions = new QHBoxLayout; layout->addLayout(actions);
        for (const auto &operation : {QString("add"),QString("remove"),QString("transfer"),QString("rename"),QString("leave"),QString("dissolve")}) {
            const QMap<QString,QString> titles{{"add",tr("添加")},{"remove",tr("移除")},{"transfer",tr("转让")},
                {"rename",tr("改名")},{"leave",tr("退出")},{"dissolve",tr("解散")}};
            auto *button = new QPushButton(titles.value(operation), this); actions->addWidget(button);
            _buttons.insert(operation, button);
            connect(button, &QPushButton::clicked, this, /** @brief 收集操作输入后提交固定身份的请求。 */ [this,operation] { operate(operation); });
        }
        _retry = new QPushButton(tr("重试待确认操作"), this); _retry->setEnabled(false); layout->addWidget(_retry);
        connect(_retry, &QPushButton::clicked, this, /** @brief 使用原 UUID、版本及参数重放请求。 */ [this] { submit(); });
        _timer = new QTimer(this); _timer->setSingleShot(true); _timer->setInterval(10000);
        connect(_timer, &QTimer::timeout, this, /** @brief 超时保留业务身份，不将结果未知显示为失败。 */ [this] {
            _status->setText(tr("结果尚未确认。可重试原操作，或刷新查看权威状态。")); _retry->setEnabled(!_pending.isEmpty());
        });
        connect(TcpMgr::instance().get(), &TcpMgr::groupResponse, this,
            /** @brief 只接受本窗口匹配请求，将分页成员组合成同一版本。 */
            [this](ReqId id, const QJsonObject &response) {
            const auto key = response["request_id"].toString();
            if (id == ID_GROUP_MANAGE_RSP && key == _pending["request_id"].toString() && !key.isEmpty()) {
                _timer->stop();
                if (response["local_save_failed"].toBool()) {
                    _status->setText(tr("操作已完成，本地保存失败。请刷新或重试原操作恢复本地状态。"));
                    _retry->setEnabled(true); return;
                }
                if (response["error"].toInt(-1) != 0) {
                    _status->setText(tr("操作未完成：%1。状态冲突时请刷新后重新选择操作。").arg(response["group_error"].toString()));
                    if (response["group_error"] == "StorageUnavailable") _retry->setEnabled(true);
                    else { clearPending(); _retry->setEnabled(false); updateActions(); }
                    return;
                }
                clearPending(); _retry->setEnabled(false); refreshInfo(); return;
            }
            if (id != ID_GROUP_INFO_RSP || key != _query) return;
            _timer->stop();
            _query.clear();
            if (response["local_save_failed"].toBool()) {
                _status->setText(tr("已获取资料，本地保存失败；当前显示缓存。")); return;
            }
            if (response["error"].toInt(-1) != 0) {
                _status->setText(tr("资料未刷新：%1；已显示内容为缓存。").arg(response["group_error"].toString())); return;
            }
            const auto current = UserMgr::instance()->messages()->groupState(_chat);
            if (current["group_state"] != "active" ||
                current["membership_epoch"].toString() != _queryEpoch ||
                response["membership_epoch"].toString() != _queryEpoch ||
                response["group_revision"] != current["group_revision"]) {
                _status->setText(tr("群资料已变化，请重新刷新。")); return;
            }
            if (!_version.isEmpty() && _version != response["group_revision"].toString()) {
                _status->setText(tr("群资料已变化，请重新刷新。")); _query.clear(); return;
            }
            _version = response["group_revision"].toString();
            for (const auto &value : response["members"].toArray()) _rows.append(value);
            if (response["load_more"].toBool()) { query(response["next_uid"].toInt()); return; }
            auto cached=UserMgr::instance()->messages()->groupState(_chat);
            if (cached["group_revision"].toString()!=_version) {
                _status->setText(tr("群资料已变化，请刷新。")); return;
            }
            _snapshotReady = true;
            cached["members"]=_rows;
            UserMgr::instance()->messages()->saveDirectory({{"conversations",QJsonArray{cached}}});
            _members->clear();
            for (const auto &value : _rows) {
                const auto row = value.toObject(); const int uid = row["uid"].toInt();
                auto *item = new QListWidgetItem(QIcon(UserMgr::instance()->avatarFor(uid)),
                    row["name"].toString() + QString(" (%1)%2").arg(uid).arg(row["role"].toInt() == 1 ? tr(" 群主") : QString()), _members);
                item->setData(Qt::UserRole,uid);
            }
            showState();
        });
        _observedState = UserMgr::instance()->messages()->groupState(_chat);
        connect(UserMgr::instance()->messages(), &MessageService::directoryChanged, this,
            /** @brief 已落盘身份改变立即撤销旧快照，迟到分页不能恢复旧权限。 */ [this](const QJsonObject &) {
            const auto state = UserMgr::instance()->messages()->groupState(_chat);
            if (state["group_revision"] != _observedState["group_revision"] ||
                state["group_state"] != _observedState["group_state"] ||
                state["membership_epoch"] != _observedState["membership_epoch"] ||
                state["owner_uid"] != _observedState["owner_uid"]) {
                _observedState = state;
                // 首页面回包先落盘再通知；同代次保留请求身份，由回包核对权威版本。
                // 已开始的分页保留原版本，防止后续页跨版本拼接。
                if (state["group_state"] != "active" || state["membership_epoch"].toString() != _queryEpoch)
                    _query.clear();
                _rows = {}; _snapshotReady = false;
                updateActions();
                if (_pending.isEmpty()) _status->setText(tr("群资料已变化，请刷新后继续操作。"));
            }
        });
        connect(UserMgr::instance()->messages(), &MessageService::directoryFailed, this,
            /** @brief 服务端成功但本地未保存时保留重试身份并清楚标注。 */ [this](const QString &) {
            _status->setText(tr("本地保存失败；服务端操作可能已完成，请刷新或重试原操作。"));
            _retry->setEnabled(!_pending.isEmpty());
        });
        connect(UserMgr::instance().get(), &UserMgr::avatarChanged, this,
            /** @brief 异步头像到达后更新包括非好友在内的成员条目。 */ [this](int uid) {
                for (int index=0;index<_members->count();++index) {
                    auto *item=_members->item(index);
                    if (item->data(Qt::UserRole).toInt()==uid) item->setIcon(QIcon(UserMgr::instance()->avatarFor(uid)));
                }
            });
        _pending = UserMgr::instance()->messages()->groupState(_chat)["pending_group_operation"].toObject();
        _retry->setEnabled(!_pending.isEmpty());
        refreshInfo();
    }
private:
    /** @brief 按已落盘群身份启用对应角色操作。 */
    void updateActions() {
        const auto state = UserMgr::instance()->messages()->groupState(_chat);
        const bool active = state["group_state"] == "active";
        const bool owner = state["owner_uid"].toInt() == UserMgr::instance()->uid();
        for (auto it = _buttons.begin(); it != _buttons.end(); ++it) {
            const bool needsSnapshot = it.key() == "add" || it.key() == "remove" || it.key() == "transfer";
            it.value()->setEnabled(active && _pending.isEmpty() && (!needsSnapshot || _snapshotReady)
                && (it.key() == "leave" ? !owner : owner));
        }
    }
    /** @brief 更新普通资料文案，不用于覆盖请求失败原因。 */
    void showState() {
        updateActions();
        const auto state = UserMgr::instance()->messages()->groupState(_chat);
        const QMap<QString,QString> labels{{"active",tr("有效成员")},{"left",tr("已退出")},
            {"removed",tr("已被移除")},{"dissolved",tr("已解散")}};
        _status->setText(state["name"].toString() + tr(" · %1 · 群主 %2 · %3 人")
            .arg(labels.value(state["group_state"].toString(),tr("尚未刷新")))
            .arg(state["owner_uid"].toInt()).arg(state["member_count"].toInt(state["members"].toArray().size())));
    }
    /** @brief 重置分页快照，已离群仅保留本地资料。 */
    void refreshInfo() {
        _snapshotReady = false; _query.clear();
        showState(); _rows = {}; _version.clear();
        _members->clear();
        for (const auto &value : UserMgr::instance()->messages()->groupState(_chat)["members"].toArray()) {
            const auto row=value.toObject();
            auto *item=new QListWidgetItem(row["name"].toString()+QString(" (%1)").arg(row["uid"].toInt()),_members);
            item->setData(Qt::UserRole,row["uid"].toInt());
        }
        if (UserMgr::instance()->messages()->groupState(_chat)["group_state"] != "active") return;
        query(0);
    }
    /** @brief 发送关联窗口及游标的下一页请求。 */
    void query(int after) {
        _queryEpoch = UserMgr::instance()->messages()->groupState(_chat)["membership_epoch"].toString();
        _query = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject request{{"chat_id",_chat},{"after_uid",after},{"request_id",_query}};
        emit TcpMgr::instance()->sendRequested(ID_GROUP_INFO_REQ,QJsonDocument(request).toJson(QJsonDocument::Compact)); _timer->start();
    }
    /** @brief 提交或重试不可变管理命令，不乐观改变成员列表。 */
    void submit() {
        if (_pending.isEmpty()) return;
        const QPointer<GroupPanel> panel(this);
        _retry->setEnabled(false); showState();
        UserMgr::instance()->messages()->saveGroupOperation(_chat, _pending,
            /** @brief 原始命令落盘后才发出；窗口关闭仍保留命令供下次打开重试。 */ [panel] {
            if (!panel) return;
            emit TcpMgr::instance()->sendRequested(ID_GROUP_MANAGE_REQ,QJsonDocument(panel->_pending).toJson(QJsonDocument::Compact));
            panel->_timer->start(); panel->_status->setText(tr("正在等待操作结果…"));
        });
    }
    /** @brief 权威终态返回后清除本地待确认命令，保留原群状态与历史。 */
    void clearPending() {
        _pending = {};
        UserMgr::instance()->messages()->saveGroupOperation(_chat, {});
    }
    /** @brief 收集角色允许的目标和确认，批量添加仅允许当前好友。 */
    void operate(const QString &operation) {
        QJsonObject request{{"chat_id",_chat},{"request_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"operation",operation},{"expected_revision",UserMgr::instance()->messages()->groupState(_chat)["group_revision"]}};
        if (operation == "rename") {
            bool ok = false; const auto name = QInputDialog::getText(this,tr("修改群名"),tr("群名（最多60 UTF-8字节）"),QLineEdit::Normal,QString(),&ok).trimmed();
            if (!ok || name.isEmpty() || name.toUtf8().size() > 60) return; request["name"] = name;
        } else if (operation == "add") {
            QDialog dialog(this); dialog.setWindowTitle(tr("选择好友")); QVBoxLayout layout(&dialog); QListWidget list;
            QSet<int> existing; for (const auto &value : _rows) existing.insert(value.toObject()["uid"].toInt());
            for (const auto &person : UserMgr::instance()->friends()) {
                if (existing.contains(person->_uid)) continue;
                auto *item = new QListWidgetItem(person->_name + QString(" (%1)").arg(person->_uid),&list);
                item->setData(Qt::UserRole,person->_uid); item->setCheckState(Qt::Unchecked);
            }
            layout.addWidget(&list); QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addWidget(&buttons);
            connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            QJsonArray members; for (int i=0;i<list.count();++i) if (list.item(i)->checkState()==Qt::Checked) members.append(list.item(i)->data(Qt::UserRole).toInt());
            if (members.isEmpty()) return; request["members"] = members;
        } else {
            if (operation == "remove" || operation == "transfer") {
                if (!_members->currentItem()) { _status->setText(tr("请先选择成员。")); return; }
                request["target_uid"] = _members->currentItem()->data(Qt::UserRole).toInt();
            }
            if (QMessageBox::question(this,tr("确认操作"),tr("确认执行所选操作？本地已保存历史会保留。")) != QMessageBox::Yes) return;
        }
        _pending = request; submit();
    }
    int _chat;
    QLabel *_status = nullptr;
    QListWidget *_members = nullptr;
    QPushButton *_retry = nullptr;
    QTimer *_timer = nullptr;
    QMap<QString,QPushButton*> _buttons;
    QJsonObject _pending, _observedState;
    bool _snapshotReady = false;
    QJsonArray _rows;
    QString _query, _version, _queryEpoch;
};
