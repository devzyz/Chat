#include "messageitemdelegate.h"
#include "messagelistmodel.h"
#include "messagemodelstore.h"

#include <QListView>
#include <QtTest>

namespace {
MessageRecord message(qint64 id, const QString &clientId,
                      const QString &text = QStringLiteral("text"))
{
    MessageRecord record;
    record.messageId = id;
    record.clientMessageId = clientId;
    record.chatId = 7;
    record.senderId = 1;
    record.senderName = QStringLiteral("test-user");
    record.sentAt = QDateTime::currentDateTime();
    record.deliveryStatus = id > 0 ? DeliveryStatus::Read : DeliveryStatus::Sending;
    record.isSelf = true;
    record.text = text;
    return record;
}
} // namespace

class MessageModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void appendAcknowledgeStatusAndRemovalKeepIndexesSynchronized();
    void unknownStableIdsDoNotMutateTheModel();
    void historyDeduplicatesAndRemainsChronological();
    void multipleHistoryPagesRemainChronological();
    void removalAndAcknowledgementRebuildShiftedIndexes();
    void textAndChatIdentityRoundTripWithoutNormalization();
    void storeRetainsOneModelAndPaginationStatePerChat();
    void delegateReflowsLongTextForANarrowViewport();
};

void MessageModelTests::appendAcknowledgeStatusAndRemovalKeepIndexesSynchronized()
{
    MessageListModel model(7);
    int inserted = 0;
    int changed = 0;
    int removed = 0;
    connect(&model, &QAbstractItemModel::rowsInserted, [&inserted]() { ++inserted; });
    connect(&model, &QAbstractItemModel::dataChanged, [&changed]() { ++changed; });
    connect(&model, &QAbstractItemModel::rowsRemoved, [&removed]() { ++removed; });

    QCOMPARE(model.appendMessage(message(0, QStringLiteral("uuid-1"))), 1);
    QCOMPARE(inserted, 1);
    QCOMPARE(model.rowForClientMessageId(QStringLiteral("uuid-1")), 0);
    QVERIFY(model.acknowledgeMessage(QStringLiteral("uuid-1"), 42,
                                     DeliveryStatus::Sent));
    QCOMPARE(model.rowForMessageId(42), 0);
    QCOMPARE(changed, 1);
    QVERIFY(model.updateStatusByMessageId(42, DeliveryStatus::Read));
    QCOMPARE(changed, 2);
    QVERIFY(model.removeByMessageId(42));
    QCOMPARE(removed, 1);
    QCOMPARE(model.rowCount(), 0);
}

void MessageModelTests::unknownStableIdsDoNotMutateTheModel()
{
    MessageListModel model(7);
    model.appendMessage(message(101, QStringLiteral("known-client")));
    int changes = 0;
    int removals = 0;
    connect(&model, &QAbstractItemModel::dataChanged, [&changes]() { ++changes; });
    connect(&model, &QAbstractItemModel::rowsRemoved, [&removals]() { ++removals; });

    QVERIFY(!model.acknowledgeMessage(QStringLiteral("missing-client"), 202,
                                      DeliveryStatus::Sent));
    QVERIFY(!model.updateStatusByClientId(QStringLiteral("missing-client"),
                                          DeliveryStatus::Failed));
    QVERIFY(!model.updateStatusByMessageId(202, DeliveryStatus::Failed));
    QVERIFY(!model.removeByMessageId(202));
    QCOMPARE(changes, 0);
    QCOMPARE(removals, 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowForMessageId(101), 0);
    QCOMPARE(model.data(model.index(0), MessageListModel::DeliveryStatusRole).toInt(),
             static_cast<int>(DeliveryStatus::Read));
}

void MessageModelTests::historyDeduplicatesAndRemainsChronological()
{
    MessageListModel model(7);
    model.appendMessage(message(30, {}));
    const QVector<MessageRecord> history = {
        message(20, {}, QStringLiteral("中文")),
        message(10, {}, QStringLiteral("English 😄\nmanual wrap")),
        message(20, {}),
        message(30, {})
    };

    QCOMPARE(model.prependHistory(history), 2);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), MessageListModel::MessageIdRole).toLongLong(), 10);
    QCOMPARE(model.data(model.index(1), MessageListModel::MessageIdRole).toLongLong(), 20);
    QCOMPARE(model.data(model.index(2), MessageListModel::MessageIdRole).toLongLong(), 30);
    QCOMPARE(model.data(model.index(0), MessageListModel::TextRole).toString(),
             QStringLiteral("English 😄\nmanual wrap"));
}

