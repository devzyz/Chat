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
    explicit StateWidget(QWidget * parent = nullptr);

    void SetState(QString leave="", QString hover="", QString select="");

    ClickLabelState GetCurState();
    void ClearState();

    void SetSelected(bool bselected);
    void AddRedPoint();
    void ShowRedPoint(bool show = true);

protected:
    void paintEvent(QPaintEvent * event) override;

    virtual void mousePressEvent(QMouseEvent * ev) override;
    virtual void mouseReleaseEvent(QMouseEvent * ev) override;
    virtual void enterEvent(QEnterEvent * event) override;
    virtual void leaveEvent(QEvent * event) override;

private:
    QString _leave;
    QString _hover;
    QString _select;

    ClickLabelState _curState;
    QLabel * _red_point;

signals:
    void clicked(void);
};

#endif // STATEWIDGET_H
