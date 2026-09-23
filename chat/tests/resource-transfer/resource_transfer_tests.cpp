#include <QDir>
#include "resourcetransfermanager.h"
#include "messagelistmodel.h"
#include "chatpage.h"
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
        connect(&sender, &AvatarCache::published, &editor, [&editor] { editor.finishUpload(); });
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
        connect(&editor, &LocalAvatar::uploadRequested, &editor, [&editor](const QString &) {
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
        UserMgr::GetInstance()->setUserInfo(std::make_shared<UserInfo>(8, "receiver", ""));
        UserMgr::GetInstance()->setToken("fixture-token");
        ChatPage page;
        QCOMPARE(page.findChildren<ResourceTransferManager*>().size(), 1);
        page.resize(600, 400);
        page.show();
        QCoreApplication::processEvents();
        page.resize(700, 500);
        QCoreApplication::processEvents();
        QCOMPARE(page.findChildren<ResourceTransferManager*>().size(), 1);
        page.SetChatInfo(std::make_shared<ChatInfo>(7, 1, 0));
        QJsonObject descriptor{{"resource_id", "incoming-resource"}, {"media_type", "image/png"}, {"name", "image.png"}};
        auto message = std::make_shared<TextChatData>("incoming-uuid", 1, ChatType::PRIVATE,
            ChatMessageType::TEXT_TYPE,
            "@resource:v1:" + QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact)),
            7, QTime::currentTime());
        page.AppendChatMsg(message);
        auto* model = page.findChild<ChatDetailList*>()->model();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(MessageListModel::ResourceIdRole).toString(), QString("incoming-resource"));
        QCOMPARE(model->index(0, 0).data(MessageListModel::MessageTypeRole).toInt(), int(MessageType::Image));
        // Destruction cancels pending attachment work before its widgets are released.
    }
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
            connect(&first, &ResourceTransferManager::progress, &first, [&first](qint64 offset, qint64) {
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
    void rejectsEmptyFile() {
        QTemporaryDir directory;
        QFile file(directory.path() + "/data.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        ResourceTransferManager transfer(QUrl("http://127.0.0.1:1"), 7, "fixture-token", directory.path());
        QSignalSpy failed(&transfer, &ResourceTransferManager::failed);
        transfer.upload(file.fileName()); QCOMPARE(failed.count(), 1); QVERIFY(!transfer.busy());
    }
};
QTEST_MAIN(ResourceTransferTests)
#include "resource_transfer_tests.moc"
