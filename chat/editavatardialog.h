#ifndef EDITAVATARDIALOG_H
#define EDITAVATARDIALOG_H

#include <QDialog>
#include <QPixmap>
#include "localavatar.h"

class QPushButton;

namespace Ui {
class EditAvatarDialog;
}

class EditAvatarDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditAvatarDialog(LocalAvatar *avatar, const QPixmap &current, QWidget *parent = nullptr);
    ~EditAvatarDialog();
    void reject() override;

private:
    Ui::EditAvatarDialog *ui;
    LocalAvatar *_avatar;
    QPushButton *_chooseButton;
};

#endif // EDITAVATARDIALOG_H
