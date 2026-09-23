#ifndef MESSAGETEXTEDIT_H
#define MESSAGETEXTEDIT_H

#include <QObject>
#include <QTextEdit>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMimeType>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QPainter>
#include <QVector>
#include "global.h"

/**
 * @brief The MessageTextEdit class
 * 聊天界面自定义输入框
 *
 * 图片通过拖拽事件添加
 */
class MessageTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    /** @brief 初始化对象，用于编辑文字和内嵌图片并拆分待发送消息。 */
    explicit MessageTextEdit(QWidget *parent = nullptr);

    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~MessageTextEdit();

    /** @brief 返回当前拆分的待发送消息列表，调用方据此生成发送请求。 */
    QVector<MsgInfo> getMsgList();
signals:
    /** @brief 将消息请求排队落盘，持久化完成后再交给发送调度。 */
    void send();

protected:
    /** @brief 检查拖入内容是否可作为图片或文本接受。 */
    void dragEnterEvent(QDragEnterEvent *event);
    /** @brief 处理拖放的图片或文本并插入编辑器。 */
    void dropEvent(QDropEvent *event);
    /** @brief 处理发送快捷键及普通编辑按键。 */
    void keyPressEvent(QKeyEvent *e);

private:
    /** @brief 读取并插入选定图片作为编辑器内嵌资源。 */
    void insertImages(const QString &url);
    /** @brief 判断 MIME 数据是否包含可插入的文本、图片或本地资源。 */
    bool canInsertFromMimeData(const QMimeData *source) const;
    /** @brief 将剪贴板 MIME 数据按文本或图片插入编辑器。 */
    void insertFromMimeData(const QMimeData *source);

private:
    /** @brief 判断文件类型是否可作为图片插入。 */
    bool isImage(QString url);//判断文件是否为图片
    /** @brief 判断文件是否为图片。 */
    void insertMsgList(QVector<MsgInfo> &list,QString flag, QString text, QPixmap pix);

    /** @brief 从拖放或剪贴板数据中提取资源地址。 */
    QStringList getUrl(QString text);

private slots:
    /** @brief 响应编辑内容变化并维护输入区域状态。 */
    void textEditChanged();

private:
    QVector<MsgInfo> _mMsgList; // 图片暂时保存的位置
    QVector<MsgInfo> _mGetMsgList; // 外界获取信息时，整个输入框内容的保存位置
    int _file_uid;
};
#endif // MESSAGETEXTEDIT_H
