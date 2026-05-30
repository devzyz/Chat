#include "clickedlabel.h"

ClickedLabel::ClickedLabel(QWidget* parent) : QLabel(parent), _curState(ClickLabelState::Normal){
    // 进入后鼠标选中为手
    this->setCursor(Qt::PointingHandCursor);
}
/**
 * @brief ClickedLabel::mousePressEvent
 * @param event
 *
 * 鼠标左键按压处理
 */
void ClickedLabel::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if(_curState == ClickLabelState::Normal){
            _curState = ClickLabelState::Selected;
            setProperty("state",_select_press);
            repolish(this);
            update();
        }else{
            _curState = ClickLabelState::Normal;
            setProperty("state",_normal_press);
            repolish(this);
            update();
        }
        return ;
    }

    // 调用基类的mousePressEvent以保证正常的事件处理
    QLabel::mousePressEvent(event);
}

// 点击后的释放事件，只需要转换为对应的hover状态即可
void ClickedLabel::mouseReleaseEvent(QMouseEvent * event)
{
    if (event->button() == Qt::LeftButton) {
        if(_curState == ClickLabelState::Normal){
            setProperty("state",_normal_hover);
            repolish(this);
            update();
        }else{
            setProperty("state",_select_hover);
            repolish(this);
            update();
        }
        emit clicked();
        return ;
    }

    // 调用基类的mousePressEvent以保证正常的事件处理
    QLabel::mouseReleaseEvent(event);
}

/**
 * @brief ClickedLabel::enterEvent
 * @param event
 *
 * 鼠标移入后触发的颜色变化事件
 *
 * 注意这里的setProperty未指定对象，则默认为当前的this,即clicklabel类对象
 */
void ClickedLabel::enterEvent(QEnterEvent *event) {
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _normal_hover);
        repolish(this);
        update();
    }else {
        setProperty("state", _select_hover);
        repolish(this);
        update();
    }

    // 调用父类的进入时间，可能其有一些默认的操作
    QLabel::enterEvent(event);
}

/**
 * @brief ClickedLabel::leaveEvent
 * @param event
 *
 * 鼠标移出后处理的颜色变化事件
 */
void ClickedLabel::leaveEvent(QEvent *event) {
    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _normal_leave);
        repolish(this);
        update();
    }else {
        setProperty("state", _select_leave);
        repolish(this);
        update();
    }

    // 调用父类的离开时间，可能其有一些默认的操作
    QLabel::leaveEvent(event);
}

// 用于保存每种状态
void ClickedLabel::SetState(QString normal_leave, QString normal_hover, QString normal_press,
                            QString select_leave, QString select_hover, QString select_press) {
    _normal_leave = normal_leave;
    _normal_hover = normal_hover;
    _normal_press = normal_press;

    _select_leave = select_leave;
    _select_hover = select_hover;
    _select_press = select_press;

    setProperty("state", normal_leave);
    repolish(this);
}

ClickLabelState ClickedLabel::GetCurState() {
    return _curState;
}

bool ClickedLabel::SetCurState(ClickLabelState state)
{
    _curState = state;

    if (_curState == ClickLabelState::Normal) {
        setProperty("state", _normal_leave);
        repolish(this);
    }else if (_curState == ClickLabelState::Selected) {
        setProperty("state", _select_leave);
        repolish(this);
    }

    return true;
}

void ClickedLabel::ResetNormalState()
{
    _curState = ClickLabelState::Normal;
    setProperty("state", _normal_leave);
    repolish(this);
}
