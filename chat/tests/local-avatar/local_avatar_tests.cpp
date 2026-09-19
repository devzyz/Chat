#include "editavatardialog.h"
#include "avatarcrop.h"
#include "avatarcropwidget.h"
#include "localavatar.h"
#include "messagelistmodel.h"
#include "userstoragepaths.h"
#include <QCryptographicHash>

#include <QFile>
#include <QDirIterator>
#include <QSlider>
#include <QWheelEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QImage solidImage(Qt::GlobalColor color, QSize size = QSize(256, 256))
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
}

class LocalAvatarTests : public QObject
{
    Q_OBJECT
private slots:
    void readsFullPngAndJpeg();
    void rejectsInvalidAndOversizedImages();
    void persistsAndIsolatesAccounts();
    void saveFailurePreservesPreviousImage();
    void refreshesOnlyMatchingSender();
    void savesAndRestoresThroughController();
    void cancellationAndResetDiscardPendingResults();
    void dialogConfirmsAndCancels();
    void cropGeometryAndSquareOutput();
    void cropWidgetDragsAndZooms();
};

void LocalAvatarTests::readsFullPngAndJpeg()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source = solidImage(Qt::red, QSize(600, 200));
    for (int y = 0; y < 200; ++y) {
        for (int x = 200; x < 400; ++x) {
            source.setPixelColor(x, y, Qt::green);
        }
    }
    for (const auto &format : {QByteArray("PNG"), QByteArray("JPG")}) {
        const QString file = dir.filePath(QString::fromLatin1(format));
        QVERIFY(source.save(file, format.constData()));
        const auto result = LocalAvatarStore::readImage(file);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.image.size(), source.size());
        const QColor center = result.image.pixelColor(300, 100);
        QVERIFY(center.green() > 240);
        QVERIFY(center.red() < 15);
        QVERIFY(result.image.pixelColor(20, 20).red() > 240);
    }
}

void LocalAvatarTests::rejectsInvalidAndOversizedImages()
{
    QTemporaryDir dir;
    QVERIFY(!LocalAvatarStore::readImage(dir.filePath("missing.png")).error.isEmpty());
    const QString fileName = dir.filePath("invalid.png");
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a picture");
    file.close();
    QVERIFY(!LocalAvatarStore::readImage(fileName).error.isEmpty());
    QVERIFY(solidImage(Qt::red).save(fileName, "BMP"));
    QVERIFY(!LocalAvatarStore::readImage(fileName).error.isEmpty());
    QVERIFY(solidImage(Qt::red, QSize(4097, 1)).save(fileName, "PNG"));
    QVERIFY(!LocalAvatarStore::readImage(fileName).error.isEmpty());
    QVERIFY(solidImage(Qt::red, QSize(4096, 1)).save(fileName, "PNG"));
    QVERIFY(LocalAvatarStore::readImage(fileName).error.isEmpty());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(5 * 1024 * 1024));
    file.close();
    QVERIFY(LocalAvatarStore::readImage(fileName).error.isEmpty());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(5 * 1024 * 1024 + 1));
    file.close();
    QVERIFY(!LocalAvatarStore::readImage(fileName).error.isEmpty());
    // A valid PNG header alone does not make the image decodable.
    QVERIFY(solidImage(Qt::blue).save(fileName, "PNG"));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(40));
    file.close();
    QVERIFY(!LocalAvatarStore::readImage(fileName).error.isEmpty());
}

