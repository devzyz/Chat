#include "chatdetailwidget.h"
#include "ui_chatdetailwidget.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QStyleOption>

ChatDetailWidget::ChatDetailWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatDetailWidget)
{
    ui->setupUi(this);
}

ChatDetailWidget::~ChatDetailWidget()
{
    delete ui;
}

// 将listitem插入到QListWidget列表中
void ChatDetailWidget::appendChatItem(QWidget *chatitem)
{
    QListWidgetItem * listItem = new QListWidgetItem();
    // 将item的大小设置为自定义的widget的大小
    listItem->setSizeHint(chatitem->sizeHint());
    // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
    ui->chat_detail_list_widget->addItem(listItem);
    ui->chat_detail_list_widget->setItemWidget(listItem, chatitem);
}

void ChatDetailWidget::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

