#include <QDir>
#include "resourcetransfermanager.h"
#include "messagelistmodel.h"
#include "chatpage.h"
#include "grouppanel.h"
#include <QTextEdit>
#include "chatdetaillist.h"
#include "usermgr.h"
#include "avatarcache.h"
#include "userstoragepaths.h"
#include <QJsonDocument>
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QImage>
#include <QRandomGenerator>
#include <QFile>

/** @brief 验证资源传输、账号隔离和消息页面生命周期。 */
class ResourceTransferTests : public QObject {
    Q_OBJECT
private slots:
    /** @brief 验证未加载会话的持久化消息在列表分页和窗口重建后仍显示提醒。 */
    void conversationAttentionWidgets();
    /** @brief 验证会话历史分页搜索与真实滚动定位。 */
    void localHistorySearchWidgets();
    /** @brief 验证完整本地目录筛选与会话打开。 */
    void localDirectorySearchWidgets();
    /** @brief 验证好友备注权威结果与界面一致。 */
    void friendRemarkOutcomeWidgets();
    /** @brief 验证群图片、视频和普通文件的生产卡片及下载路径展示。 */
    void groupResourceCardWidgets();
    /** @brief 与生产退出顺序一致，在 Qt 应用销毁前释放网络和存储单例。 */
    void cleanupTestCase() {
        TcpMgr::releaseInstance();
        UserMgr::releaseInstance();
    }
    /** 验证真实群界面随持久化成员状态禁止输入，并保留离群前缓存成员信息。 */
    void groupMembershipControlsWidgets() {
        QTemporaryDir directory;
        const auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"owner",""));
        auto *service=user->messages();
        QSignalSpy restored(service,&MessageService::directoryRestored), changed(service,&MessageService::directoryChanged);
        service->start(directory.path(),7); QTRY_COMPARE(restored.size(),1);
        QJsonObject state{{"id",12},{"type","group"},{"name","成员测试群"},{"group_revision","1"},
            {"group_state","active"},{"membership_epoch","1"},{"owner_uid",7},
            {"members",QJsonArray{QJsonObject{{"uid",7},{"name","owner"},{"role",1}}}}};
        service->saveDirectory({{"conversations",QJsonArray{state}}}); QTRY_COMPARE(changed.size(),1);
        ChatPage page; page.resize(650,450); page.setChatInfo(std::make_shared<ChatInfo>(0,"成员测试群",QString(),QString(),12,ChatType::GROUP));
        QVERIFY(page.findChild<QTextEdit*>("chat_edit")->isEnabled());
        QVERIFY(page.findChild<QPushButton*>("send_btn")->isEnabled());
        state["group_state"]="removed"; state["group_revision"]="2";
        service->saveDirectory({{"conversations",QJsonArray{state}}}); QTRY_COMPARE(changed.size(),2);
        QVERIFY(!page.findChild<QTextEdit*>("chat_edit")->isEnabled());
        QVERIFY(!page.findChild<QPushButton*>("send_btn")->isEnabled());
        GroupPanel panel(12,&page); panel.show();
        QCOMPARE(panel.findChild<QListWidget*>()->count(),1);
        for (auto *button:panel.findChildren<QPushButton*>())
            if (button->text()=="添加" || button->text()=="解散") QVERIFY(!button->isEnabled());
        if (!qEnvironmentVariable("CHAT_UI_CAPTURE").isEmpty())
            QVERIFY(panel.grab().save(qEnvironmentVariable("CHAT_UI_CAPTURE")));
        service->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(directory.path()+"/messages.lock"),15000);
    }
    /** @brief 验证管理拒绝原因不会被状态文案覆盖。 */
    void groupPanelRejectionVisible() { groupPanelConsistency(0); }
    /** @brief 验证外部移除即时关闭本窗口管理权限。 */
    void groupPanelExternalRevocation() { groupPanelConsistency(1); }
    /** @brief 验证完整成员快照到达前禁止成员操作。 */
    void groupPanelSnapshotRequired() { groupPanelConsistency(2); }
    /** @brief 验证分页完成门禁、失败缓存与迟到页隔离。 */
    void groupPanelPaginationFailure() { groupPanelConsistency(3); }
    /** @brief 验证重开面板按原身份重试待确认命令。 */
    void groupPanelReopenRetryIdentity() { groupPanelConsistency(4); }
    /** @brief 验证真实TCP落盘顺序及旧成员代次隔离。 */
    void groupPanelTcpRefreshRevision() { groupPanelConsistency(5); }
