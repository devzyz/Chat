#ifndef FINDSUCCESSDIALOG_H
#define FINDSUCCESSDIALOG_H

#include <QDialog>
#include "userdata.h"
#include <memory>

namespace Ui {
class FindSuccessDialog;
}

/**
 * @brief The FindSuccessDialog class
 * 搜索添加好友成功后的弹出框
 */
class FindSuccessDialog : public QDialog
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于展示命中用户资料并提供添加好友入口。 */
    explicit FindSuccessDialog(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~FindSuccessDialog();
    /** @brief 保存搜索命中的用户资料并刷新对话框展示。 */
    void SetSearchInfo(std::shared_ptr<SearchInfo> si);

private:
    Ui::FindSuccessDialog *ui;
    std::shared_ptr<SearchInfo> _si;

    // 可能传数据给父节点
    QWidget * _parent;

private slots:
    /** @brief 打开好友申请编辑对话框。 */
    void on_add_friend_btn_clicked();
};

#endif // FINDSUCCESSDIALOG_H
