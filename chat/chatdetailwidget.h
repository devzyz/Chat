#ifndef CHATDETAILWIDGET_H
#define CHATDETAILWIDGET_H

#include <QWidget>

namespace Ui {
class ChatDetailWidget;
}

class ChatDetailWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatDetailWidget(QWidget *parent = nullptr);
    ~ChatDetailWidget();

    void appendChatItem(QWidget *item);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Ui::ChatDetailWidget *ui;
};

#endif // CHATDETAILWIDGET_H
