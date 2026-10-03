#pragma once
#include "chatdialog.h"
#include "chatuserlist.h"
#include "chatuseritem.h"
#include "contactuserlist.h"
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>

/** @brief 验证消息提醒不依赖实时通知和会话行的创建时机。 */
inline void ResourceTransferTests::conversationAttentionWidgets()
{
    std::ostringstream warnings;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(warnings);
    auto logger = std::make_shared<spdlog::logger>("conversation-selection-test", sink);
    logger->set_level(spdlog::level::warn);
    const auto previousLogger = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    const auto restoreLogger = qScopeGuard(/** @brief 测试结束或断言失败时恢复原日志接收器。 */ [previousLogger] {
        spdlog::set_default_logger(previousLogger);
    });
    QTemporaryDir root;
    {
        LocalMessageStore store; store.open(root.path(), 7);
        QJsonArray chats;
        for (int id = 1; id <= 30; ++id)
            chats.append(QJsonObject{{"id", id}, {"uid", id + 100}, {"type", "private"}});
        store.mergeDirectory({{"conversations", chats}});
        QVector<StoredMessage> rows;
        for (int id = 1; id <= 3; ++id) {
            StoredMessage row;
            row.messageId = id; row.chatId = 30; row.senderId = id == 3 ? 7 : 130;
            row.recipientId = id == 3 ? 130 : 7; row.content = "notice";
            row.clientMessageId = QString("notice-%1").arg(id); row.sentAt = id * 1000;
            rows.append(row);
        }
        store.applySyncPage(30, 0, 3, rows);
        // 更近的本人消息使待验证的未读会话仍在第一页之外，避免自动展示清除其提醒。
        for (int chat=1;chat<30;++chat) {
            StoredMessage recent; recent.chatId=chat; recent.messageId=1; recent.senderId=7;
            recent.recipientId=chat+100; recent.content=QString("recent-%1").arg(chat); recent.sentAt=4000+chat;
            store.applySyncPage(chat,0,1,{recent});
        }
    }
    const auto user = UserMgr::instance();
    user->setUserInfo(std::make_shared<UserInfo>(7, "self", ""));
    auto *service = user->messages();
    // 断言失败同样先释放账号锁，再让临时目录析构。
    const auto cleanup = qScopeGuard(/** @brief 即使断言失败也先停止存储并有界等待锁释放。 */ [service, &root] {
        service->stop();
        QElapsedTimer elapsed; elapsed.start();
        while (QFileInfo::exists(root.path() + "/messages.lock") && elapsed.elapsed() < 15000)
            QTest::qWait(10);
    });
    QSignalSpy restored(service, &MessageService::directoryRestored);
    QSignalSpy syncRequests(service, &MessageService::syncRequested);
    service->start(root.path(), 7);
    QTRY_COMPARE(restored.size(), 1);
    for (int attempt = 0; attempt < 3; ++attempt) {
        ChatDialog window; window.resize(1000, 650); window.show();
        auto *list = window.findChild<ChatUserList*>();
        QTRY_VERIFY(list->count() > 0);
        QVERIFY2(warnings.str().find("invalid chat uid=0") == std::string::npos,
            "normal initial conversation selection must not warn");
        auto *first = qobject_cast<ChatUserItem*>(list->itemWidget(list->item(0)));
        QVERIFY(first); QCOMPARE(first->getChatInfo()->getChatId(),29);
        QCOMPARE(first->findChild<QLabel*>("user_chat_label")->text(),QString("recent-29"));
        QTRY_VERIFY_WITH_TIMEOUT((window.loadChatUserList(), list->count() == 30), 5000);
        ChatUserItem *target = nullptr;
        for (auto *item : window.findChildren<ChatUserItem*>())
            if (item->getChatInfo()->getChatId() == 30) target = item;
        QVERIFY(target);
        auto *badge = target->findChild<QLabel*>("new_msg_count_label");
        QVERIFY(badge);
        QTRY_COMPARE_WITH_TIMEOUT(badge->text(), attempt == 2 ? QString("0") : QString("2"), 2000);
        QCOMPARE(badge->isHidden(), attempt == 2);
        if (attempt == 1) {
            QApplication::setActiveWindow(&window);
            QTRY_VERIFY(window.isActiveWindow());
            QListWidgetItem *row = nullptr;
            for (int index = 0; index < list->count(); ++index)
                if (list->itemWidget(list->item(index)) == target) row = list->item(index);
            QVERIFY(row);
            list->scrollToItem(row);
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(row).center());
            QTRY_COMPARE_WITH_TIMEOUT(badge->text(), QString("0"), 3000);
            QVERIFY(badge->isHidden());
        }
        service->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path() + "/messages.lock"), 15000);
        service->start(root.path(), 7);
        QTRY_COMPARE(restored.size(), attempt + 2);
    }
    {
        ChatDialog window; window.resize(1000, 650); window.show();
        auto *list = window.findChild<ChatUserList*>();
        QTRY_VERIFY_WITH_TIMEOUT((window.loadChatUserList(), list->count() == 30), 5000);
        ChatUserItem *target = nullptr;
        for (auto *item : window.findChildren<ChatUserItem*>())
            if (item->getChatInfo()->getChatId() == 30) target = item;
        QVERIFY(target);
        auto *badge = target->findChild<QLabel*>("new_msg_count_label");
        window.findChild<ChatPage*>()->setChatInfo(target->getChatInfo());
        QApplication::setActiveWindow(&window);
        QTRY_COMPARE(badge->text(), QString("0"));
        for (int mode = 0; mode < 3; ++mode) {
            QDialog modal(&window); modal.setModal(true);
            if (mode == 0) window.hide();
            else if (mode == 1) { window.showMinimized(); QTRY_VERIFY(window.isMinimized()); }
            else { modal.show(); QTRY_COMPARE(QApplication::activeModalWidget(), &modal); }
            service->synchronize(30);
            QJsonObject request;
            QTRY_VERIFY_WITH_TIMEOUT((/** @brief 查找当前会话和已提交游标对应的真实同步请求。 */ [&] {
                for (const auto &entry : syncRequests)
                    if (entry[0].toJsonObject()["chat_id"].toInt() == 30
                        && entry[0].toJsonObject()["after_id"].toInteger() == mode + 3)
                        request = entry[0].toJsonObject();
                return !request.isEmpty();
            }()), 3000);
            request["error"] = 0; request["load_more"] = false; request["next_cursor"] = mode + 4;
            request["msgs"] = QJsonArray{QJsonObject{{"message_id", mode + 4}, {"send_id", 130},
                {"recv_id", 7}, {"content", "background arrival"}, {"created_at", 1700000000},
                {"msg_uuid", QString("background-%1").arg(mode)}}};
            QSignalSpy loaded(service, &MessageService::historyLoaded);
            service->acceptSyncPage(request);
            QTRY_VERIFY(!loaded.isEmpty());
            QTRY_COMPARE(badge->text(), QString("1"));
            modal.hide(); window.showNormal(); QApplication::setActiveWindow(&window);
            QTRY_VERIFY(window.isActiveWindow());
            // 模态退出时平台可能复用活动窗口；显式重开同一会话也必须正确清除。
            window.findChild<ChatPage*>()->setChatInfo(target->getChatInfo());
            QTRY_COMPARE(badge->text(), QString("0"));
        }
    }
    service->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path() + "/messages.lock"), 15000);
}

