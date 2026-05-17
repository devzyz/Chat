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
    ClickedLabel(QWidget *parent=nullptr);
    virtual void mousePressEvent(QMouseEvent *ev) override;
    virtual void mouseReleaseEvent(QMouseEvent * Ev) override;
    virtual void enterEvent(QEnterEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;
    void SetState(QString normal_leave="", QString normal_hover="", QString normal_press="",
                  QString select_leave="", QString select_hover="", QString select_press="");
    ClickLabelState GetCurState();
    bool SetCurState(ClickLabelState state);
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
    void clicked();
};

#endif // CLICKEDLABEL_H
