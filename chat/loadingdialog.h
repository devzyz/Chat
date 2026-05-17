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
    explicit LoadingDialog(QWidget *parent = nullptr);
    ~LoadingDialog();

private:
    Ui::LoadingDialog *ui;
};

#endif // LOADINGDIALOG_H
