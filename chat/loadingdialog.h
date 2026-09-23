#ifndef LOADINGDIALOG_H
#define LOADINGDIALOG_H

#include <QDialog>

namespace Ui {
class LoadingDialog;
}

/**
 * @brief The LoadingDialog class
 * 用于显示正在加载动画的ui
 * 显示一个转圈的tif文件
 */
class LoadingDialog : public QDialog
{
    Q_OBJECT

public:
    /** @brief 初始化对象，用于展示异步操作等待提示。 */
    explicit LoadingDialog(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~LoadingDialog();

private:
    Ui::LoadingDialog *ui;
};

#endif // LOADINGDIALOG_H
