#ifndef FINDFAILDIALOG_H
#define FINDFAILDIALOG_H

#include <QDialog>

namespace Ui {
class FindFailDialog;
}

/**
 * @brief The FindFailDialog class
 * 搜索用户失败的弹出对话框
 */
class FindFailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindFailDialog(QWidget *parent = nullptr);
    ~FindFailDialog();

private slots:
    /**
     * @brief on_find_fail_sure_btn_clicked
     * 点击确认触发的槽函数
     */
    void on_find_fail_sure_btn_clicked();

private:
    Ui::FindFailDialog *ui;
};

#endif // FINDFAILDIALOG_H
