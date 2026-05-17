#include "chatuserwidget.h"
#include "ui_chatuserwidget.h"

ChatUserWidget::ChatUserWidget(QWidget *parent)
    : ListItemBase(parent)
    , ui(new Ui::ChatUserWidget)
{
    ui->setupUi(this);
    // 设置当前ListItemType的类型
    SetItemType(ListItemType::CHAT_USER_ITEM);
}

ChatUserWidget::~ChatUserWidget()
{
    delete ui;
}

/**
 * @brief ChatUserWidget::SetInfo
 * @param name 昵称
 * @param head 头像
 * @param msg 上次聊天记录
 *
 * 设置模板的参数
 */
void ChatUserWidget::SetInfo(QString name, QString head, QString msg) {
    _name = name;
    _head = head;
    _message = msg;

    // 加载head路径下的头像图片
    QPixmap pixmap(_head);

    // 将图片缩放为icon_label的大小，并显示
    ui->icon_label->setPixmap(pixmap.scaled(ui->icon_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_label->setScaledContents(true);
    // 更新用户名和上次聊天记录
    ui->user_name_label->setText(_name);
    ui->user_chat_label->setText(_message);
}
/**
 * @brief ChatUserWidget::sizeHint
 * @return
 * 返回默认尺寸大小
 */
QSize ChatUserWidget::sizeHint() const {
    return QSize(250, 70);
}
