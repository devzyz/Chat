#ifndef USERINFOPAGE_H
#define USERINFOPAGE_H

#include <QWidget>
#include "editavatardialog.h"
#include <QPointer>

namespace Ui {
class UserInfoPage;
}

/** @brief 展示当前账号资料，协调头像裁剪和上传入口。 */
class UserInfoPage : public QWidget
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于展示当前账号资料，协调头像裁剪和上传入口。 */
    explicit UserInfoPage(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~UserInfoPage();

private slots:
    /** @brief 打开头像编辑入口并将结果交给当前账号头像流程。 */
    void on_user_info_page_upload_btn_clicked();

private:
    Ui::UserInfoPage *ui;
    QPointer<EditAvatarDialog> _edit_avatar_dialog;
};

#endif // USERINFOPAGE_H
