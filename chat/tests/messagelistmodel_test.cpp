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
