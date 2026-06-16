#ifndef FRIENDINFOPAGE_H
#define FRIENDINFOPAGE_H

#include <QWidget>
#include "userdata.h"
#include <memory>

namespace Ui {
class FriendInfoPage;
}

class FriendInfoPage : public QWidget
{
    Q_OBJECT

public:
    explicit FriendInfoPage(QWidget *parent = nullptr);
    ~FriendInfoPage();
    void SetInfo(std::shared_ptr<UserInfo>);

private:
    Ui::FriendInfoPage *ui;
    std::shared_ptr<UserInfo> _friend_info;

signals:
    void sig_jump_chat_item(std::shared_ptr<UserInfo>);

public slots:
    void on_info_chat_label_clicked();
};

#endif // FRIENDINFOPAGE_H
