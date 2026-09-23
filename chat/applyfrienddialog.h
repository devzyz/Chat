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
    /** @brief 初始化对象，用于编辑并发送好友申请。 */
    explicit ApplyFriendDialog(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ApplyFriendDialog();
    /** @brief 保存搜索命中的用户资料并刷新对话框展示。 */
    void setSearchInfo(std::shared_ptr<SearchInfo> si);
protected:

private:
    Ui::ApplyFriendDialog *ui;

    // 保存查询到的人的信息
    std::shared_ptr<SearchInfo> _si;

public slots:
    /** @brief 使用当前账号和搜索结果发送好友申请。 */
    void sendApplySure();
    /** @brief 取消好友申请对话框并结束本次编辑。 */
    void sendApplyCancel();
};

#endif // APPLYFRIENDDIALOG_H