/** @brief 按用户可见文字取得按钮，避免依赖控件创建顺序。 */
inline QPushButton *acceptanceButton(QWidget *parent, const QString &text)
{
    for (auto *button : parent->findChildren<QPushButton*>())
        if (button->text() == text) return button;
    return nullptr;
}

/** @brief 查询实际列表文本，确认目录提交更新到所有已展示位置。 */
inline bool acceptanceHasLabel(QWidget *parent, const QString &name, const QString &text)
{
    for (auto *label : parent->findChildren<QLabel*>(name))
        if (label->text()==text) return true;
    return false;
}

/** @brief 验证真实搜索控件翻页后双击命中可以选中并显示历史消息。 */
inline void ResourceTransferTests::localHistorySearchWidgets()
{
    QTemporaryDir root;
    {
        LocalMessageStore store; store.open(root.path(),7);
        QVector<StoredMessage> rows;
        for (int id=1; id<=65; ++id) {
            StoredMessage row; row.messageId=id; row.chatId=870; row.senderId=8;
            row.recipientId=7; row.content=QString("needle-%1").arg(id);
            row.clientMessageId=QString("search-%1").arg(id); row.sentAt=id*1000;
            rows.push_back(row);
        }
        store.applySyncPage(870,0,65,rows);
    }
    auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"self",""));
    auto *service=user->messages(); QSignalSpy restored(service,&MessageService::directoryRestored);
    service->start(root.path(),7); QTRY_COMPARE_WITH_TIMEOUT(restored.size(),1,15000);
    ChatPage page; page.resize(750,550); page.show();
    connect(service,&MessageService::historyLoaded,&page,&ChatPage::applyStoredHistory);
    connect(&page,&ChatPage::historyRequested,service,/** @brief 使用生产存储异步加载当前页面。 */
        [service](int chat,qint64 before) { service->loadHistory(chat,before); });
    page.setChatInfo(std::make_shared<ChatInfo>(8,"peer",QString(),QString(),870,ChatType::PRIVATE));
    auto *view=page.findChild<ChatDetailList*>(); QTRY_VERIFY(view->model()->rowCount()>0);
    view->setCurrentIndex(view->model()->index(0,0));
    QTest::keyClick(view,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(),view->currentIndex().data(MessageListModel::TextRole).toString());
    QTest::mouseClick(acceptanceButton(&page,QStringLiteral("查找历史")),Qt::LeftButton);
    auto *dialog=page.findChild<QDialog*>(); QVERIFY(dialog);
    auto *input=dialog->findChild<QLineEdit*>(); auto *list=dialog->findChild<QListWidget*>();
    input->setText("needle"); QTest::keyClick(input,Qt::Key_Return);
    QTRY_COMPARE(list->count(),50);
    auto *more=acceptanceButton(dialog,QStringLiteral("下一页")); QVERIFY(more->isEnabled());
    QTest::mouseClick(more,Qt::LeftButton); QTRY_COMPARE(list->count(),65);
    QVERIFY(!more->isEnabled());
    auto *target=list->item(60); const auto id=target->data(Qt::UserRole).toLongLong();
    list->scrollToItem(target); QCoreApplication::processEvents();
    QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(target).center());
    QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(target).center());
    // A periodic refresh can finish before the explicitly requested search range.
    page.applyStoredHistory(870,0,{},false);
    QTRY_COMPARE(view->currentIndex().data(MessageListModel::MessageIdRole).toLongLong(),id);
    QVERIFY(view->viewport()->rect().intersects(view->visualRect(view->currentIndex())));
    service->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path()+"/messages.lock"),15000);
}

