#include "chatpage.h"
#include "ui_chatpage.h"
#include <QStyleOption>
#include <QPainter>
#include "chatitembase.h"
#include "textbubble.h"
#include "picturebubble.h"
#include "textchatbubble.h"

ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatPage)
{
    ui->setupUi(this);

    // 设置按钮样式
    ui->receive_btn->SetState("normal", "hover", "press");
    ui->send_btn->SetState("normal", "hover", "press");

    // 设置图标样式
    ui->emoij_label->SetState("normal", "hover", "press", "normal", "hover", "press");
    ui->file_label->SetState("normal", "hover", "press", "normal", "hover", "press");
}

ChatPage::~ChatPage()
{
    delete ui;
}

// 让 ChatPage 能完整显示样式表定义的背景颜色、背景图片、边框
void ChatPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

/**
 * @brief ChatPage::on_send_btn_clicked
 * 点击发送数据所做的操作
 */
void ChatPage::on_send_btn_clicked()
{
    auto pTextEdit = ui->chat_edit;
    ChatRole role = ChatRole::Self;
    QString userName = QStringLiteral("111");
    QString userIcon = ":/res/head_1.jpg";

    // 获取到输入框内的所有待发送数据,每一个MsgInfo作为一条数据发出
    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    for (int i = 0; i < msgList.size(); i ++ ) {
        QString type = msgList[i].msgFlag;
        // ChatItemBase * pChatItem = new ChatItemBase(role);
        // pChatItem->setUserName(userName);
        // pChatItem->setUserIcon(QPixmap(userIcon));

        QWidget * pBubble = nullptr;
        if (type == "text") {
            pBubble = new TextChatBubble(role, msgList[i].content, userName, userIcon);
        }else if (type == "image") {
            pBubble = new PictureBubble(QPixmap(msgList[i].content), role);
        }else if (type == "file") {

        }

        if (pBubble != nullptr) {
            ui->chat_data_list->appendChatItem(pBubble);
        }
    }
}

