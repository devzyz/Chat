#ifndef TEXTCHATBUBBLE_H
#define TEXTCHATBUBBLE_H

#include "chatbubble.h"
#include <QTextEdit>
#include "bubbleframe.h"

/**
 * @brief The TextChatBubble class
 * 文本聊天气泡类
 */
class TextChatBubble : public ChatBubble
{
    Q_OBJECT
public:
    /**
     * @brief TextChatBubble
     * @param role 类型
     * @param text 聊天内容
     * @param userName 用户昵称
     * @param userIcon 头像
     */
    TextChatBubble(ChatRole role, const QString &text, const QString& userName, const QString& userIcon, const ChatStatus& status, QWidget *parent = nullptr);

    /**
     * @brief setPlainText
     * @param text 要显示的纯文本。
     * 设置文本，并设置宽高等
     */
    void setPlainText(const QString &text);
    /** @brief 返回气泡内部文本编辑控件的借用指针，有效期不超过气泡生命周期。 */
    QTextEdit *textEdit() const { return _text_edit;}
protected:

private:

    QTextEdit* _text_edit;
    BubbleFrame* _wrapper;
};

#endif // TEXTCHATBUBBLE_H
