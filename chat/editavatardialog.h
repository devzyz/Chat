#ifndef EDITAVATARDIALOG_H
#define EDITAVATARDIALOG_H

#include <QDialog>

namespace Ui {
class EditAvatarDialog;
}

class EditAvatarDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditAvatarDialog(QWidget *parent = nullptr);
    ~EditAvatarDialog();

private:
    Ui::EditAvatarDialog *ui;
};

#endif // EDITAVATARDIALOG_H