/** @brief 验证完整本地目录可打开超出初始列表页的联系人和群。 */
inline void ResourceTransferTests::localDirectorySearchWidgets()
{
    QTemporaryDir root;
    {
        LocalMessageStore store; store.open(root.path(),7);
        QJsonArray contacts, chats;
        for (int id=101;id<=170;++id) {
            contacts.append(QJsonObject{{"id",id},{"uid",id},{"name",QString("friend-%1").arg(id)}});
            chats.append(QJsonObject{{"id",id+400},{"uid",id},{"type","private"}});
            chats.append(QJsonObject{{"id",id+900},{"name",QString("group-%1").arg(id)},
                {"type","group"},{"group_revision","1"},{"group_state","active"},
                {"membership_epoch","1"},{"joined_after_id","0"},{"owner_uid",7}});
        }
        store.mergeDirectory({{"contacts",contacts},{"conversations",chats}});
    }
    auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"self",""));
    auto *service=user->messages(); QSignalSpy restored(service,&MessageService::directoryRestored);
    service->start(root.path(),7); QTRY_COMPARE_WITH_TIMEOUT(restored.size(),1,15000);
    ChatDialog window; window.resize(1000,650); window.show();
    auto *chatList=window.findChild<ChatUserList*>(); QTRY_VERIFY(chatList->count()>0);
    QVERIFY(chatList->count()<50);
    for (const bool group : {false,true}) {
        QTest::mouseClick(acceptanceButton(&window,QStringLiteral("本地查找")),Qt::LeftButton);
        QPointer<QDialog> search;
        for (auto *candidate:window.findChildren<QDialog*>())
            if (candidate->windowTitle()==QStringLiteral("联系人与群")) search=candidate;
        QVERIFY(search);
        search->findChild<QComboBox*>()->setCurrentIndex(group?1:0);
        auto *input=search->findChild<QLineEdit*>(); auto *rows=search->findChild<QListWidget*>();
        input->clear(); QTest::keyClick(input,Qt::Key_Return); QTRY_COMPARE(rows->count(),50);
        QTest::mouseClick(acceptanceButton(search,QStringLiteral("下一页")),Qt::LeftButton);
        QTRY_COMPARE(rows->count(),70);
        input->setText(group?"group-170":"friend-170"); QTest::keyClick(input,Qt::Key_Return);
        QTRY_COMPARE(rows->count(),1);
        const auto rect=rows->visualItemRect(rows->item(0));
        QTest::mouseClick(rows->viewport(),Qt::LeftButton,Qt::NoModifier,rect.center());
        QTest::mouseDClick(rows->viewport(),Qt::LeftButton,Qt::NoModifier,rect.center());
        auto *page=window.findChild<ChatPage*>();
        QTRY_COMPARE(page->findChild<QLabel*>("title_label")->text(),group?QString("group-170"):QString("friend-170"));
        QVERIFY(!search || !search->isVisible());
    }
    service->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path()+"/messages.lock"),15000);
}

