#include "bubbleframe.h"
#include <QPainter>
#include <QDebug>
const int WIDTH_SANJIAO = 8;

BubbleFrame::BubbleFrame(ChatRole role, QWidget * parent) :
    QFrame(parent), _role(role), _margin(4){
    // 创建一个水平布局
    _h_layout = new QHBoxLayout();

    // 设置内边距
    if (_role == ChatRole::Self) {
        // 左，上，右，下。自己发消息，右边有一个三角，距离增大一下
        _h_layout->setContentsMargins(_margin, _margin, WIDTH_SANJIAO + _margin, _margin);
    }else {
        _h_layout->setContentsMargins(WIDTH_SANJIAO + _margin, _margin, _margin, _margin);
    }

    this->setLayout(_h_layout);
}

// 只能够设置一次widget，因为对布局设置了内边距，因此这个widget会符合内边距的要求
// 然后在内边距的基础上进行气泡喷绘，保证文本一定在气泡内部
void BubbleFrame::setTextWidget(QWidget * text_edit) {
    if (_h_layout->count() > 0) {
        return ;
    }else {
        // 添加上聊天文本
        // 设置文本的对齐方式
        if (_role == ChatRole::Other) {
            _h_layout->addWidget(text_edit, 0, Qt::AlignLeft);
        } else {
            _h_layout->addWidget(text_edit, 0, Qt::AlignRight);
        }
    }
}

void BubbleFrame::setPictureWidget(QWidget *_picture_label)
{
    if (_h_layout->count() > 0) {
        return ;
    }else {
        // 设置文本的对齐方式
        if (_role == ChatRole::Other) {
            _h_layout->addWidget(_picture_label, 0, Qt::AlignLeft);
        } else {
            _h_layout->addWidget(_picture_label, 0, Qt::AlignRight);
        }
    }
}

void BubbleFrame::paintEvent(QPaintEvent * e) {
    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    // 对方
    if (_role == ChatRole::Other) {
        QColor background_color(Qt::white);
        painter.setBrush(QBrush(background_color)); // 设置颜色

        // 左侧留下WIDTH_SANJIAO的长度，用于画三角
        QRect background_rect = QRect(WIDTH_SANJIAO, 0, this->width() - WIDTH_SANJIAO, this->height());
        painter.drawRoundedRect(background_rect, 5, 5); // 画矩形，圆角半径为5

        // 画三角，根据三个顶点，绘制一个多边形
        QPointF points[3] = {
            QPointF(background_rect.x(), 12),
            QPointF(background_rect.x(), 12 + WIDTH_SANJIAO),
            QPointF(background_rect.x() - WIDTH_SANJIAO, 12 + WIDTH_SANJIAO / 2),
        };
        painter.drawPolygon(points, 3);
    }else {
        QColor background_color(158, 234, 106);
        painter.setBrush(QBrush(background_color));

        // 画一个矩形，从左上角开始，高度跟this高度相同，宽度要减去三角形的宽度
        QRect background_rect = QRect(0, 0, this->width() - WIDTH_SANJIAO, this->height());
        painter.drawRoundedRect(background_rect, 5, 5);

        // 画三角，根据三个顶点，绘制一个多边形
        QPointF points[3] = {
            QPointF(background_rect.x() + background_rect.width(), 12),
            QPointF(background_rect.x() + background_rect.width(), 12 + WIDTH_SANJIAO),
            QPointF(background_rect.x() + background_rect.width() + WIDTH_SANJIAO, 12 + WIDTH_SANJIAO / 2),
        };
        painter.drawPolygon(points, 3);
    }
}
