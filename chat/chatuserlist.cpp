#include "chatuserlist.h"
#include <QWheelEvent>
#include <QEvent>
#include <QScrollBar>

ChatUserList::ChatUserList(QWidget *parent) : QListWidget(parent){
    // 将滚动条关闭
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 对当前视口安装过滤器，当事件要发生时，先交给ChatUesrList看一下，再决定是否放行
    this->viewport()->installEventFilter(this);
}

/**
 * @brief ChatUserList::eventFilter
 * @param watched
 * @param event
 * @return
 * 重写了滚轮的一些方法
 */
bool ChatUserList::eventFilter(QObject *watched, QEvent *event) {
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

        // 检查是否滚动到底部
        QScrollBar *scrollBar = this->verticalScrollBar();
        int maxScrollValue = scrollBar->maximum();
        int currentValue = scrollBar->value();

        if (maxScrollValue - currentValue <= 0) {
            // 滚动到底部，加载新的联系人
            qDebug() << "load more chat user";
            // 发送信号通知聊天界面加载更多聊天内容
            emit sig_loading_chat_user();
        }

        return true;
    }

    return QListWidget::eventFilter(watched, event);
}
