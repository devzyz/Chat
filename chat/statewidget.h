#ifndef STATEWIDGET_H
#define STATEWIDGET_H

#include <QWidget>
#include "global.h"
#include <QLabel>

/**
 * @brief The StateWidget class
 * 左侧的菜单栏。其中聊天列表，联系人都是用这个StateWidget
 * 目的是为了能够在右上角显示一个红点
 */
class StateWidget : public QWidget
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于按鼠标交互状态更新自绘组件样式。 */
    explicit StateWidget(QWidget * parent = nullptr);

    /** @brief 设置控件各交互状态对应的样式名称。 */
    void setState(QString leave="", QString hover="", QString select="");

    /**
     * @brief getCurState
     * @return
     * 获取当前StateWidget的状态
     */
    ClickLabelState getCurState();
    /**
     * @brief clearState
     * 刷新状态
     */
    void clearState();

    /**
     * @brief setSelected
     * 设置为选中状态
     */
    void setSelected(bool bselected);
    /**
     * @brief addRedPoint
     * 设置红点
     */
    void addRedPoint();
    /**
     * @brief showRedPoint
     * 是否展示红点
     */
    void showRedPoint(bool show = true);

protected:
    /** @brief 绘制当前控件的背景、边框或内容，遵循 Qt GUI 线程事件约束。 */
    void paintEvent(QPaintEvent * event) override;

    // 重写的一些鼠标点击释放，移入移出事件
    /** @brief 处理鼠标按下，更新控件当前交互状态。 */
    virtual void mousePressEvent(QMouseEvent * ev) override;
    /** @brief 处理鼠标释放并按控件状态触发点击或结束拖动。 */
    virtual void mouseReleaseEvent(QMouseEvent * ev) override;
    /** @brief 在鼠标进入时切换悬停样式。 */
    virtual void enterEvent(QEnterEvent * event) override;
    /** @brief 在鼠标离开时恢复非悬停样式。 */
    virtual void leaveEvent(QEvent * event) override;

private:
    QString _leave;
    QString _hover;
    QString _select;

    ClickLabelState _curState;
    QLabel * _red_point;

signals:
    /** @brief 通知用户完成一次有效点击。 */
    void clicked(void);
};

#endif // STATEWIDGET_H