/** @brief 验证备注只在持久化目录更新后显示，拒绝、超时和保存错误不会显示成功。 */
inline void ResourceTransferTests::friendRemarkOutcomeWidgets()
{
    QTemporaryDir root;
    auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"self",""));
    auto *service=user->messages(); QSignalSpy restored(service,&MessageService::directoryRestored);
    service->start(root.path(),7); QTRY_COMPARE_WITH_TIMEOUT(restored.size(),1,15000);
    QSignalSpy changed(service,&MessageService::directoryChanged);
    service->saveDirectory({{"contacts",QJsonArray{QJsonObject{{"id",8},{"uid",8},{"name","peer"},{"backname","old"}}}},
        {"conversations",QJsonArray{QJsonObject{{"id",880},{"uid",8},{"type","private"}}}}});
    QTRY_COMPARE(changed.size(),1);
    ChatDialog window; window.resize(1000,650); window.show();
    auto *page=window.findChild<ChatPage*>();
    page->setChatInfo(user->chatInfo(880));
    auto *title=page->findChild<QLabel*>("title_label"); QCOMPARE(title->text(),QString("old"));
    QTRY_VERIFY(acceptanceHasLabel(&window,"contact_user_name_label","old"));
    QTRY_VERIFY(acceptanceHasLabel(&window,"user_name_label","old"));
    QSignalSpy requests(TcpMgr::instance().get(),&TcpMgr::sendRequested);
    for (int outcome=0;outcome<4;++outcome) {
        QTimer::singleShot(0,&window,/** @brief 通过真实备注对话框确认新值。 */ [&window] {
            auto *input=window.findChild<QInputDialog*>(); if (input) { input->setTextValue("new"); input->accept(); }
        });
        QTest::mouseClick(acceptanceButton(page,QStringLiteral("资料 / 备注")),Qt::LeftButton);
        QVERIFY(!requests.isEmpty());
        const auto request=QJsonDocument::fromJson(requests.last()[1].toByteArray()).object();
        QVERIFY(!request["request_id"].toString().isEmpty());
        QString displayed;
        QTimer closer;
        connect(&closer,&QTimer::timeout,&window,/** @brief 记录用户实际看到的结果并关闭提示。 */ [&window,&displayed] {
            if (auto *box=window.findChild<QMessageBox*>()) { displayed=box->text(); box->accept(); }
        });
        closer.start(10);
        if (outcome<3) {
            if (outcome==2) {
                service->saveDirectory({{"contacts",QJsonArray{QJsonObject{{"id",8},{"backname","new"}}}}});
                QTRY_COMPARE(changed.size(),2);
            }
            emit TcpMgr::instance()->groupResponse(ID_FRIEND_REMARK_RSP,
                QJsonObject{{"request_id",request["request_id"]},{"error",outcome==0?1:0},{"local_save_failed",outcome==1}});
        }
        QTRY_VERIFY_WITH_TIMEOUT(!displayed.isEmpty(),12000);
        if (outcome==0) QVERIFY(displayed.contains(QStringLiteral("未保存")));
        if (outcome==1) QVERIFY(displayed.contains(QStringLiteral("本地保存失败")));
        if (outcome==2) QVERIFY(displayed.contains(QStringLiteral("备注已保存")));
        if (outcome==3) QVERIFY(displayed.contains(QStringLiteral("尚未确认")));
        const auto expected=outcome>=2?QString("new"):QString("old");
        QCOMPARE(title->text(),expected);
        QVERIFY(acceptanceHasLabel(&window,"contact_user_name_label",expected));
        QVERIFY(acceptanceHasLabel(&window,"user_name_label",expected));
    }
    QTimer::singleShot(0,&window,/** @brief 发起备注后立即导航到另一会话。 */ [&window] {
        if (auto *input=window.findChild<QInputDialog*>()) { input->setTextValue("late"); input->accept(); }
    });
    QTest::mouseClick(acceptanceButton(page,QStringLiteral("资料 / 备注")),Qt::LeftButton);
    const auto lateRequest=QJsonDocument::fromJson(requests.last()[1].toByteArray()).object();
    page->setChatInfo(std::make_shared<ChatInfo>(9,"another",QString(),QString(),881,ChatType::PRIVATE));
    QString unrelatedPrompt;
    QTimer closer;
    connect(&closer,&QTimer::timeout,&window,/** @brief 捕获任何错误进入新会话的旧操作提示。 */ [&window,&unrelatedPrompt] {
        if (auto *box=window.findChild<QMessageBox*>()) { unrelatedPrompt=box->text(); box->accept(); }
    });
    closer.start(10);
    emit TcpMgr::instance()->groupResponse(ID_FRIEND_REMARK_RSP,
        QJsonObject{{"request_id",lateRequest["request_id"]},{"error",0}});
    QVERIFY(unrelatedPrompt.isEmpty());
    service->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path()+"/messages.lock"),15000);
}