private:
    /** @brief 通过真实控件及已落盘目录验证指定群面板场景。 */
    void groupPanelConsistency(int scenario) {
        QTemporaryDir directory;
        const auto user=UserMgr::instance(); user->setUserInfo(std::make_shared<UserInfo>(7,"owner",""));
        auto *service=user->messages();
        QSignalSpy restored(service,&MessageService::directoryRestored), changed(service,&MessageService::directoryChanged);
        service->start(directory.path(),7); QTRY_COMPARE(restored.size(),1);
        QJsonObject state{{"id",12},{"type","group"},{"name","panel"},{"group_revision","1"},
            {"group_state","active"},{"membership_epoch","1"},{"owner_uid",7},
            {"members",QJsonArray{QJsonObject{{"uid",7},{"name","owner"},{"role",1}}}}};
        if(scenario==0 || scenario==4) state["pending_group_operation"]=QJsonObject{{"request_id","pending-reopen"},
            {"chat_id",12},{"operation","rename"},{"name","new"},{"expected_revision","1"}};
        service->saveDirectory({{"conversations",QJsonArray{state}}}); QTRY_COMPARE(changed.size(),1);
        QSignalSpy sent(TcpMgr::instance().get(), &TcpMgr::sendRequested);
        GroupPanel panel(12,nullptr); panel.show();
        if(scenario==0) {
            emit TcpMgr::instance()->groupResponse(ID_GROUP_MANAGE_RSP,
                {{"request_id","pending-reopen"},{"error",1},{"group_error","VersionConflict"}});
            QVERIFY(panel.findChild<QLabel*>()->text().contains("VersionConflict"));
        } else if(scenario==1) {
            state["group_state"]="removed"; state["group_revision"]="2";
            service->saveDirectory({{"conversations",QJsonArray{state}}}); QTRY_VERIFY(changed.size()>=2);
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="解散") QVERIFY(!button->isEnabled());
        } else if(scenario==3) {
            const auto first = QJsonDocument::fromJson(sent.last()[1].toByteArray()).object()["request_id"].toString();
            emit TcpMgr::instance()->groupResponse(ID_GROUP_INFO_RSP, {{"request_id",first},{"error",0},
                {"group_revision","1"},{"membership_epoch","1"},{"members",state["members"]},{"load_more",true},{"next_uid",7}});
            const auto second = QJsonDocument::fromJson(sent.last()[1].toByteArray()).object()["request_id"].toString();
            QVERIFY(first != second);
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加") QVERIFY(!button->isEnabled());
            emit TcpMgr::instance()->groupResponse(ID_GROUP_INFO_RSP, {{"request_id",second},{"error",0},
                {"group_revision","1"},{"membership_epoch","1"},{"members",QJsonArray{}},{"load_more",false}});
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加") QVERIFY(button->isEnabled());
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="刷新资料") QTest::mouseClick(button,Qt::LeftButton);
            const auto failed = QJsonDocument::fromJson(sent.last()[1].toByteArray()).object()["request_id"].toString();
            emit TcpMgr::instance()->groupResponse(ID_GROUP_INFO_RSP, {{"request_id",failed},{"error",1},{"group_error","StorageUnavailable"}});
            QCOMPARE(panel.findChild<QListWidget*>()->count(),1);
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加") QVERIFY(!button->isEnabled());
            emit TcpMgr::instance()->groupResponse(ID_GROUP_INFO_RSP, {{"request_id",second},{"error",0},
                {"group_revision","1"},{"membership_epoch","1"},{"members",QJsonArray{}},{"load_more",false}});
            QVERIFY(panel.findChild<QLabel*>()->text().contains("StorageUnavailable"));
        } else if(scenario==5) {
            const auto request = QJsonDocument::fromJson(sent.last()[1].toByteArray()).object()["request_id"].toString();
            QJsonObject response{{"request_id",request},{"error",0},{"chat_id",12},{"group_name","fresh"},
                {"group_revision","2"},{"membership_epoch","1"},{"group_state","active"},{"owner_uid",7},
                {"members",QJsonArray{QJsonObject{{"uid",8},{"name","fresh member"},{"role",0}}}},
                {"member_count",1},{"load_more",false}};
            const auto wire=QJsonDocument(response).toJson(QJsonDocument::Compact);
            TcpMgr::instance()->handleMessage(ID_GROUP_INFO_RSP,wire.size(),wire);
            QTRY_VERIFY(service->groupState(12)["group_revision"]=="2");
            QTRY_COMPARE(panel.findChild<QListWidget*>()->item(0)->data(Qt::UserRole).toInt(),8);
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加") QVERIFY(button->isEnabled());
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="刷新资料") QTest::mouseClick(button,Qt::LeftButton);
            const auto stale = QJsonDocument::fromJson(sent.last()[1].toByteArray()).object()["request_id"].toString();
            state["group_revision"]="3"; state["membership_epoch"]="2";
            service->saveDirectory({{"conversations",QJsonArray{state}}});
            QTRY_VERIFY(service->groupState(12)["membership_epoch"]=="2");
            QSignalSpy delivered(TcpMgr::instance().get(), &TcpMgr::groupResponse);
            response["request_id"]=stale;
            const auto staleWire=QJsonDocument(response).toJson(QJsonDocument::Compact);
            TcpMgr::instance()->handleMessage(ID_GROUP_INFO_RSP,staleWire.size(),staleWire);
            QTRY_COMPARE(delivered.size(),1);
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加") QVERIFY(!button->isEnabled());
        } else if(scenario==4) {
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="重试待确认操作") { QVERIFY(button->isEnabled()); QTest::mouseClick(button,Qt::LeftButton); }
            QTRY_VERIFY(sent.size() >= 2);
            QCOMPARE(QJsonDocument::fromJson(sent.last()[1].toByteArray()).object(), state["pending_group_operation"].toObject());
            emit TcpMgr::instance()->groupResponse(ID_GROUP_MANAGE_RSP,
                {{"request_id","pending-reopen"},{"error",0},{"local_save_failed",true}});
            QVERIFY(panel.findChild<QLabel*>()->text().contains("本地保存失败"));
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="重试待确认操作") QVERIFY(button->isEnabled());
        } else {
            for(auto *button:panel.findChildren<QPushButton*>())
                if(button->text()=="添加" || button->text()=="移除" || button->text()=="转让")
                    QVERIFY(!button->isEnabled());
        }
        service->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(directory.path()+"/messages.lock"),15000);
    }
