#include "listviewbehavior.h"
#include <QEnterEvent>
#include <QListView>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QWheelEvent>

/** @brief 使用真实 Qt 列表验证共享滚动行为，不依赖网络或账号状态。 */
class ListViewTests : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 悬停行为同时适用于模型视图和条目列表，且不修改模型或选择策略。 */
    void sharesHoverWithoutChangingModels()
    {
        QListView view;
        QStandardItemModel model;
        view.setModel(&model);
        view.setSelectionMode(QAbstractItemView::NoSelection);
        new ListViewBehavior(&view);
        QEnterEvent enter(QPointF(10, 10), QPointF(10, 10), QPointF(10, 10));
        QCoreApplication::sendEvent(&view, &enter);
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(&view, &leave);
        QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(view.model(), &model);
        QCOMPARE(view.selectionMode(), QAbstractItemView::NoSelection);
    }

    /** @brief 与未封装列表对比滚轮结果，防止手动步长或吞事件破坏 Qt 滚动。 */
    void preservesNativeWheelAndSelection()
    {
        QListWidget native;
        QListWidget shared;
        new ListViewBehavior(&shared);
        for (auto *view : {&native, &shared}) {
            view->resize(240, 160);
            view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            for (int i = 0; i < 80; ++i) view->addItem(QString::number(i));
            view->show();
        }
        QCoreApplication::processEvents();
        for (int delta : {-120, -30, -30, -30, -30, 120}) {
            for (auto *view : {&native, &shared}) {
                QWheelEvent wheel(QPointF(30, 30), QPointF(30, 30), {}, QPoint(0, delta),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(view->viewport(), &wheel);
            }
            QCOMPARE(shared.verticalScrollBar()->value(), native.verticalScrollBar()->value());
        }
        QVERIFY(shared.verticalScrollBar()->value() > 0);
        shared.scrollToTop();
        QTest::mouseClick(shared.viewport(), Qt::LeftButton, Qt::NoModifier,
                          shared.visualItemRect(shared.item(0)).center());
        QCOMPARE(shared.currentRow(), 0);
    }

    /** @brief 键盘和滚动条到底均通知；短列表可向下重试且不会向上误触发。 */
    void notifiesBottomAcrossInputs()
    {
        QListWidget view;
        auto *behavior = new ListViewBehavior(&view);
        QSignalSpy reached(behavior, &ListViewBehavior::bottomReached);
        view.resize(240, 160);
        for (int i = 0; i < 80; ++i) view.addItem(QString::number(i));
        view.show();
        QCoreApplication::processEvents();
        reached.clear();
        QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier);
        QTRY_COMPARE(reached.count(), 1);
        view.verticalScrollBar()->setValue(0);
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());
        QTRY_COMPARE(reached.count(), 2);
        view.clear();
        view.addItem("only row");
        QCoreApplication::processEvents();
        reached.clear();
        QWheelEvent down(QPointF(30, 30), QPointF(30, 30), {}, QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view.viewport(), &down);
        QTRY_COMPARE(reached.count(), 1);
        QWheelEvent up(QPointF(30, 30), QPointF(30, 30), {}, QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view.viewport(), &up);
        QCoreApplication::processEvents();
        QCOMPARE(reached.count(), 1);
        QCoreApplication::sendEvent(view.viewport(), &down);
        QTRY_COMPARE(reached.count(), 2);
    }

    /** @brief 排队的底部通知随视图销毁而取消，不保留跨页面回调。 */
    void destructionCancelsQueuedNotification()
    {
        auto *view = new QListWidget;
        auto *behavior = new ListViewBehavior(view);
        QSignalSpy reached(behavior, &ListViewBehavior::bottomReached);
        view->show();
        QWheelEvent down(QPointF(30, 30), QPointF(30, 30), {}, QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &down);
        delete view;
        QCoreApplication::processEvents();
        QCOMPARE(reached.count(), 0);
    }
};

QTEST_MAIN(ListViewTests)
#include "list_view_tests.moc"