void LocalAvatarTests::persistsAndIsolatesAccounts()
{
    QTemporaryDir dir;
    LocalAvatarStore store(dir.path());
    QVERIFY(store.load("server-a", 1).image.isNull());
    const QString source = dir.filePath("source.png");
    QVERIFY(solidImage(Qt::red).save(source));
    const auto prepared = LocalAvatarStore::readImage(source);
    QVERIFY(store.save("server-a", 1, prepared.image).isEmpty());
    QVERIFY(QFile::remove(source));
    LocalAvatarStore reopened(dir.path());
    QCOMPARE(reopened.load("server-a", 1).image.pixelColor(128, 128), QColor(Qt::red));
    QVERIFY(reopened.load("server-a", 2).image.isNull());
    QVERIFY(reopened.load("server-b", 1).image.isNull());
    QVERIFY(reopened.save("server-a", 2, solidImage(Qt::blue)).isEmpty());
    QVERIFY(reopened.save("server-b", 1, solidImage(Qt::green)).isEmpty());
    QCOMPARE(reopened.load("server-a", 1).image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(reopened.load("server-a", 2).image.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(reopened.load("server-b", 1).image.pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(reopened.path("server-a", 1).endsWith("/users/1/static/head/1/current.png"));
    QVERIFY(UserStoragePaths::dataRoot().startsWith(QCoreApplication::applicationDirPath() + "/"));
    QVERIFY(UserStoragePaths::accountRoot(dir.path(), "server", 0).isEmpty());
    QCOMPARE(UserStoragePaths::environmentKey("http://localhost:8080/"),
             UserStoragePaths::environmentKey("http://localhost:8080"));
    const auto hash = QCryptographicHash::hash(QByteArray("legacy-server"), QCryptographicHash::Sha256).toHex();
    const auto legacy = dir.filePath("legacy/avatars/v1/" + QString::fromLatin1(hash) + "/1.png");
    QVERIFY(QDir().mkpath(QFileInfo(legacy).absolutePath()));
    QVERIFY(solidImage(Qt::yellow).save(legacy));
    LocalAvatarStore migrating(dir.filePath("installation/data"), dir.filePath("legacy"));
    QCOMPARE(migrating.load("legacy-server", 1).image.pixelColor(0, 0), QColor(Qt::yellow));
    QVERIFY(QFileInfo::exists(migrating.path("legacy-server", 1)));
    QVERIFY(QFileInfo::exists(legacy));
    QVERIFY(migrating.save("legacy-server", 1, solidImage(Qt::blue)).isEmpty());
    QCOMPARE(migrating.load("legacy-server", 1).image.pixelColor(0, 0), QColor(Qt::blue));
}

void LocalAvatarTests::saveFailurePreservesPreviousImage()
{
    QTemporaryDir dir;
    LocalAvatarStore store(dir.path());
    QVERIFY(store.save("server", 1, solidImage(Qt::red)).isEmpty());
    QVERIFY(!store.save("server", 1, {}).isEmpty());
    QCOMPARE(store.load("server", 1).image.pixelColor(0, 0), QColor(Qt::red));
    const QString blockedRoot = dir.filePath("file-instead-of-directory");
    QFile blocker(blockedRoot);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!LocalAvatarStore(blockedRoot).save("server", 1, solidImage(Qt::blue)).isEmpty());
    // QSaveFile must not truncate the existing PNG when the replacement cannot be opened.
    const QString stored = store.path("server", 1);
    QFile locked(stored);
    const auto originalPermissions = locked.permissions();
    QVERIFY(locked.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));
    const QString error = store.save("server", 1, solidImage(Qt::blue));
    QVERIFY(locked.setPermissions(originalPermissions));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.load("server", 1).image.pixelColor(0, 0), QColor(Qt::red));
}

void LocalAvatarTests::refreshesOnlyMatchingSender()
{
    MessageListModel model(7);
    MessageRecord self;
    self.messageId = 1;
    self.chatId = 7;
    self.senderId = 10;
    self.avatarKey = ":/server-avatar.png";
    MessageRecord other = self;
    other.messageId = 2;
    other.senderId = 20;
    model.appendMessages({self, other});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.updateSenderAvatar(10, QPixmap::fromImage(solidImage(Qt::green)));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.recordAt(0)->avatar.toImage().pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(model.recordAt(1)->avatar.isNull());
    QCOMPARE(model.recordAt(0)->avatarKey, self.avatarKey);
    QCOMPARE(model.recordAt(0)->messageId, qint64(1));
    model.updateSenderAvatar(30, {});
    QCOMPARE(changed.count(), 1);
}

