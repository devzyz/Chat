#ifndef CHATVIEW_H
#define CHATVIEW_H
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QPaintEvent>

/**
 * @brief The ChatView class
 * 详细聊天记录的显示区域
 */
class ChatView : public QWidget
{
    Q_OBJECT
public:
    ChatView(QWidget* parent = nullptr);
    void appendChatItem(QWidget * item); // 尾插
    void prependChatItem(QWidget * item); // 头插
    void insertChatItem(QWidget * before, QWidget * item); // 中间插

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    virtual void paintEvent(QPaintEvent * event) override;

private slots:
    void onVScrollBarMoved(int min, int max);

private:
    // 垂直布局
    QVBoxLayout * m_pVl;
    // 滚动区域
    QScrollArea * m_pScrollArea;
    // 用于控制是否可以添加，因为如果滚动的太快，就会出现一次性加载很多数据的情况
    // 通过一个布尔变量来进行约束，间隔一段时间后才可以再次加载数据
    bool isAppended;
};

#endif // CHATVIEW_H
