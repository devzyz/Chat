#include "clickedoncelabel.h"

ClickedOnceLabel::ClickedOnceLabel(QWidget *parent) : QLabel(parent)
{
    // 设置鼠标变形
    setCursor(Qt::PointingHandCursor);
}

void ClickedOnceLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(this->text());
        return ;
    }

    // 调用基类
    QLabel::mouseReleaseEvent(event);
}


