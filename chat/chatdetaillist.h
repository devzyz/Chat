#ifndef CHATDETAILLIST_H
#define CHATDETAILLIST_H

#include <QListView>

class ChatDetailList : public QListView
{
    Q_OBJECT
public:
    explicit ChatDetailList(QWidget *parent = nullptr);

    bool isNearBottom(int tolerance = 24) const;

signals:
    void nearTopReached();
    void viewportResized();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

#endif // CHATDETAILLIST_H
