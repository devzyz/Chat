#include "statewidget.h"
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>

StateWidget::StateWidget(QWidget *parent) : QWidget(parent), _curState(ClickLabelState::Normal)
{
    setCursor(Qt::PointingHandCursor);
    // 添加红点
    AddRedPoint();
}

void StateWidget::SetState(QString leave, QString hover, QString select)
{
    _leave = leave;
    _hover = hover;
    _select = select;

    setProperty("state", leave);
    repolish(this);
}

ClickLabelState StateWidget::GetCurState()
{
    return _curState;
}

void StateWidget::ClearState()
{
    _curState = ClickLabelState::Normal;
    setProperty("state", _leave);
    repolish(this);
    update();
}

void StateWidget::SetSelected(bool bselected)
{
    if (bselected) {
        _curState = ClickLabelState::Selected;
        setProperty("state", _select);
        repolish(this);
        update();
        return ;
    }

    _curState = ClickLabelState::Normal;
    setProperty("state", _leave);
    repolish(this);
    update();
}

void StateWidget::AddRedPoint() {
    // 添加红点
    _red_point = new QLabel();
    _red_point->setObjectName("red_point");
    QVBoxLayout * layout2 = new QVBoxLayout();

    _red_point->setAlignment(Qt::AlignCenter);
    layout2->addWidget(_red_point);
    layout2->setContentsMargins(0, 0, 0, 0);
    this->setLayout(layout2);
    _red_point->setVisible(false);
}

// 显示红点
void StateWidget::ShowRedPoint(bool show)
{
    _red_point->setVisible(true);
}

// 为了让自定义样式生效
void StateWidget::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void StateWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (_curState == ClickLabelState::Normal) {
            _curState = ClickLabelState::Selected;
            setProperty("state", _select);
            repolish(this);
            update();
        }

        return ;
    }
    // 调用基类的鼠标按压事件
    QWidget::mousePressEvent(event);
}

void StateWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        emit clicked();
        return ;
    }
    // 调用基类的鼠标按压事件
    QWidget::mouseReleaseEvent(event);
}

void StateWidget::enterEvent(QEnterEvent *event)
{
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _hover);
        repolish(this);
        update();
    }
}

void StateWidget::leaveEvent(QEvent *event)
{
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _leave);
        repolish(this);
        update();
    }
}
