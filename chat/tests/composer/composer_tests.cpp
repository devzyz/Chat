#include "messagetextedit.h"
#include "customizeedit.h"
#include "clickedlabel.h"
#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include "messagesubmissioncontroller.h"
#include "messageservice.h"
#include "resourcetransfermanager.h"
#include "usersearchcontroller.h"
#include "chattcptransport.h"
#include "statewidget.h"
#include "clickedoncelabel.h"
#include "timerbtn.h"
#include "submissionexitguard.h"
#include <QtConcurrentRun>
#include <QSemaphore>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QThreadPool>


/** @brief 验证真实编辑器及点击控件的用户输入合同。 */
class ComposerTests : public QObject {
    Q_OBJECT
private slots:
    /** @brief 切换会话保留各自原生文档和附件撤销记录，后台完成不污染当前输入。 */
    void conversationDrafts() {
        QTemporaryDir root;
        MessageTextEdit editor; editor.setAccountRoot(root.path());
        editor.setConversation(10);
        editor.insertPlainText("first");
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QApplication::clipboard()->setImage(image); editor.paste();
        auto *first = editor.document();
        editor.setConversation(20);
        QVERIFY(editor.toPlainText().isEmpty());
        editor.insertPlainText("second");
        auto *second = editor.document();
        editor.setConversation(10);
        QCOMPARE(editor.document(), first);
        QTRY_VERIFY(!editor.hasPendingAttachments());
        QCOMPARE(editor.draft().entries.size(), 2);
        editor.undo(); QCOMPARE(editor.toPlainText(), QString("first"));
        editor.redo(); QCOMPARE(editor.draft().entries.size(), 2);
        editor.clearAccepted(editor.draft().id);
        editor.setConversation(20);
        QCOMPARE(editor.document(), second); QCOMPARE(editor.toPlainText(), QString("second"));
        editor.undo(); QVERIFY(editor.toPlainText().isEmpty());
        editor.redo(); QCOMPARE(editor.toPlainText(), QString("second"));
        QApplication::clipboard()->clear();
    }
    /** @brief 自动控件回归不使用系统原生对话框，避免依赖桌面主题。 */
    void initTestCase() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setStyle("Fusion");
    }
    /** @brief 后台准备期间 UI 事件继续执行，提交拒绝保留原文；完成后附件位置不变。 */
    void preparationIsAsynchronous() {
        QTemporaryDir root;
        auto *pool = QThreadPool::globalInstance();
        const int previous = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run(/** @brief 暂停工作池以确定性验证 UI 不等待附件准备。 */
            [&] { entered.release(); release.acquire(); });
        entered.acquire();
        MessageTextEdit editor; editor.setAccountRoot(root.path());
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QApplication::clipboard()->setImage(image); editor.paste(); editor.insertPlainText("after");
        bool eventRan = false;
        QTimer::singleShot(0, &editor, /** @brief 附件尚未准备时确认 GUI 事件仍能处理。 */
            [&] { eventRan = true; });
        QCoreApplication::processEvents();
        const bool pending = editor.hasPendingAttachments();
        const auto draft = editor.draft();
        release.release(); blocker.waitForFinished(); pool->setMaxThreadCount(previous);
        QVERIFY(eventRan); QVERIFY(pending); QVERIFY(!draft.error.isEmpty());
        QTRY_VERIFY(!editor.hasPendingAttachments());
        QVERIFY(editor.draft().error.isEmpty()); QCOMPARE(editor.draft().entries.size(), 2);
        QCOMPARE(editor.draft().entries[1].content, QString("after"));
    }
    /** @brief 后台队列满载明确拒绝；无草稿引用的排队图片不再写入临时文件。 */
    void preparationCapacity() {
        QTemporaryDir root;
        auto *pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int previous = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run(/** @brief 阻塞工作池，让容量与取消检查不依赖执行速度。 */
            [&] { entered.release(); release.acquire(); });
        entered.acquire();
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QVector<std::shared_ptr<DraftAttachment>> queued;
        for (int i = 0; i < 33; ++i) queued.push_back(prepareDraftAttachment({}, image, root.path()));
        const auto last = queued.last()->future;
        const bool rejected = last.isFinished() && !last.result().error.isEmpty();
        queued.clear();
        release.release(); blocker.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(previous);
        QVERIFY(rejected);
        QVERIFY(QDir(root.filePath("transfers/drafts")).entryList(QDir::Files).isEmpty());
    }
    /** @brief 撤销后建立新分支时，已不可恢复的图片释放临时文件。 */
    void discardedAttachment() {
        QTemporaryDir root;
        MessageTextEdit editor; editor.setAccountRoot(root.path());
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QApplication::clipboard()->setImage(image); editor.paste();
        QTRY_VERIFY(!editor.hasPendingAttachments());
        const QString path = editor.draft().entries.first().content;
        editor.undo(); editor.insertPlainText("replacement");
        QVERIFY(!editor.document()->isRedoAvailable());
        QVERIFY(!QFileInfo::exists(path));
    }
    /** @brief 附件剪切粘贴后仍按附件提交，剪贴板持有临时文件直到被替换。 */
    void attachmentClipboard() {
        QTemporaryDir root;
        MessageTextEdit editor; editor.setAccountRoot(root.path());
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QApplication::clipboard()->setImage(image); editor.paste();
        QTRY_VERIFY(!editor.hasPendingAttachments());
        const auto original = editor.draft();
        editor.selectAll(); editor.cut(); editor.paste();
        const auto copied = editor.draft();
        QCOMPARE(copied.entries.size(), 1);
        QCOMPARE(copied.entries.first().kind, DraftEntry::Kind::Attachment);
        QCOMPARE(copied.entries.first().content, original.entries.first().content);
        QApplication::clipboard()->clear();
    }
    /** @brief 草稿读取无副作用，长 Unicode 文本按真实 JSON 预算无损重组。 */
    void byteBudget() {
        const QString text = QString::fromUtf8("汉字😄\n\"\\").repeated(1500);
        const QJsonObject context{{"from_uid", 7}, {"to_uid", 0}, {"chat_id", 12},
            {"chat_type", "group"}, {"membership_epoch", "9223372036854775807"}};
        const auto requests = MessageSubmissionController::textRequests(text, context);
        QVERIFY(requests.size() > 1);
        QString combined;
        QSet<QString> ids;
        for (auto request : requests) {
            QVERIFY(QJsonDocument(request).toJson(QJsonDocument::Compact).size() <= 1950);
            const auto message = request["text_array"].toArray().first().toObject();
            const auto chunk = message["msg_content"].toString();
            QVERIFY(!chunk.front().isLowSurrogate()); QVERIFY(!chunk.back().isHighSurrogate());
            combined += chunk;
            QVERIFY(!ids.contains(message["msg_uuid"].toString())); ids.insert(message["msg_uuid"].toString());
            request["attempt_id"] = "9223372036854775807";
            QVERIFY(QJsonDocument(request).toJson(QJsonDocument::Compact).size() <= ChatTcpTransport::maxBodyBytes());
        }
        QCOMPARE(combined, text);
    }
    /** @brief 附件按文档身份读取，删除及撤销重做不改变文件路径。 */
    void attachmentDraft() {
        QTemporaryDir root;
        const auto path = root.filePath("some_file name.png");
        QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(path));
        MessageTextEdit editor; editor.setAccountRoot(root.path());
        editor.insertPlainText("before");
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)}); QApplication::clipboard()->setMimeData(mime);
        editor.paste(); editor.insertPlainText("after");
        QTRY_VERIFY(!editor.hasPendingAttachments());
        const auto draft = editor.draft();
        QCOMPARE(draft.entries.size(), 3); QCOMPARE(draft.entries[1].content, path);
        QCOMPARE(editor.draft().id, draft.id); QCOMPARE(editor.draft().entries.size(), 3);
        auto cursor = editor.textCursor(); cursor.setPosition(6); cursor.setPosition(7, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QCOMPARE(editor.draft().entries.size(), 1);
        editor.undo(); QCOMPARE(editor.draft().entries[1].content, path);
        editor.redo(); QCOMPARE(editor.draft().entries.size(), 1);
        editor.undo();
        editor.clearAccepted(draft.id); QVERIFY(!editor.toPlainText().isEmpty());
        editor.clearAccepted(editor.draft().id); QVERIFY(editor.draft().entries.isEmpty());
    }
    /** @brief 普通文件草稿显示带文件名的卡片，跨会话和撤销后仍可识别。 */
    void fileAttachmentPreview() {
        QTemporaryDir root;
        const auto path = root.filePath(QString::fromUtf8("测试-file.txt"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        MessageTextEdit editor; editor.setAccountRoot(root.path()); editor.setConversation(10);
        QVERIFY(editor.addAttachment(path)); QTRY_VERIFY(!editor.hasPendingAttachments());
        QCoreApplication::processEvents();
        const auto format = editor.document()->begin().begin().fragment().charFormat().toImageFormat();
        QVERIFY(format.toolTip().contains(QFileInfo(path).fileName()));
        const auto preview = qvariant_cast<QImage>(editor.document()->resource(QTextDocument::ImageResource, QUrl(format.name())));
        QVERIFY(!preview.isNull());
        bool hasDetails = false;
        for (int y = 0; y < preview.height(); ++y)
            for (int x = 0; x < preview.width(); ++x)
                hasDetails |= preview.pixel(x, y) != preview.pixel(0, 0);
        QVERIFY2(hasDetails, "file preview cannot be a flat gray block");
        editor.setConversation(20); editor.setConversation(10);
        editor.undo(); editor.redo();
        QCOMPARE(editor.draft().entries.first().content, path);
        QVERIFY(editor.document()->begin().begin().fragment().charFormat().toolTip().contains(QFileInfo(path).fileName()));
    }
    /** @brief 剪贴板图片暂存文件由草稿及任务共享持有。 */
    void clipboardLifetime() {
        QTemporaryDir root;
        MessageDraft snapshot;
        QString path;
        {
            MessageTextEdit editor; editor.setAccountRoot(root.path());
            QImage image(20, 20, QImage::Format_RGB32); image.fill(Qt::blue);
            QApplication::clipboard()->setImage(image); editor.paste();
        QTRY_VERIFY(!editor.hasPendingAttachments());
            snapshot = editor.draft(); QCOMPARE(snapshot.entries.size(), 1);
            path = snapshot.entries.first().content; QVERIFY(QFileInfo::exists(path));
            editor.clearAccepted(snapshot.id); QVERIFY(QFileInfo::exists(path));
        }
        QVERIFY(QFileInfo::exists(path)); snapshot = {}; QVERIFY(!QFileInfo::exists(path));
    }
    /** @brief 文本拒绝不清空，接受后每段只落盘一次且固定会话。 */
    void submissionPersistence() {
        QTemporaryDir root;
        MessageService service;
        ResourceTransferManager uploads(QUrl("http://127.0.0.1:1"), 7, "fixture", root.path());
        MessageSubmissionController controller(&service, &uploads, 7);
        MessageTextEdit editor; const QString text = QString::fromUtf8("长文本😄").repeated(600);
        editor.setPlainText(text); const auto draft = editor.draft();
        QVERIFY(!controller.submit(draft, 12, 8, false)); QCOMPARE(editor.toPlainText(), text);
        QSignalSpy restored(&service, &MessageService::directoryRestored);
        service.start(root.path(), 7); QTRY_COMPARE(restored.size(), 1);
        QSignalSpy persisted(&service, &MessageService::outgoingPersisted);
        QVERIFY(controller.submit(draft, 12, 8, false));
        QVERIFY(!controller.submit(draft, 13, 9, false));
        editor.clearAccepted(draft.id); QVERIFY(editor.toPlainText().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPending(), 5000);
        const auto expected = MessageSubmissionController::textRequests(text, {{"chat_id",12},{"to_uid",8},{"from_uid",7}});
        QCOMPARE(persisted.size(), expected.size());
        for (const auto &args : persisted) { QCOMPARE(args[0].toInt(), 12); QVERIFY(args[2].toBool()); }
        QVERIFY(!controller.submit(draft, 12, 8, false));
        QSignalSpy history(&service, &MessageService::historyLoaded);
        service.loadHistory(12); QTRY_COMPARE(history.size(), 1);
        const auto rows = qvariant_cast<QVector<StoredMessage>>(history.first()[2]);
        QString combined; for (const auto &row : rows) combined += row.content;
        QCOMPARE(combined, text);
    }
    /** @brief 本地开库失败保留 UUID，修复可写路径后仅重试未落盘部分。 */
    void storageFailureRetry() {
        QTemporaryDir root;
        QFile blocker(root.filePath("blocked")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        MessageService service;
        ResourceTransferManager uploads(QUrl("http://127.0.0.1:1"), 7, "fixture", root.path());
        MessageSubmissionController controller(&service, &uploads, 7);
        QSignalSpy persisted(&service, &MessageService::outgoingPersisted);
        service.start(blocker.fileName(), 7);
        QVERIFY(controller.submit({"draft", {{DraftEntry::Kind::Text, {}, "retained", {}}}}, 12, 8, false));
        QTRY_VERIFY(controller.isFailed()); QVERIFY(controller.hasPending());
        QCOMPARE(persisted.size(), 1); const auto ids = persisted.first()[1];
        bool sawWarning = false;
        QTimer::singleShot(0, &controller, /** @brief 在真实退出提示中选择 Cancel。 */ [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    sawWarning = true; box->done(QMessageBox::Cancel);
                }
            }
        });
        QVERIFY(!confirmSubmissionExit(nullptr, controller.hasPending()));
        QVERIFY(sawWarning); QVERIFY(controller.hasPending()); QVERIFY(controller.isFailed());
        QVERIFY(blocker.remove()); controller.retry();
        QTRY_VERIFY(!controller.hasPending()); QCOMPARE(persisted.size(), 2);
        QCOMPARE(persisted.last()[1], ids); QVERIFY(persisted.last()[2].toBool());
    }
    /** @brief 取消写盘失败的提交后，旧消息服务重试入口不能找回已取消内容。 */
    void storageFailureCancel() {
        QTemporaryDir root;
        QFile blocker(root.filePath("blocked")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        MessageService service;
        ResourceTransferManager uploads(QUrl("http://127.0.0.1:1"), 7, "fixture", root.path());
        MessageSubmissionController controller(&service, &uploads, 7);
        QSignalSpy persisted(&service, &MessageService::outgoingPersisted);
        service.start(blocker.fileName(), 7);
        QVERIFY(controller.submit({"cancel-draft", {{DraftEntry::Kind::Text, {}, "cancelled", {}}}}, 12, 8, false));
        QTRY_VERIFY(controller.isFailed());
        const auto ids = qvariant_cast<QVector<QString>>(persisted.first()[1]);
        controller.cancel(); QVERIFY(!controller.hasPending());
        QVERIFY(blocker.remove());
        QVERIFY(controller.submit({"next-draft", {{DraftEntry::Kind::Text, {}, "next", {}}}}, 12, 8, false));
        QTRY_VERIFY(!controller.hasPending()); QCOMPARE(persisted.size(), 2);
        service.retry(12, ids.first());
        QSignalSpy history(&service, &MessageService::historyLoaded);
        service.loadHistory(12); QTRY_COMPARE(history.size(), 1);
        QCOMPARE(persisted.size(), 2);
        const auto rows = qvariant_cast<QVector<StoredMessage>>(history.first()[2]);
        QCOMPARE(rows.size(), 1); QCOMPARE(rows.first().content, QString("next"));
    }
    /** @brief 上传失败暂停后续文本，取消和账号停止隔离迟到上传结果。 */
    void uploadFailureCancel() {
        QTemporaryDir root; const auto path = root.filePath("file.bin");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("content"); file.close();
        MessageService service; service.start(root.path(), 7);
        ResourceTransferManager uploads(QUrl("http://127.0.0.1:1"), 7, "fixture", root.path());
        MessageSubmissionController controller(&service, &uploads, 7);
        QSignalSpy saved(&service, &MessageService::outgoingPersisted);
        QVERIFY(controller.submit({"draft", {{DraftEntry::Kind::Text, {}, "first", {}},
            {DraftEntry::Kind::Attachment, {}, path, {}}, {DraftEntry::Kind::Text, {}, "last", {}}}},12,8,false));
        QTRY_VERIFY(controller.isFailed()); QCOMPARE(saved.size(), 1);
        controller.retry(); QTRY_VERIFY(controller.isFailed()); QCOMPARE(saved.size(), 1);
        controller.cancel(); QVERIFY(!controller.hasPending());
        emit uploads.uploaded({{"resource_id","late"}}); QCOMPARE(saved.size(), 1);
        QVERIFY(controller.submit({"second", {{DraftEntry::Kind::Text, {}, "old account", {}}}},12,8,false));
        // 确实排入旧账号 SQLite 操作，但不处理其 GUI 完成回调即切换账号。
        const auto queuedOld = MessageSubmissionController::textRequests("old queued write",
            {{"from_uid", 7}, {"to_uid", 8}, {"chat_id", 12}}).first();
        service.send(queuedOld);
        service.stop(); QVERIFY(!controller.hasPending());
        QCoreApplication::processEvents(); QCOMPARE(saved.size(), 1);
        QTemporaryDir nextRoot;
        QSignalSpy restored(&service, &MessageService::directoryRestored);
        service.start(nextRoot.path(), 8); QTRY_COMPARE(restored.size(), 1);
        ResourceTransferManager nextUploads(QUrl("http://127.0.0.1:1"), 8, "fixture", nextRoot.path());
        MessageSubmissionController next(&service, &nextUploads, 8);
        QVERIFY(next.submit({"new-account", {{DraftEntry::Kind::Text, {}, "new account", {}}}}, 14, 9, false));
        emit uploads.uploaded({{"resource_id", "late-old-account"}});
        emit uploads.failed("late-old-account");
        QTRY_VERIFY(!next.hasPending()); QCOMPARE(saved.size(), 2);
        QSignalSpy history(&service, &MessageService::historyLoaded);
        service.loadHistory(14); QTRY_COMPARE(history.size(), 1);
        const auto rows = qvariant_cast<QVector<StoredMessage>>(history.first()[2]);
        QCOMPARE(rows.size(), 1); QCOMPARE(rows.first().senderId, 8);
        QCOMPARE(rows.first().content, QString("new account"));
        service.stop();
        QTRY_VERIFY(!QFileInfo::exists(nextRoot.path() + "/messages.lock"));
    }
    /** @brief 搜索超时、旧响应、重复及无编号响应均有独立终态。 */
    void searchLifecycle() {
        UserSearchController search(nullptr, 20);
        QSignalSpy requests(&search, &UserSearchController::requestReady);
        QSignalSpy errors(&search, &UserSearchController::failed);
        QSignalSpy found(&search, &UserSearchController::found);
        search.search("first"); QTRY_COMPARE(errors.size(), 1);
        const auto old = requests.first()[0].toJsonObject().value("request_id");
        search.search("second");
        search.accept({{"request_id",old},{"error",0},{"uid",8},{"name","old"}}); QVERIFY(found.isEmpty());
        auto current = requests.last()[0].toJsonObject().value("request_id");
        QJsonObject response{{"request_id",current},{"error",0},{"uid",9},{"name","new"}};
        search.accept(response); QCOMPARE(found.size(), 1); search.accept(response); QCOMPARE(found.size(), 1);
        search.search("third"); search.accept({{"error",0},{"uid",8},{"name","legacy"}}); QCOMPARE(errors.size(), 2);
        search.search("fourth"); search.disconnected(); QCOMPARE(errors.size(), 3);
        search.accept(response); QCOMPARE(found.size(), 1);
        search.search("fifth"); current = requests.last()[0].toJsonObject().value("request_id");
        search.accept({{"request_id",current},{"error",1}}); QCOMPARE(errors.size(), 4);
        search.search("sixth"); search.cancel(); search.accept({{"request_id",requests.last()[0].toJsonObject().value("request_id")},{"error",0},{"uid",8},{"name","late"}});
        QCOMPARE(found.size(), 1);
        auto *temporary = new UserSearchController(nullptr,20); temporary->search("dispose"); delete temporary;
    }
    /** @brief 生产 TCP 解帧把 loopback 回包的搜索身份交给控制器。 */
    void searchLoopback() {
        QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost,0));
        ChatTcpTransport transport; UserSearchController search;
        connect(&search, &UserSearchController::requestReady, &transport,
            /** @brief 测试使用真实协议帧传输查询 JSON。 */ [&transport](const QJsonObject &request) {
            transport.send(1007,QJsonDocument(request).toJson(QJsonDocument::Compact));
        });
        connect(&transport, &ChatTcpTransport::frameReceived, &search,
            /** @brief 回包通过生产解帧后核对查询身份。 */ [&search](const ChatTcpFrame &frame) {
            search.accept(QJsonDocument::fromJson(frame.body).object());
        });
        QSignalSpy connected(&transport,&ChatTcpTransport::connected), found(&search,&UserSearchController::found);
        transport.connectTo({"127.0.0.1",peer.serverPort(),1,1000,1000});
        QTRY_COMPARE(connected.size(),1); QTRY_VERIFY(peer.hasPendingConnections());
        auto *socket=peer.nextPendingConnection(); search.search("user");
        QByteArray bytes;
        const auto received = /** @brief 轮询读取隔离连接中的完整帧，不固定等待时长。 */ [&](qsizetype count) {
            bytes += socket->readAll(); return bytes.size() >= count;
        };
        QTRY_VERIFY(received(4));
        const auto size=(quint8(bytes[2])<<8)|quint8(bytes[3]);
        QTRY_VERIFY(received(4 + size));
        const auto request=QJsonDocument::fromJson(bytes.mid(4,size)).object();
        const auto body=QJsonDocument(QJsonObject{{"request_id",request["request_id"]},{"error",0},{"uid",8},{"name","user"}}).toJson(QJsonDocument::Compact);
        QByteArray wire; QDataStream stream(&wire,QIODevice::WriteOnly); stream.setByteOrder(QDataStream::BigEndian);
        stream<<quint16(1008)<<quint16(body.size()); wire+=body;
        socket->write(wire); socket->flush(); QTRY_COMPARE(found.size(),1); transport.reset();
    }
    /** @brief 点击包装器支持键盘，右键及拖出取消不启动倒计时。 */
    void keyboardControls() {
        ClickedLabel toggle; QSignalSpy clicks(&toggle,&ClickedLabel::clicked);
        QTest::keyClick(&toggle,Qt::Key_Space); QCOMPARE(clicks.size(),1); QCOMPARE(toggle.getCurState(),Selected);
        QTest::mouseClick(&toggle,Qt::RightButton); QCOMPARE(clicks.size(),1);
        StateWidget navigation; navigation.resize(80,30); QSignalSpy navigated(&navigation,&StateWidget::clicked);
        QTest::mousePress(&navigation,Qt::LeftButton,Qt::NoModifier,QPoint(10,10));
        QTest::mouseRelease(&navigation,Qt::LeftButton,Qt::NoModifier,QPoint(100,10));
        QCOMPARE(navigated.size(),0); QCOMPARE(navigation.getCurState(),Normal);
        QTest::keyClick(&navigation,Qt::Key_Return); QCOMPARE(navigated.size(),1);
        ClickedOnceLabel once; QSignalSpy activated(&once,&ClickedOnceLabel::clicked);
        QTest::keyClick(&once,Qt::Key_Space); QCOMPARE(activated.size(),1);
        TimerBtn timer; QSignalSpy timed(&timer,&QPushButton::clicked);
        QTest::keyClick(&timer,Qt::Key_Space); QCOMPARE(timed.size(),1); QVERIFY(!timer.isEnabled());
    }

    /** @brief 普通 Unicode 剪贴板内容必须完整进入编辑器。 */
    void pasteText() {
        MessageTextEdit editor;
        const QString text = QString::fromUtf8("你好 😄\nsecond line");
        QApplication::clipboard()->setText(text);
        editor.paste();
        QCOMPARE(editor.toPlainText(), text);
    }
    /** @brief 输入长度沿用 QLineEdit 的字符单位。 */
    void unicodeLength() {
        CustomizeEdit editor;
        editor.setMaxLength(15);
        editor.setText(QString::fromUtf8("一二三四五六"));
        QCOMPARE(editor.text(), QString::fromUtf8("一二三四五六"));
    }
    /** @brief 按住移出后释放不得发出点击或切换选中。 */
    void releaseOutside() {
        ClickedLabel label;
        label.resize(80, 30);
        QSignalSpy clicked(&label, &ClickedLabel::clicked);
        QTest::mousePress(&label, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseRelease(&label, Qt::LeftButton, Qt::NoModifier, QPoint(100, 10));
        QCOMPARE(clicked.size(), 0);
        QCOMPARE(label.getCurState(), ClickLabelState::Normal);
    }
};
QTEST_MAIN(ComposerTests)
#include "composer_tests.moc"
