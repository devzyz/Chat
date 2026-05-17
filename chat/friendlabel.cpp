#include "friendlabel.h"
#include "ui_friendlabel.h"

FriendLabel::FriendLabel(QWidget *parent)
    : QFrame(parent)
    , ui(new Ui::FriendLabel)
{
    ui->setupUi(this);

    ui->close_label->SetState("normal_leave", "normal_hover", "normal_press",
                              "select_leave", "select_hover", "select_press");

    connect(ui->close_label, &ClickedLabel::clicked, this, &FriendLabel::slot_chose);
}

FriendLabel::~FriendLabel()
{
    delete ui;
}

// 设置文本信息，并根据文本以及字体等信息，计算并设置整个friendlabel控件的长度和宽度
void FriendLabel::SetText(QString text)
{
    _text = text;
    ui->tip_label->setText(_text);
    ui->tip_label->adjustSize();

    // 获取QLabel控件的字体信息
    QFontMetrics fontMetrics(ui->tip_label->font());
    // auto textWidth = fontMetrics.horizontalAdvance(ui->tip_label->text());
    auto textHeight = fontMetrics.height();

    this->setFixedWidth(ui->tip_label->width() + ui->close_label->width() + 5);
    this->setFixedHeight(textHeight + 2);

    _width = this->width();
    _height = this->height();
}

int FriendLabel::Width()
{
    return _width;
}

int FriendLabel::Height()
{
    return _height;
}

QString FriendLabel::Text()
{
    return _text;
}

void FriendLabel::slot_chose()
{

}
