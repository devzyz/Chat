#include "messageitemdelegate.h"
#include "messagelistmodel.h"
#include "messagemodelstore.h"

#include <QApplication>
#include <QListView>
#include <iostream>

namespace {
MessageRecord message(qint64 id, const QString &clientId,
                      const QString &text = QStringLiteral("text"))
{
    MessageRecord record;
    record.messageId = id;
    record.clientMessageId = clientId;
    record.chatId = 7;
    record.senderId = 1;
    record.senderName = QStringLiteral("测试用户");
    record.sentAt = QDateTime::currentDateTime();
    record.deliveryStatus = id > 0 ? DeliveryStatus::Read : DeliveryStatus::Sending;
    record.isSelf = true;
    record.text = text;
    return record;
}

bool check(bool condition, const char *description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
    }
    return condition;
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    bool passed = true;

    MessageListModel model(7);
    int inserted = 0;
    int changed = 0;
    int removed = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                     [&inserted]() { ++inserted; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&changed]() { ++changed; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved,
                     [&removed]() { ++removed; });

    passed &= check(model.appendMessage(message(0, QStringLiteral("uuid-1"))) == 1,
                    "append inserts one local message");
    passed &= check(inserted == 1, "append emits rowsInserted");
    passed &= check(model.rowForClientMessageId(QStringLiteral("uuid-1")) == 0,
                    "client UUID has a fast row index");
    passed &= check(model.acknowledgeMessage(QStringLiteral("uuid-1"), 42,
                                             DeliveryStatus::Sent),
                    "acknowledgement updates by UUID");
    passed &= check(model.rowForMessageId(42) == 0, "formal message id has a fast row index");
    passed &= check(changed == 1, "acknowledgement emits dataChanged");
    passed &= check(model.updateStatusByMessageId(42, DeliveryStatus::Read)
                        && changed == 2,
                    "status update by formal id emits dataChanged");
    passed &= check(model.removeByMessageId(42), "remove finds a message by formal id");
    passed &= check(removed == 1 && model.rowCount() == 0,
                    "remove emits rowsRemoved and erases the row");

    MessageListModel unknownIdModel(7);
    unknownIdModel.appendMessage(message(101, QStringLiteral("known-client")));
    int unknownIdChanges = 0;
    int unknownIdRemovals = 0;
    QObject::connect(&unknownIdModel, &QAbstractItemModel::dataChanged,
                     [&unknownIdChanges]() { ++unknownIdChanges; });
    QObject::connect(&unknownIdModel, &QAbstractItemModel::rowsRemoved,
                     [&unknownIdRemovals]() { ++unknownIdRemovals; });
    passed &= check(!unknownIdModel.acknowledgeMessage(QStringLiteral("missing-client"), 202,
                                                       DeliveryStatus::Sent)
                        && !unknownIdModel.updateStatusByClientId(
                            QStringLiteral("missing-client"), DeliveryStatus::Failed)
                        && !unknownIdModel.updateStatusByMessageId(202, DeliveryStatus::Failed)
                        && !unknownIdModel.removeByMessageId(202),
                    "operations for unknown stable ids report no change");
    passed &= check(unknownIdChanges == 0 && unknownIdRemovals == 0
                        && unknownIdModel.rowCount() == 1
                        && unknownIdModel.rowForMessageId(101) == 0
                        && unknownIdModel.data(
                               unknownIdModel.index(0),
                               MessageListModel::DeliveryStatusRole).toInt()
                            == static_cast<int>(DeliveryStatus::Read),
                    "unknown stable ids leave the model and indexes unchanged");

    model.appendMessage(message(30, {}));
    const QVector<MessageRecord> history = {
        message(20, {}, QStringLiteral("中文")),
        message(10, {}, QStringLiteral("English 😀\nmanual wrap")),
        message(20, {}),
        message(30, {})
    };
    passed &= check(model.prependHistory(history) == 2,
                    "history insert removes batch and model duplicates");
    passed &= check(model.rowCount() == 3, "history plus existing row count is correct");
    passed &= check(model.data(model.index(0), MessageListModel::MessageIdRole).toLongLong() == 10
                    && model.data(model.index(1), MessageListModel::MessageIdRole).toLongLong() == 20
                    && model.data(model.index(2), MessageListModel::MessageIdRole).toLongLong() == 30,
                    "history is stored in chronological order");
    passed &= check(model.data(model.index(0), MessageListModel::TextRole).toString()
                        == QStringLiteral("English 😀\nmanual wrap"),
                    "multilingual text and manual newlines remain intact");

