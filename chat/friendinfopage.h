#ifndef FRIENDINFOPAGE_H
#define FRIENDINFOPAGE_H

#include <QWidget>
#include "userdata.h"
#include <memory>

namespace Ui {
class FriendInfoPage;
}

/** @brief 展示当前好友资料并提供进入私聊的入口。 */
class FriendInfoPage : public QWidget
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于展示当前好友资料并提供进入私聊的入口。 */
    explicit FriendInfoPage(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~FriendInfoPage();
    /** @brief 保存资料并刷新对应页面或列表条目的展示。 */
    void setInfo(std::shared_ptr<UserInfo>);

private:
    Ui::FriendInfoPage *ui;
    std::shared_ptr<UserInfo> _friend_info;

signals:
    /** @brief 通知上层选择或创建该用户对应的私聊页面。 */
    void chatRequested(std::shared_ptr<UserInfo>);

public slots:
    /** @brief 根据当前好友资料请求进入私聊。 */
    void on_info_chat_label_clicked();
};

#endif // FRIENDINFOPAGE_H
