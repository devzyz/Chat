#ifndef APPLYFRIENDPAGE_H
#define APPLYFRIENDPAGE_H

#include <QWidget>
#include "applyfrienditem.h"

namespace Ui {
class ApplyFriendPage;
}

/**
 * @brief The ApplyFriendPage class
 * 点击ContactUserList内的新的朋友item后触发信号，右侧切换到当前界面
 * 好友申请界面
 */
class ApplyFriendPage : public QWidget
{
    Q_OBJECT
public:
    explicit ApplyFriendPage(QWidget *parent = nullptr);
    ~ApplyFriendPage();
    /**
     * @brief AddNewApply
     * 添加新的申请好友请求
     */
    void AddNewApply(std::shared_ptr<ApplyInfo>);

protected:
    void paintEvent(QPaintEvent * event) override;

private:
    void loadApplyList();
    QMap<int, ApplyFriendItem*> _apply_items_map;

private:
    Ui::ApplyFriendPage *ui;

signals:
    void sig_show_search(bool);

public slots:
    void slot_tcp_add_auth_friend(std::shared_ptr<AuthInfo>);
};

#endif // APPLYFRIENDPAGE_H
