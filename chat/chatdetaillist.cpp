#include "chatdetaillist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QListWidgetItem>

ChatDetailList::ChatDetailList(QWidget *parent) : QListWidget(parent){

}

// 将listitem插入到QListWidget列表中
void ChatDetailList::appendChatItem(QWidget * chatitem)
{
    QListWidgetItem * listItem = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    listItem->setSizeHint(chatitem->sizeHint());
    // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
    this->addItem(listItem);
    this->setItemWidget(listItem, chatitem);
}

// 删除当前QListWidget内的所有item
void ChatDetailList::removeAllItem()
{
    // 枚举所有的item
    while(this->count() > 0) {
        QListWidgetItem * item = this->takeItem(0);
        if (item) {
            QWidget * widget = this->itemWidget(item);
            if (widget) {
                delete widget;
            }
            delete item;
        }
    }
}

/**
 * @brief ContactUserList::eventFilter
 * @param watched
 * @param event
 * @return
 * 重写QListWidget的鼠标移入移出（是否显示滚轮）
 * 重写在QListWidget内鼠标的滚动事件
 */
bool ChatDetailList::eventFilter(QObject *watched, QEvent *event)
{
    // 如果鼠标进入了这个列表的显示窗口区域
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            // 鼠标悬浮在当前窗口，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }else if(event->type() == QEvent::Leave){
            // 鼠标不在当前窗口，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查鼠标在显示窗口内的滚轮事件
    if (watched == this->viewport() && event->type() == QEvent::Wheel) {
        // 将基类QEvent转换为对应的子类，QWheelEvent来访问他的私有数据
        QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
        // 计算移动的步长
        // angleDeltada返回的单位为1/8度，因此，除以8能够得到角度，然后一般鼠标滚轮嘎达一下为15度，因此除以15得到步数
        int numDegrees = wheelEvent->angleDelta().y() / 8;
        int numSteps = numDegrees / 15;

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        return true;
    }

    return QListWidget::eventFilter(watched, event);
}
