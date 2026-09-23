#ifndef EDITAVATARDIALOG_H
#define EDITAVATARDIALOG_H

#include <QDialog>
#include <QPixmap>
#include "localavatar.h"

class QPushButton;

namespace Ui {
class EditAvatarDialog;
}

/** @brief 协调头像选图、裁剪和保存，保存中限制关闭以保护当前操作。 */
class EditAvatarDialog : public QDialog
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于协调头像选图、裁剪和保存，保存中限制关闭以保护当前操作。 */
    explicit EditAvatarDialog(LocalAvatar *avatar, const QPixmap &current, QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~EditAvatarDialog();
    /** @brief 处理关闭请求；保存等不可中断阶段由实现决定是否允许关闭。 */
    void reject() override;

private:
    Ui::EditAvatarDialog *ui;
    LocalAvatar *_avatar;
    QPushButton *_chooseButton;
};

#endif // EDITAVATARDIALOG_H
