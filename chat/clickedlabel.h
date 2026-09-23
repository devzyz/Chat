#ifndef CLICKEDLABEL_H
#define CLICKEDLABEL_H

#include <QLabel>
#include "global.h"
#include "QMoveEvent"

/**
 * @brief The ClickedLabel class
 * 为了让label可点击，同时可以有6种状态（隐藏与显示的眼睛）
 */
class ClickedLabel : public QLabel
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于按选中及鼠标状态切换标签样式。 */
    ClickedLabel(QWidget *parent=nullptr);
    /** @brief 处理鼠标按下，更新控件当前交互状态。 */
    virtual void mousePressEvent(QMouseEvent *ev) override;
    /** @brief 处理鼠标释放并按控件状态触发点击或结束拖动。 */
    virtual void mouseReleaseEvent(QMouseEvent * Ev) override;
    /** @brief 在鼠标进入时切换悬停样式。 */
    virtual void enterEvent(QEnterEvent *event) override;
    /** @brief 在鼠标离开时恢复非悬停样式。 */
    virtual void leaveEvent(QEvent *event) override;
    // 初始化图标的状态
    /** @brief 初始化图标的状态。 */
    void SetState(QString normal_leave="", QString normal_hover="", QString normal_press="",
                  QString select_leave="", QString select_hover="", QString select_press="");
    // 获取图标现在的状态
    /** @brief 获取图标现在的状态。 */
    ClickLabelState GetCurState();
    // 设置当前的状态
    /** @brief 设置当前的状态。 */
    bool SetCurState(ClickLabelState state);
    // 重置状态
    /** @brief 重置状态。 */
    void ResetNormalState();
private:
    QString _normal_leave;
    QString _normal_hover;
    QString _normal_press;

    QString _select_leave;
    QString _select_hover;
    QString _select_press;

    ClickLabelState _curState;

signals:
    /** @brief 通知用户完成一次有效点击。 */
    void clicked();
};

#endif // CLICKEDLABEL_H
