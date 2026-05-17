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
    explicit MessageTextEdit(QWidget *parent = nullptr);

    ~MessageTextEdit();

    QVector<MsgInfo> getMsgList();
signals:
    void send();

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);
    void keyPressEvent(QKeyEvent *e);

private:
    void insertImages(const QString &url);
    bool canInsertFromMimeData(const QMimeData *source) const;
    void insertFromMimeData(const QMimeData *source);

private:
    bool isImage(QString url);//判断文件是否为图片
    void insertMsgList(QVector<MsgInfo> &list,QString flag, QString text, QPixmap pix);

    QStringList getUrl(QString text);

private slots:
    void textEditChanged();

private:
    QVector<MsgInfo> _mMsgList; // 图片暂时保存的位置
    QVector<MsgInfo> _mGetMsgList; // 外界获取信息时，整个输入框内容的保存位置
    int _file_uid;
};
#endif // MESSAGETEXTEDIT_H
