#ifndef CLICKEDBTN_H
#define CLICKEDBTN_H

#include <QPushButton>

/**
 * @brief The ClickedBtn class
 * 自定义的clickedBtn,目的是重写按钮的一些进入点击等事件，进行图标切换
 */
class ClickedBtn : public QPushButton
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于按鼠标状态切换按钮样式。 */
    ClickedBtn(QWidget * parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ClickedBtn();
    // 初始化按钮的状态设置
    /** @brief 初始化按钮的状态设置。 */
    void setState(QString normal, QString hover, QString press);

protected:
    /** @brief 处理鼠标按下，更新控件当前交互状态。 */
    virtual void mousePressEvent(QMouseEvent *e) override;
    /** @brief 处理鼠标释放并按控件状态触发点击或结束拖动。 */
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
    /** @brief 在鼠标进入时切换悬停样式。 */
    virtual void enterEvent(QEnterEvent *event) override;
    /** @brief 在鼠标离开时恢复非悬停样式。 */
    virtual void leaveEvent(QEvent *event) override;

private:
    QString _normal;
    QString _hover;
    QString _press;
};

#endif // CLICKEDBTN_H
