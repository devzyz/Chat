#ifndef TEXTCHATBUBBLE_H
#define TEXTCHATBUBBLE_H

#include "chatbubble.h"
#include <QTextEdit>

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

    void setPlainText(const QString &text, QWidget * wrappper);
    QTextEdit *textEdit() const { return _text_edit;}
    QSize sizeHint() const override;

protected:

private:
    void initStyleSheet(QWidget * wrapper);

    QTextEdit* _text_edit;
};

#endif // TEXTCHATBUBBLE_H
