#include "applyfriendlist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>

ApplyFriendList::ApplyFriendList(QWidget *parent) : QListWidget(parent)
{
    // 关闭滚动条
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 安装事件过滤器
    this->viewport()->installEventFilter(this);
}

/**
 * @brief ApplyFriendList::eventFilter
 * @param watched
 * @param event
 * @return
 * 重写的滚轮事件等
 */
bool ApplyFriendList::eventFilter(QObject *watched, QEvent *event)
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

    // ???
    if (watched == this->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            emit sig_show_search(false);
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
