#include "clickedoncelabel.h"

ClickedOnceLabel::ClickedOnceLabel(QWidget *parent) : QLabel(parent)
{
    // 设置鼠标变形
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void ClickedOnceLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (_activation.release(this, event)) { emit clicked(text()); }
    QLabel::mouseReleaseEvent(event);
}



void ClickedOnceLabel::mousePressEvent(QMouseEvent *event)
{
    _activation.press(this, event);
    QLabel::mousePressEvent(event);
}

void ClickedOnceLabel::keyPressEvent(QKeyEvent *event)
{
    if (_activation.keyPress(event)) { event->accept(); return; }
    QLabel::keyPressEvent(event);
}
void ClickedOnceLabel::keyReleaseEvent(QKeyEvent *event)
{
    if (_activation.keyRelease(event)) { emit clicked(text()); event->accept(); return; }
    QLabel::keyReleaseEvent(event);
}
void ClickedOnceLabel::focusOutEvent(QFocusEvent *event)
{
    _activation.cancel(); QLabel::focusOutEvent(event);
}
