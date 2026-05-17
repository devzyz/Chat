#include "chatbubble.h"
#include "ui_chatbubble.h"
#include <QPainter>
#include <QBoxLayout>

ChatBubble::ChatBubble(ChatRole role, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Chat_Bubble_Widget)
    , _role(role)
{
    ui->setupUi(this);
    _chat_widget = ui->bubble_placeholder;

    QFont font("Microsoft YaHei");
    font.setPointSize(9);
    ui->user_name_label->setFont(font);
    ui->user_name_label->setFixedHeight(23);

    // 隐藏另一侧的头像，固定另一侧的弹簧
    if (_role == ChatRole::Self) {
        ui->left_head_widget->hide();
        ui->right_spacer->changeSize(10, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->user_name_label->setAlignment(Qt::AlignRight);
        ui->user_name_label->setContentsMargins(0, 0, 8, 0);
    } else {
        ui->right_head_widget->hide();
        ui->left_spacer->changeSize(10, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->user_name_label->setAlignment(Qt::AlignLeft);
        ui->user_name_label->setContentsMargins(8, 0, 0, 0);
    }
}

ChatBubble::~ChatBubble()
{
    delete ui;
}

/**
 * @brief ChatBubble::setWidget
 * @param w
 * 设置聊天具体的内容
 */
void ChatBubble::setWidget(QWidget *w)
{
    // 将原来占位的bubble替换下来，换上自定义的
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(ui->chat_content_widget->layout());
    int idx = layout->indexOf(_chat_widget);
    layout->removeWidget(_chat_widget);
    delete _chat_widget;
    _chat_widget = w;
    layout->insertWidget(idx, w);

    ui->chat_content_widget->layout()->invalidate();
    ui->horizontalLayout->invalidate();

    // 根据内部的布局得到真实的高度
    int idealHeight = ui->horizontalLayout->sizeHint().height();

    // 最小高度限制
    int minHeight = 45;

    // 锁定当前widget的高度
    this->setFixedHeight(qMax(idealHeight, minHeight));
}

/**
 * @brief ChatBubble::setUserName
 * @param name
 * 设置用户昵称
 */
void ChatBubble::setUserName(const QString &name)
{
    ui->user_name_label->setText(name);
}

/**
 * @brief ChatBubble::setUserIcon
 * @param icon
 * 设置头像
 */
void ChatBubble::setUserIcon(const QPixmap &icon)
{
    if (_role == ChatRole::Self)
        ui->right_head_label->setPixmap(icon);
    else
        ui->left_head_label->setPixmap(icon);
}
