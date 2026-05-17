#include "loadingdialog.h"
#include "ui_loadingdialog.h"
#include <QMovie>

LoadingDialog::LoadingDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoadingDialog)
{
    ui->setupUi(this);

    // 设置当前ui的格式，无边框、背景透明等
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // 当前窗口的尺寸跟父窗口一样大
    setFixedSize(parent->size());
    // 启动动画
    QMovie * movie = new QMovie(":/res/loading.gif");
    ui->loading_label->setMovie(movie);
    movie->start();
}

LoadingDialog::~LoadingDialog()
{
    delete ui;
}
