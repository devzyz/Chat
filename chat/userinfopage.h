#ifndef USERINFOPAGE_H
#define USERINFOPAGE_H

#include <QWidget>
#include "editavatardialog.h"

namespace Ui {
class UserInfoPage;
}

class UserInfoPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserInfoPage(QWidget *parent = nullptr);
    ~UserInfoPage();

private slots:
    void on_user_info_page_upload_btn_clicked();

private:
    Ui::UserInfoPage *ui;
    EditAvatarDialog * _edit_avatar_dialog;
};

#endif // USERINFOPAGE_H
