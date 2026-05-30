#ifndef APPLYFRIENDLIST_H
#define APPLYFRIENDLIST_H

#include <QListWidget>

/**
 * @brief The ApplyFriendList class
 * 重写新的朋友的QListWidget，自定义的QListWidget
 */
class ApplyFriendList : public QListWidget
{
    Q_OBJECT
public:
    ApplyFriendList(QWidget * parent = nullptr);

protected:
    bool eventFilter(QObject * watched, QEvent * event) override;

signals:
    void sig_show_search(bool);
};

#endif // APPLYFRIENDLIST_H
