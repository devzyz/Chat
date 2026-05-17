#include "bubbleframe.h"
#include <QPainter>
#include <QDebug>
const int WIDTH_SANJIAO = 8;

BubbleFrame::BubbleFrame(ChatRole role, QWidget * parent) :
    QFrame(parent), m_role(role), m_margin(3){
    m_pHLayout = new QHBoxLayout();

    if (m_role == ChatRole::Self) {
        // 左，上，右，下。自己发消息，右边有一个三角，距离增大一下
        m_pHLayout->setContentsMargins(m_margin, m_margin, WIDTH_SANJIAO + m_margin, m_margin);
    }else {
        m_pHLayout->setContentsMargins(WIDTH_SANJIAO + m_margin, m_margin, m_margin, m_margin);
    }

    this->setLayout(m_pHLayout);
}

void BubbleFrame::setWidget(QWidget * w) {
    if (m_pHLayout->count() > 0) {
        return ;
    }else {
        m_pHLayout->addWidget(w);
    }
}

void BubbleFrame::paintEvent(QPaintEvent * e) {
    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    if (m_role == ChatRole::Other) {
        QColor bk_color(Qt::white);
        painter.setBrush(QBrush(bk_color));

        // 左侧留下WIDTH_SANJIAO的长度，用于画三角
        QRect bk_rect = QRect(WIDTH_SANJIAO, 0, this->width()-WIDTH_SANJIAO, this->height());
        painter.drawRoundedRect(bk_rect, 5, 5); // 画矩形，圆角半径为5

        // 画三角
        QPointF points[3] = {
            QPointF(bk_rect.x(), 12),
            QPointF(bk_rect.x(), 12 + WIDTH_SANJIAO),
            QPointF(bk_rect.x() - WIDTH_SANJIAO, 12 + WIDTH_SANJIAO / 2),
        };
        painter.drawPolygon(points, 3);
    }else {
        QColor bk_color(158, 234, 106);
        painter.setBrush(QBrush(bk_color));

        // 画一个矩形，从左上角开始，高度跟this高度相同，宽度要减去三角形的宽度
        QRect bk_rect = QRect(0, 0, this->width()-WIDTH_SANJIAO, this->height());
        painter.drawRoundedRect(bk_rect, 5, 5);

        // 画三角
        QPointF points[3] = {
            QPointF(bk_rect.x() + bk_rect.width(), 12),
            QPointF(bk_rect.x() + bk_rect.width(), 12 + WIDTH_SANJIAO),
            QPointF(bk_rect.x() + bk_rect.width() + WIDTH_SANJIAO, 12 + WIDTH_SANJIAO / 2),
        };
        painter.drawPolygon(points, 3);
    }

    return QFrame::paintEvent(e);
}