void MessageModelTests::multipleHistoryPagesRemainChronological()
{
    MessageListModel model(7);
    model.appendMessage(message(50, QStringLiteral("server-50")));
    QCOMPARE(model.prependHistory({
                 message(40, QStringLiteral("server-40")),
                 message(30, QStringLiteral("server-30"))
             }),
             2);
    QCOMPARE(model.prependHistory({
                 message(20, QStringLiteral("server-20")),
                 message(10, QStringLiteral("server-10")),
                 message(30, QStringLiteral("duplicate-message-id")),
                 message(15, QStringLiteral("server-20"))
             }),
             2);

    const QVector<qint64> expectedIds = {10, 20, 30, 40, 50};
    QCOMPARE(model.rowCount(), expectedIds.size());
    for (int row = 0; row < expectedIds.size(); ++row) {
        QCOMPARE(model.data(model.index(row), MessageListModel::MessageIdRole).toLongLong(),
                 expectedIds.at(row));
    }
    QCOMPARE(model.oldestMessageId(), 10);
}

void MessageModelTests::removalAndAcknowledgementRebuildShiftedIndexes()
{
    MessageListModel model(7);
    model.appendMessages({
        message(100, QStringLiteral("client-100")),
        message(200, QStringLiteral("client-200")),
        message(300, QStringLiteral("client-300"))
    });

    QVERIFY(model.removeByMessageId(100));
    QCOMPARE(model.rowForMessageId(100), -1);
    QCOMPARE(model.rowForClientMessageId(QStringLiteral("client-100")), -1);
    QCOMPARE(model.rowForMessageId(200), 0);
    QCOMPARE(model.rowForMessageId(300), 1);
    QVERIFY(model.acknowledgeMessage(QStringLiteral("client-200"), 250,
                                     DeliveryStatus::Sent));
    QCOMPARE(model.rowForMessageId(200), -1);
    QCOMPARE(model.rowForMessageId(250), 0);
    QCOMPARE(model.indexForStableId(0, QStringLiteral("client-200")).row(), 0);
}

void MessageModelTests::textAndChatIdentityRoundTripWithoutNormalization()
{
    MessageListModel model(7);
    const QString unicodeText = QStringLiteral("你好，组合字符 e\u0301，😄\nsecond line");
    QCOMPARE(model.appendMessages({
                 message(1, QStringLiteral("unicode"), unicodeText),
                 message(2, QStringLiteral("empty"), QString())
             }),
             2);
    QCOMPARE(model.data(model.index(0), MessageListModel::TextRole).toString(), unicodeText);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), unicodeText);
    QVERIFY(model.data(model.index(1), MessageListModel::TextRole).toString().isEmpty());
    QVERIFY(model.recordAt(1) != nullptr);
    QVERIFY(model.recordAt(1)->text.isEmpty());

    MessageRecord wrongChat = message(999, QStringLiteral("wrong-chat"));
    wrongChat.chatId = 8;
    const int rowsBeforeWrongChat = model.rowCount();
    QCOMPARE(model.appendMessage(wrongChat), 0);
    QCOMPARE(model.rowCount(), rowsBeforeWrongChat);
    QCOMPARE(model.rowForMessageId(999), -1);
}

void MessageModelTests::storeRetainsOneModelAndPaginationStatePerChat()
{
    MessageModelStore store;
    auto *first = store.getOrCreate(7);
    QCOMPARE(first, store.getOrCreate(7));
    QVERIFY(first != store.getOrCreate(8));
    QCOMPARE(store.find(7), first);

    first->setLoadingHistory(true);
    first->setCanLoadMore(false);
    first->setInitialPageLoaded(true);
    first->setHistoryCursor(123);
    QVERIFY(first->isLoadingHistory());
    QVERIFY(!first->canLoadMore());
    QVERIFY(first->hasLoadedInitialPage());
    QCOMPARE(first->historyCursor(), 123);
}

void MessageModelTests::delegateReflowsLongTextForANarrowViewport()
{
    MessageListModel model(7);
    model.appendMessage(message(
        1,
        {},
        QStringLiteral("中文 English 😄 emoji\n手工换行，以及一段用于验证窗口缩放后自动换行高度的长文本。")));
    QListView view;
    MessageItemDelegate delegate(&view);
    QStyleOptionViewItem wide;
    wide.rect = QRect(0, 0, 700, 100);
    QStyleOptionViewItem narrow;
    narrow.rect = QRect(0, 0, 260, 100);

    const QSize wideSize = delegate.sizeHint(wide, model.index(0));
    const QSize narrowSize = delegate.sizeHint(narrow, model.index(0));
    QVERIFY(wideSize.height() > 0);
    QVERIFY(narrowSize.height() > wideSize.height());
}

QTEST_MAIN(MessageModelTests)

#include "message_model_tests.moc"
