#include "chatpage.h"
#include "ui_chatpage.h"
#include <QStyleOption>
#include <QPainter>
#include "textchatbubble.h"
#include "picturechatbubble.h"
#include "usermgr.h"
#include <QJsonDocument>
#include "tcpmgr.h"

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

void ChatPage::SetChatInfo(std::shared_ptr<FriendInfo> friend_info)
{
    _chat_info = friend_info;
    ui->title_label->setText(_chat_info->_name);

    ui->chat_detail_data_list->removeAllItem();
    for (auto & msg : _chat_info->_chat_msgs) {
        AppendChatMsg(msg);
    }
}

// 往聊天记录显示列表里面添加数据
void ChatPage::AppendChatMsg(std::shared_ptr<TextChatData> msg_data)
{
    auto self_info = UserMgr::GetInstance()->GetUserInfo();
    ChatRole role;
    if (msg_data->_from_uid == self_info->_uid) {
        role = ChatRole::Self;

        QWidget * pBubble = nullptr;
        pBubble = new TextChatBubble(role, msg_data->_msg_content, self_info->_name, self_info->_icon);
        ui->chat_detail_data_list->appendChatItem(pBubble);
    }else {
        role = ChatRole::Other;

        if (_chat_info == nullptr) {
            return;
        }
        QWidget * pBubble = nullptr;
        pBubble = new TextChatBubble(role, msg_data->_msg_content, _chat_info->_name, _chat_info->_icon);
        ui->chat_detail_data_list->appendChatItem(pBubble);
    }
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
 * 文本数据，可以通过网络发送到对端
 * 图片数据，本地气泡可绘制，无法通过网络发送 待做 todo...
 * 文件数据 待做 todo...
 *
 * 聊天数据需要添加到三个地方
 * 其一，通过tcp请求，发往对端
 * 其二，添加到聊天显示QListWidget内
 * 其三，添加到聊天记录本地内存中
 */
void ChatPage::on_send_btn_clicked()
{
    if (_chat_info == nullptr) {
        qDebug() << "on send btn clicked _user_info is empty";
        return ;
    }
    auto self_info = UserMgr::GetInstance()->GetUserInfo();
    auto pTextEdit = ui->chat_edit;
    ChatRole role = ChatRole::Self;
    QString userName = self_info->_name;
    QString userIcon = self_info->_icon;

    // 获取到输入框内的所有待发送数据,每一个MsgInfo作为一条数据发出
    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();

    // 因为如果发送的信息很短，每次都调用网络发送会占用空间
    // 因此通过textArray来进行累计，当text_length发送长度超过1k的时候，将所有的文本信息打包一起发送
    int text_length = 0;
    QJsonObject textObj;
    QJsonArray textArray;

    for (int i = 0; i < msgList.size(); i ++ ) {
        // 判断是否发送的太长了最大为1k
        if (msgList[i].content.length() > 1024) {
            continue;
        }

        QString type = msgList[i].msgFlag;

        QWidget * pBubble = nullptr;
        if (type == "text") {
            // 这里是绘制到本地界面上
            pBubble = new TextChatBubble(role, msgList[i].content, userName, userIcon);

            // 为每个消息生成唯一的id
            QUuid uuid = QUuid::createUuid();
            QString uuid_string = uuid.toString();

            // 足够长了，则打包一起发送
            if (text_length + msgList[i].content.length() > 1024) {
                textObj["from_uid"] = self_info->_uid;
                textObj["to_uid"] = _chat_info->_uid;
                textObj["text_array"] = textArray;
                QJsonDocument doc(textObj);
                QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
                // 将整个包发送
                text_length = 0;
                textArray = QJsonArray();
                textObj = QJsonObject();

                // 发送tcp请求
                emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
            }

            // 不够长，则进行累计
            text_length += msgList[i].content.length();
            // 将本次要发送的文本数据打包放入textArray中
            QJsonObject obj;
            QByteArray utf8Message = msgList[i].content.toUtf8();
            obj["msg_content"] = QString::fromUtf8(utf8Message);
            obj["msg_id"] = uuid_string;
            textArray.append(obj);

            // 然后将发送的聊天记录保存到本地
            auto text_msg = std::make_shared<TextChatData> (self_info->_uid, _chat_info->_uid,
                                                           uuid_string, obj["msg_content"].toString());
            // 发出信号，添加聊天记录到本地
            emit sig_append_send_text_chat_msg(text_msg);
        }else if (type == "image") {
            // todo... 发送图片
            pBubble = new PictureChatBubble(role, QPixmap(msgList[i].content), userName, userIcon);
        }else if (type == "file") {
            // todo... 发送文件
        }

        // 添加数据到聊天显示区域
        if (pBubble != nullptr) {
            ui->chat_detail_data_list->appendChatItem(pBubble);
        }
    }

    // 还有未发完的
    if (text_length > 0) {
        //发送给服务器
        textObj["text_array"] = textArray;
        textObj["from_uid"] = self_info->_uid;
        textObj["to_uid"] = _chat_info->_uid;
        QJsonDocument doc(textObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        //发送tcp请求
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
    }
}

