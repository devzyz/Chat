#include "findfaildialog.h"
#include "logmgr.h"
#include "ui_findfaildialog.h"

FindFailDialog::FindFailDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FindFailDialog)
{
    ui->setupUi(this);
    setWindowTitle("添加");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    ui->find_fail_sure_btn->SetState("normal", "hover", "press");
    this->setModal(true);
}

FindFailDialog::~FindFailDialog()
{
    SPDLOG_DEBUG("FindFailDialog destructed");
    delete ui;
}

void FindFailDialog::on_find_fail_sure_btn_clicked()
{
    this->hide();
}