/** @brief 将三类群资源经真实页面解析和绘制，并验证图片预览及附件下载路径。 */
inline void ResourceTransferTests::groupResourceCardWidgets()
{
    QTemporaryDir root;
    auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"self",""));
    auto *service=user->messages(); QSignalSpy restored(service,&MessageService::directoryRestored);
    service->start(root.path(),7); QTRY_COMPARE_WITH_TIMEOUT(restored.size(),1,15000);
    QSignalSpy changed(service,&MessageService::directoryChanged);
    service->saveDirectory({{"conversations",QJsonArray{QJsonObject{{"id",890},{"type","group"},
        {"name","resources"},{"group_state","active"},{"group_revision","1"},{"membership_epoch","1"}}}}});
    QTRY_COMPARE(changed.size(),1);
    ChatPage page; page.resize(750,650); page.show();
    page.setChatInfo(user->chatInfo(890));
    auto *view=page.findChild<ChatDetailList*>();
    const QStringList media{"image/png","video/x-msvideo","application/octet-stream"};
    const QStringList names{"group.png","group.avi","group.txt"};
    const QVector<MessageType> types{MessageType::Image,MessageType::Video,MessageType::File};
    for (int index=0;index<media.size();++index) {
        const QJsonObject descriptor{{"resource_id",QString("card-%1").arg(index)},
            {"media_type",media[index]},{"name",names[index]}};
        const auto content="@resource:v1:"+QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact));
        page.appendChatMsg(std::make_shared<TextChatData>(QString("resource-%1").arg(index),890,
            ChatType::GROUP,ChatMessageType::TEXT_TYPE,content,8,QTime::currentTime()));
        const auto row=view->model()->index(index,0);
        QCOMPARE(row.data(MessageListModel::MessageTypeRole).toInt(),int(types[index]));
        QVERIFY(row.data(MessageListModel::TextRole).toString().contains(names[index]));
        QVERIFY(!row.data(MessageListModel::TextRole).toString().contains("@resource:v1:"));
    }
    const auto imagePath=root.filePath("group.png");
    QImage image(100,80,QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(imagePath));
    auto *transfer=page.findChild<ResourceTransferManager*>();
    emit transfer->downloaded("card-0",imagePath);
    emit transfer->downloaded("card-1",root.filePath("group.avi"));
    emit transfer->downloaded("card-2",root.filePath("group.txt"));
    QCoreApplication::processEvents();
    for (int index=0;index<media.size();++index) {
        const auto row=view->model()->index(index,0);
        QCOMPARE(row.data(MessageListModel::LocalResourcePathRole).toString(),root.filePath(names[index]));
        const auto preview=qvariant_cast<QPixmap>(row.data(MessageListModel::ResourcePreviewRole));
        QCOMPARE(preview.isNull(),index!=0);
        view->scrollTo(row,QAbstractItemView::PositionAtCenter);
        QCoreApplication::processEvents();
        QVERIFY(view->viewport()->rect().intersects(view->visualRect(row)));
        QVERIFY(!view->viewport()->grab().isNull());
    }
    service->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(root.path()+"/messages.lock"),15000);
}
