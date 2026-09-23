#ifndef AUTHFRIENDDIALOG_H
#define AUTHFRIENDDIALOG_H

#include <QDialog>
#include <memory>
#include "userdata.h"

namespace Ui {
class AuthFriendDialog;
}

/** @brief 展示好友申请并提交同意添加好友的操作。 */
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
    /** @brief 提交好友审批及双方备注资料。 */
    void slot_auth_apply_sure();

private:
    Ui::AuthFriendDialog *ui;
    // 用于保存申请人的信息
    std::shared_ptr<ApplyInfo> _apply_info;
};

#endif // AUTHFRIENDDIALOG_H