    MessageListModel pagedHistoryModel(7);
    pagedHistoryModel.appendMessage(message(50, QStringLiteral("server-50")));
    passed &= check(pagedHistoryModel.prependHistory({
                        message(40, QStringLiteral("server-40")),
                        message(30, QStringLiteral("server-30"))
                    }) == 2,
                    "first history page is inserted");
    passed &= check(pagedHistoryModel.prependHistory({
                        message(20, QStringLiteral("server-20")),
                        message(10, QStringLiteral("server-10")),
                        message(30, QStringLiteral("duplicate-message-id")),
                        message(15, QStringLiteral("server-20"))
                    }) == 2,
                    "later history page deduplicates ids seen in earlier pages");
    const QVector<qint64> expectedPagedIds = {10, 20, 30, 40, 50};
    bool pagesRemainChronological = pagedHistoryModel.rowCount() == expectedPagedIds.size();
    for (int row = 0; row < expectedPagedIds.size() && pagesRemainChronological; ++row) {
        pagesRemainChronological = pagedHistoryModel.data(
            pagedHistoryModel.index(row), MessageListModel::MessageIdRole).toLongLong()
            == expectedPagedIds.at(row);
    }
    passed &= check(pagesRemainChronological,
                    "history remains chronological across multiple older pages");
    passed &= check(pagedHistoryModel.oldestMessageId() == 10,
                    "oldest message id follows the earliest retained history row");

    MessageListModel indexModel(7);
    indexModel.appendMessages({
        message(100, QStringLiteral("client-100")),
        message(200, QStringLiteral("client-200")),
        message(300, QStringLiteral("client-300"))
    });
    passed &= check(indexModel.removeByMessageId(100)
                        && indexModel.rowForMessageId(100) == -1
                        && indexModel.rowForClientMessageId(QStringLiteral("client-100")) == -1
                        && indexModel.rowForMessageId(200) == 0
                        && indexModel.rowForMessageId(300) == 1,
                    "removal rebuilds message and client indexes for shifted rows");
    passed &= check(indexModel.acknowledgeMessage(
                        QStringLiteral("client-200"), 250, DeliveryStatus::Sent)
                        && indexModel.rowForMessageId(200) == -1
                        && indexModel.rowForMessageId(250) == 0
                        && indexModel.indexForStableId(
                               0, QStringLiteral("client-200")).row() == 0,
                    "acknowledgement replaces the formal id without desynchronizing indexes");

    MessageListModel textModel(7);
    const QString unicodeText = QStringLiteral("你好，مرحبا，😀\nsecond line");
    passed &= check(textModel.appendMessages({
                        message(1, QStringLiteral("unicode"), unicodeText),
                        message(2, QStringLiteral("empty"), QString())
                    }) == 2,
                    "unicode and empty text messages are accepted");
    passed &= check(textModel.data(textModel.index(0), MessageListModel::TextRole).toString()
                            == unicodeText
                        && textModel.data(textModel.index(0), Qt::DisplayRole).toString()
                            == unicodeText
                        && textModel.data(textModel.index(1), MessageListModel::TextRole).toString()
                            .isEmpty()
                        && textModel.recordAt(1) != nullptr
                        && textModel.recordAt(1)->text.isEmpty(),
                    "unicode, newlines, and empty text round-trip without normalization");

    MessageRecord wrongChat = message(999, QStringLiteral("wrong-chat"));
    wrongChat.chatId = 8;
    const int textRowsBeforeWrongChat = textModel.rowCount();
    passed &= check(textModel.appendMessage(wrongChat) == 0
                        && textModel.rowCount() == textRowsBeforeWrongChat
                        && textModel.rowForMessageId(999) == -1,
                    "messages belonging to another chat are ignored");

    MessageModelStore store;
    auto *first = store.getOrCreate(7);
    passed &= check(first == store.getOrCreate(7), "store keeps one resident model per chat");
    passed &= check(first != store.getOrCreate(8), "different chats receive different models");
    passed &= check(store.find(7) == first, "store lookup returns the owned model");
    first->setLoadingHistory(true);
    first->setCanLoadMore(false);
    first->setInitialPageLoaded(true);
    first->setHistoryCursor(123);
    passed &= check(first->isLoadingHistory() && !first->canLoadMore()
                        && first->hasLoadedInitialPage() && first->historyCursor() == 123,
                    "history pagination state is retained per model");

    MessageListModel layoutModel(7);
    layoutModel.appendMessage(message(
        1, {}, QStringLiteral("中文 English 😀 emoji\n手工换行，以及一段用于验证窗口缩放后自动换行高度的长文本。")));
    QListView view;
    MessageItemDelegate delegate(&view);
    QStyleOptionViewItem wide;
    wide.rect = QRect(0, 0, 700, 100);
    QStyleOptionViewItem narrow;
    narrow.rect = QRect(0, 0, 260, 100);
    const QSize wideSize = delegate.sizeHint(wide, layoutModel.index(0));
    const QSize narrowSize = delegate.sizeHint(narrow, layoutModel.index(0));
    passed &= check(wideSize.height() > 0 && narrowSize.height() > wideSize.height(),
                    "delegate reflows multilingual long text when viewport narrows");

    if (passed) {
        std::cout << "All message model tests passed\n";
        return 0;
    }
    return 1;
}
