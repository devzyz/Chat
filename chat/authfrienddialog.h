#ifndef AUTHFRIENDDIALOG_H
#define AUTHFRIENDDIALOG_H

#include <QDialog>
#include <memory>
#include "userdata.h"

namespace Ui {
class AuthFriendDialog;
}

/**
 * @brief The AuthFriendDialog class
 * 验证添加好友请求弹出框
 */
class AuthFriendDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AuthFriendDialog(QWidget *parent = nullptr);
    ~AuthFriendDialog();
    /**
     * @brief SetApplyInfo
     * @param applyinfo
     * 设置验证好友信息
     */
    void SetApplyInfo(std::shared_ptr<ApplyInfo> applyinfo);

private slots:
    // 取消验证
    void slot_auth_apply_cancel();
    // 同意添加好友
    void slot_auth_apply_sure();

private:
    Ui::AuthFriendDialog *ui;
    // 用于保存申请人的信息
    std::shared_ptr<ApplyInfo> _apply_info;
};

#endif // AUTHFRIENDDIALOG_H