void LocalAvatarTests::savesAndRestoresThroughController()
{
    QTemporaryDir dir;
    const QString source = dir.filePath("source.png");
    QVERIFY(solidImage(Qt::green).save(source));
    LocalAvatar avatar(dir.path());
    avatar.setAccount("server", 1);
    QTRY_VERIFY(!avatar.isBusy());
    avatar.selectFile(source);
    QTRY_VERIFY(!avatar.isBusy());
    QVERIFY(avatar.image().isNull());
    QCOMPARE(avatar.selection().size(), QSize(256, 256));
    QSignalSpy saved(&avatar, &LocalAvatar::saved);
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(avatar.image().pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(solidImage(Qt::blue).save(source));
    avatar.selectFile(source);
    QTRY_VERIFY(!avatar.isBusy());
    QFile stored(LocalAvatarStore(dir.path()).path("server", 1));
    const auto permissions = stored.permissions();
    QVERIFY(stored.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));
    QSignalSpy errors(&avatar, &LocalAvatar::errorOccurred);
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    QTRY_VERIFY(!avatar.isBusy());
    QVERIFY(stored.setPermissions(permissions));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(saved.count(), 1);
    QCOMPARE(avatar.image().pixelColor(0, 0), QColor(Qt::green));
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    QTRY_COMPARE(saved.count(), 2);
    QVERIFY(QFile::remove(source));
    avatar.setAccount("server", 2);
    QTRY_VERIFY(!avatar.isBusy());
    QVERIFY(avatar.image().isNull());
    LocalAvatar restored(dir.path());
    restored.setAccount("server", 1);
    QTRY_VERIFY(!restored.isBusy());
    QCOMPARE(restored.image().pixelColor(0, 0), QColor(Qt::blue));
}

void LocalAvatarTests::cancellationAndResetDiscardPendingResults()
{
    QTemporaryDir dir;
    const QString source = dir.filePath("source.png");
    QVERIFY(solidImage(Qt::blue).save(source));
    LocalAvatar avatar(dir.path());
    avatar.setAccount("server", 1);
    QTRY_VERIFY(!avatar.isBusy());
    avatar.selectFile(source);
    avatar.discardSelection();
    // The serial worker's next load is a deterministic barrier for the cancelled preview.
    avatar.setAccount("server", 2);
    QTRY_VERIFY(!avatar.isBusy());
    QVERIFY(avatar.selection().isNull());
    QVERIFY(avatar.image().isNull());
    avatar.selectFile(source);
    QTRY_VERIFY(!avatar.isBusy());
    avatar.selectFile({});
    QVERIFY(!avatar.selection().isNull());
    avatar.discardSelection();
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    QVERIFY(LocalAvatarStore(dir.path()).load("server", 2).image.isNull());
    avatar.selectFile(source);
    QTRY_VERIFY(!avatar.isBusy());
    avatar.saveSelection(QRectF(avatar.selection().rect()));
    avatar.reset();
    QSignalSpy changed(&avatar, &LocalAvatar::imageChanged);
    avatar.setAccount("server", 3);
    QTRY_VERIFY(!avatar.isBusy());
    QVERIFY(avatar.image().isNull());
    for (const auto &args : changed) {
        QVERIFY(qvariant_cast<QImage>(args.at(0)).isNull());
    }
}

