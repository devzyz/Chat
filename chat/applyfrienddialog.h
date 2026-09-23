#ifndef APPLYFRIENDDIALOG_H
#define APPLYFRIENDDIALOG_H

#include <QDialog>
#include <userdata.h>

namespace Ui {
class ApplyFriendDialog;
}

/** @brief 收集好友申请内容并向服务器提交申请。 */
class ApplyFriendDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ApplyFriendDialog(QWidget *parent = nullptr);
    ~ApplyFriendDialog();
    void SetSearchInfo(std::shared_ptr<SearchInfo> si);
protected:

private:
    Ui::ApplyFriendDialog *ui;

    // 保存查询到的人的信息
    std::shared_ptr<SearchInfo> _si;

public slots:
    /** @brief 使用当前账号和搜索结果发送好友申请。 */
    void slot_send_apply_sure();
    void slot_send_apply_cancel();
};

#endif // APPLYFRIENDDIALOG_H
