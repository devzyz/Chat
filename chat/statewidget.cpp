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

/**
 * @brief StateWidget::SetState
 * @param leave
 * @param hover
 * @param select
 * 初始化不同状态名称，通过其控制qss样式表
 */
void StateWidget::SetState(QString leave, QString hover, QString select)
{
    _leave = leave;
    _hover = hover;
    _select = select;

    setProperty("state", leave);
    repolish(this);
}

/**
 * @brief StateWidget::GetCurState
 * @return
 * 获取当前的statewidget状态
 */
ClickLabelState StateWidget::GetCurState()
{
    return _curState;
}

/**
 * @brief StateWidget::ClearState
 * 清空当前的状态为关闭状态
 */
void StateWidget::ClearState()
{
    _curState = ClickLabelState::Normal;
    setProperty("state", _leave);
    repolish(this);
    update();
}

/**
 * @brief StateWidget::SetSelected
 * @param bselected
 * 设置StateWidget是否是选中状态
 */
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

/**
 * @brief StateWidget::AddRedPoint
 * 给StateWidget添加一个红点
 */
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
    _red_point->setVisible(show);
}

// 为了让自定义样式生效
void StateWidget::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

/**
 * @brief StateWidget::mousePressEvent
 * @param event
 * 重写鼠标左键按压事件，如果左键按压后为普通模式，则转换为选中模式
 */
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

/**
 * @brief StateWidget::mouseReleaseEvent
 * @param event
 * 左键按压后的释放事件
 */
void StateWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        emit clicked();
        return ;
    }
    // 调用基类的鼠标按压事件
    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief StateWidget::enterEvent
 * @param event
 * 鼠标移入事件
 */
void StateWidget::enterEvent(QEnterEvent *event)
{
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _hover);
        repolish(this);
        update();
    }
}

/**
 * @brief StateWidget::leaveEvent
 * @param event
 * 鼠标移出事件
 */
void StateWidget::leaveEvent(QEvent *event)
{
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _leave);
        repolish(this);
        update();
    }
}