void LocalAvatarTests::dialogConfirmsAndCancels()
{
    QTemporaryDir dir;
    LocalAvatar avatar(dir.path());
    avatar.setAccount("server", 1);
    QTRY_VERIFY(!avatar.isBusy());
    const QString source = dir.filePath("source.png");
    QImage sourceImage = solidImage(Qt::green, QSize(600, 200));
    for (int y = 0; y < sourceImage.height(); ++y) {
        for (int x = 400; x < sourceImage.width(); ++x) {
            sourceImage.setPixelColor(x, y, Qt::blue);
        }
    }
    QVERIFY(sourceImage.save(source));
    EditAvatarDialog dialog(&avatar, QPixmap::fromImage(solidImage(Qt::red)));
    auto *confirm = dialog.findChild<QPushButton *>("edit_avatar_confirm_btn");
    auto *cancel = dialog.findChild<QPushButton *>("edit_avatar_cancel_btn");
    auto *preview = dialog.findChild<AvatarCropWidget *>("avatar_crop_widget");
    QVERIFY(confirm && cancel && preview);
    QVERIFY(!confirm->isEnabled());
    dialog.show();
    avatar.selectFile(source);
    QTRY_VERIFY(confirm->isEnabled());
    QCOMPARE(preview->sourceRect(), QRectF(200, 0, 200, 200));
    cancel->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(avatar.selection().isNull());
    QVERIFY(LocalAvatarStore(dir.path()).load("server", 1).image.isNull());
    dialog.show();
    avatar.selectFile(source);
    QTRY_VERIFY(confirm->isEnabled());
    auto *zoom = dialog.findChild<QSlider *>("avatarZoomSlider");
    QVERIFY(zoom);
    zoom->setValue(200);
    QCOMPARE(preview->sourceRect(), QRectF(250, 50, 100, 100));
    const QPoint center = preview->rect().center();
    QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, center);
    const QPointF destination = center - QPoint(2000, 0);
    QMouseEvent move(QEvent::MouseMove, destination, preview->mapToGlobal(destination.toPoint()),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(preview, &move);
    QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(preview->sourceRect(), QRectF(500, 50, 100, 100));
    // Removing the source proves confirmation uses the edited in-memory image.
    QVERIFY(QFile::remove(source));
    confirm->click();
    QVERIFY(!confirm->isEnabled());
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    const QImage stored = LocalAvatarStore(dir.path()).load("server", 1).image;
    QCOMPARE(stored.size(), QSize(256, 256));
    QCOMPARE(stored.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(stored.pixelColor(255, 255), QColor(Qt::blue));
    QDirIterator files(dir.path(), QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (files.hasNext()) {
        files.next();
        ++count;
    }
    QCOMPARE(count, 1);
}

void LocalAvatarTests::cropGeometryAndSquareOutput()
{
    AvatarCrop crop;
    QVERIFY(crop.sourceRect().isEmpty());
    crop.setImageSize(QSize(600, 200));
    QCOMPARE(crop.sourceRect(), QRectF(200, 0, 200, 200));
    crop.setZoom(200);
    QCOMPARE(crop.sourceRect(), QRectF(250, 50, 100, 100));
    crop.moveBy(QPointF(1000, 1000));
    QCOMPARE(crop.sourceRect(), QRectF(500, 100, 100, 100));
    crop.setZoom(50);
    QCOMPARE(crop.zoom(), 100);
    QCOMPARE(crop.sourceRect(), QRectF(400, 0, 200, 200));
    crop.moveBy(QPointF(-1000, -1000));
    QCOMPARE(crop.sourceRect(), QRectF(0, 0, 200, 200));
    crop.setZoom(500);
    QCOMPARE(crop.zoom(), 400);
    QCOMPARE(crop.sourceRect().size(), QSizeF(50, 50));
    crop.setImageSize(QSize(200, 600));
    QCOMPARE(crop.zoom(), 100);
    QCOMPARE(crop.sourceRect(), QRectF(0, 200, 200, 200));

    QImage source = solidImage(Qt::red, QSize(200, 600));
    for (int y = 200; y < 400; ++y) {
        for (int x = 0; x < 200; ++x) {
            source.setPixelColor(x, y, Qt::green);
        }
    }
    const QImage square = AvatarCrop::render(source, crop.sourceRect());
    QCOMPARE(square.size(), QSize(256, 256));
    QCOMPARE(square.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(square.pixelColor(255, 255), QColor(Qt::green));
    const QImage round = AvatarCrop::circularPreview(square);
    QCOMPARE(round.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(round.pixelColor(128, 128), QColor(Qt::green));
    QCOMPARE(square.pixelColor(0, 0).alpha(), 255);
    QVERIFY(AvatarCrop::render(source, QRectF(-1, 0, 200, 200)).isNull());
}

void LocalAvatarTests::cropWidgetDragsAndZooms()
{
    AvatarCropWidget widget;
    widget.resize(400, 400);
    widget.setImage(solidImage(Qt::green, QSize(600, 200)));
    widget.show();
    QSignalSpy zoomChanged(&widget, &AvatarCropWidget::zoomChanged);
    widget.setZoom(200);
    QCOMPARE(zoomChanged.count(), 1);
    QCOMPARE(widget.sourceRect(), QRectF(250, 50, 100, 100));
    const QPoint center = widget.rect().center();
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, center);
    const QPointF destination = QPointF(center) + QPointF(widget.frameRect().width() / 2, 0);
    QMouseEvent move(QEvent::MouseMove, destination, widget.mapToGlobal(destination.toPoint()),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &move);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(widget.sourceRect(), QRectF(200, 50, 100, 100));
    QWheelEvent wheel(center, widget.mapToGlobal(center), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&widget, &wheel);
    QCOMPARE(widget.zoom(), 210);
    widget.setZoom(100);
    const QImage display = widget.grab().toImage();
    const QColor inside = display.pixelColor(center);
    const QColor outside = display.pixelColor(2, 2);
    QVERIFY(inside.green() > 240);
    QVERIFY(outside.green() < inside.green());
    widget.setImage(solidImage(Qt::red, QSize(200, 600)));
    QCOMPARE(widget.zoom(), 100);
    QCOMPARE(widget.sourceRect(), QRectF(0, 200, 200, 200));
}

QTEST_MAIN(LocalAvatarTests)
#include "local_avatar_tests.moc"
