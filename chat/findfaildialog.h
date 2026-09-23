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
    /** @brief 初始化对象，用于展示用户搜索未命中的结果。 */
    explicit FindFailDialog(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
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
