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
    ClickedBtn(QWidget * parent = nullptr);
    ~ClickedBtn();
    void SetState(QString normal, QString hover, QString press);

protected:
    virtual void mousePressEvent(QMouseEvent *e) override;
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
    virtual void enterEvent(QEnterEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;

private:
    QString _normal;
    QString _hover;
    QString _press;
};

#endif // CLICKEDBTN_H
