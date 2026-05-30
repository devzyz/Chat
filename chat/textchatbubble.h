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
     * @param parent
     */
    TextChatBubble(ChatRole role, const QString &text, const QString& userName, const QString& userIcon, QWidget *parent = nullptr);

    /**
     * @brief setPlainText
     * @param text
     * @param wrappper
     * 设置文本，并设置宽高等
     */
    void setPlainText(const QString &text);
    QTextEdit *textEdit() const { return _text_edit;}

protected:

private:

    QTextEdit* _text_edit;
    BubbleFrame* _wrapper;
};

#endif // TEXTCHATBUBBLE_H