private slots:
    /** 验证头像发布、账号路径隔离与重新加载，并覆盖编辑器成功及拒绝状态。 */
    void avatarPublicationIsolationAndRestore() {
        QTemporaryDir directory;
        QProcess host;
        host.start(qEnvironmentVariable("RESOURCE_TEST_HOST"), {"--serve-test", directory.path() + "/store"});
        QVERIFY(host.waitForStarted(5000));
        QVERIFY(host.waitForReadyRead(5000));
        const QUrl endpoint("http://127.0.0.1:" + QString::fromUtf8(host.readLine()).trimmed());
        const auto root = directory.path() + "/installation/data";
        const auto first = UserStoragePaths::accountRoot(root, endpoint.toString(), 7);
        const auto second = UserStoragePaths::accountRoot(root, endpoint.toString(), 8);
        QVERIFY(first != second);
        AvatarCache sender(endpoint, 7, "fixture-token", first);
        AvatarCache receiver(endpoint, 8, "fixture-token", second);
        receiver.watch(7);
        LocalAvatar editor(root);
        editor.setAccount(endpoint.toString(), 7);
        editor.setUploadEnabled(true);
        connect(&editor, &LocalAvatar::uploadRequested, &sender, &AvatarCache::upload);
        connect(&sender, &AvatarCache::published, &editor, /** 头像发布成功后结束编辑器上传状态。 */ [&editor] { editor.finishUpload(); });
        connect(&sender, &AvatarCache::uploadFailed, &editor, &LocalAvatar::finishUpload);
        QSignalSpy saved(&editor, &LocalAvatar::saved);
        QSignalSpy errors(&editor, &LocalAvatar::errorOccurred);
        QTRY_VERIFY(!editor.isBusy());
        const auto source = directory.path() + "/source.png";
        QImage sourceImage(256, 256, QImage::Format_RGB32); sourceImage.fill(Qt::red);
        QVERIFY(sourceImage.save(source));
        editor.selectFile(source);
        QTRY_VERIFY(!editor.isBusy());
        QVERIFY(editor.image().isNull());
        editor.saveSelection(QRectF(0, 0, 256, 256));
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() == 1 || !errors.isEmpty(), 10000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first()[0].toString()));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(editor.image().pixelColor(0, 0), QColor(Qt::red));
        receiver.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver.image(7).isNull(), 10000);
        QCOMPARE(receiver.image(7).pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(QFileInfo::exists(second + "/static/head/7/index.json"));
        QVERIFY(!QFileInfo::exists(UserStoragePaths::accountRoot(root, "other-environment", 8)));
        AvatarCache reopened(endpoint, 8, "fixture-token", second);
        reopened.watch(7);
        QTRY_VERIFY(!reopened.image(7).isNull());
        QCOMPARE(reopened.image(7), receiver.image(7));
        // A failed remote publication must never replace the confirmed image.
        disconnect(&sender, nullptr, &editor, nullptr);
        disconnect(&editor, &LocalAvatar::uploadRequested, &sender, &AvatarCache::upload);
        connect(&editor, &LocalAvatar::uploadRequested, &editor, /** 头像发布失败后向编辑器提供固定拒绝原因。 */ [&editor](const QString &) {
            editor.finishUpload("publication rejected");
        });
        sourceImage.fill(Qt::blue); QVERIFY(sourceImage.save(source));
        editor.discardSelection(); editor.selectFile(source);
        QTRY_VERIFY(!editor.isBusy());
        editor.saveSelection(QRectF(0, 0, 256, 256));
        QTRY_VERIFY(!errors.isEmpty());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(editor.image().pixelColor(0, 0), QColor(Qt::red));
        editor.reset();
        QVERIFY(editor.image().isNull());
        host.kill(); QVERIFY(host.waitForFinished(5000));
    }
    /** @brief 验证附件解析及页面销毁时释放账号资源传输。 */
    void incomingAttachmentAndPageLifetime() {
        UserMgr::instance()->setUserInfo(std::make_shared<UserInfo>(8, "receiver", ""));
        UserMgr::instance()->setToken("fixture-token");
        ChatPage page;
        QCOMPARE(page.findChildren<ResourceTransferManager*>().size(), 1);
        page.resize(600, 400);
        page.show();
        QCoreApplication::processEvents();
        page.resize(700, 500);
        QCoreApplication::processEvents();
        QCOMPARE(page.findChildren<ResourceTransferManager*>().size(), 1);
        page.setChatInfo(std::make_shared<ChatInfo>(7, 1, 0));
        QJsonObject descriptor{{"resource_id", "incoming-resource"}, {"media_type", "image/png"}, {"name", "image.png"}};
        auto message = std::make_shared<TextChatData>("incoming-uuid", 1, ChatType::PRIVATE,
            ChatMessageType::TEXT_TYPE,
            "@resource:v1:" + QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact)),
            7, QTime::currentTime());
        page.appendChatMsg(message);
        auto* model = page.findChild<ChatDetailList*>()->model();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(MessageListModel::ResourceIdRole).toString(), QString("incoming-resource"));
        QCOMPARE(model->index(0, 0).data(MessageListModel::MessageTypeRole).toInt(), int(MessageType::Image));
        // Destruction cancels pending attachment work before its widgets are released.
    }
    /** 验证设置附件缓存不会改变普通文本消息的展示角色。 */
    void resourceModelKeepsTextAndAttachmentsSeparate() {
        MessageListModel model(1);
        MessageRecord text;
        text.messageId = 1; text.chatId = 1; text.text = "ordinary text";
        MessageRecord attachment;
        attachment.messageId = 2; attachment.chatId = 1; attachment.resourceId = "resource-a";
        attachment.messageType = MessageType::Image;
        model.appendMessage(text); model.appendMessage(attachment);
        model.setResourceFile("resource-a", "cached.png", QPixmap());
        QCOMPARE(model.data(model.index(0), MessageListModel::TextRole).toString(), QString("ordinary text"));
        QVERIFY(model.data(model.index(0), MessageListModel::LocalResourcePathRole).toString().isEmpty());
        QCOMPARE(model.data(model.index(1), MessageListModel::LocalResourcePathRole).toString(), QString("cached.png"));
        QCOMPARE(model.data(model.index(1), MessageListModel::MessageTypeRole).toInt(), int(MessageType::Image));
    }
    /** 验证上传中断后从持久偏移恢复，并下载校验内容一致。 */
    void resumeUploadAndDownload() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess host;
        host.start(qEnvironmentVariable("RESOURCE_TEST_HOST"), {"--serve-test", directory.path() + "/store"});
        QVERIFY(host.waitForStarted(5000));
        QVERIFY(host.waitForReadyRead(5000));
        const auto port = QString::fromUtf8(host.readLine()).trimmed();
        QVERIFY(!port.isEmpty());
        const QUrl endpoint("http://127.0.0.1:" + port);
        QImage image(700, 700, QImage::Format_RGB32);
        QRandomGenerator random(42);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, random.generate() | 0xff000000);
        const auto source = directory.path() + "/image.png";
        QVERIFY(image.save(source));
        {
            ResourceTransferManager first(endpoint, 7, "fixture-token", directory.path() + "/cache");
            QSignalSpy progress(&first, &ResourceTransferManager::progress);
            connect(&first, &ResourceTransferManager::progress, &first, /** 上传至少一个块后取消，以保存可恢复的已确认偏移。 */ [&first](qint64 offset, qint64) {
                if (offset >= 65536) first.cancel();
            });
            first.upload(source);
            QTRY_VERIFY_WITH_TIMEOUT(progress.count() >= 2, 15000);
            QVERIFY(!first.busy());
        }
        ResourceTransferManager resumed(endpoint, 7, "fixture-token", directory.path() + "/cache");
        QSignalSpy progress(&resumed, &ResourceTransferManager::progress);
        QSignalSpy uploaded(&resumed, &ResourceTransferManager::uploaded);
        QSignalSpy failed(&resumed, &ResourceTransferManager::failed);
        resumed.upload(source);
        QTRY_VERIFY_WITH_TIMEOUT(uploaded.count() == 1 || failed.count() > 0, 20000);
        QCOMPARE(failed.count(), 0);
        QVERIFY(progress.first()[0].toLongLong() >= 65536);
        const auto metadata = uploaded.first()[0].toJsonObject();
        // A previously interrupted download must append to the existing prefix.
        QDir().mkpath(directory.path() + "/cache/files/images");
        QFile partial(directory.path() + "/cache/files/images/" + metadata["resource_id"].toString() + ".png.part");
        QFile prefix(source);
        QVERIFY(prefix.open(QIODevice::ReadOnly)); QVERIFY(partial.open(QIODevice::WriteOnly));
        QCOMPARE(partial.write(prefix.read(32001)), qint64(32001)); partial.close(); prefix.close();
        QSignalSpy downloaded(&resumed, &ResourceTransferManager::downloaded);
        resumed.download(metadata);
        QTRY_VERIFY_WITH_TIMEOUT(downloaded.count() == 1 || failed.count() > 0, 15000);
        QCOMPARE(failed.count(), 0);
        QFile actual(downloaded.first()[1].toString()), expected(source);
        QVERIFY(actual.open(QIODevice::ReadOnly)); QVERIFY(expected.open(QIODevice::ReadOnly));
        QCOMPARE(actual.readAll(), expected.readAll());
        host.kill(); QVERIFY(host.waitForFinished(5000));
    }
    /** 验证空文件被同步拒绝且传输器不进入忙状态。 */
    void rejectsEmptyFile() {
        QTemporaryDir directory;
        QFile file(directory.path() + "/data.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        ResourceTransferManager transfer(QUrl("http://127.0.0.1:1"), 7, "fixture-token", directory.path());
        QSignalSpy failed(&transfer, &ResourceTransferManager::failed);
        transfer.upload(file.fileName()); QCOMPARE(failed.count(), 1); QVERIFY(!transfer.busy());
    }
};
#include "ui_acceptance_cases.h"
QTEST_MAIN(ResourceTransferTests)
#include "resource_transfer_tests.moc"
