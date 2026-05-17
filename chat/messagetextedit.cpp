#include "messagetextedit.h"
#include <QDebug>
#include <QMessageBox>


MessageTextEdit::MessageTextEdit(QWidget *parent)
    : QTextEdit(parent), _file_uid(0)
{
    this->setMaximumHeight(60);
}

MessageTextEdit::~MessageTextEdit()
{

}

QVector<MsgInfo> MessageTextEdit::getMsgList()
{
    _mGetMsgList.clear();
    // 获取输入框的纯文本
    QString doc = this->document()->toPlainText();
    QString text = ""; //存储文本信息
    int indexUrl = 0;
    int count = _mMsgList.size();

    // 枚举所有的字符
    for(int index = 0; index < doc.size(); index++)
    {
       // 如果当前字符不是纯文本，代表这里有一个图片或者文件
        if(doc[index] == QChar::ObjectReplacementCharacter)
        {
            if(!text.isEmpty())
            {
                // text非空，则将纯文本插入进去
                QPixmap pix; // 空的pix，因为插入的是纯文本
                insertMsgList(_mGetMsgList, "text", text, pix);
                text.clear();
            }

            // 从mMstList中查找是否有这个数据，按照顺序插入的，按照顺序转换
            if (indexUrl < _mMsgList.size()) {
                _mMsgList[indexUrl].content = _mMsgList[indexUrl].content.split('_').at(0);
                _mGetMsgList.append(_mMsgList[indexUrl]);
                indexUrl ++;
            }
        }
        else
        {
            // 是纯文本，插入到末尾
            text.append(doc[index]);
        }
    }

    if(!text.isEmpty())
    {
        QPixmap pix;
        insertMsgList(_mGetMsgList, "text", text, pix);
        text.clear();
    }
    _mMsgList.clear();
    this->clear(); // 清空输入框的内容
    _file_uid = 0;
    return _mGetMsgList;
}

// 拖拽进入事件
void MessageTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    // 在输入框内部拖拽，禁止
    if(event->source() == this)
        event->ignore();
    else
        event->accept();
}

/**
 * @brief MessageTextEdit::dropEvent
 * @param event
 * 拖拽的放下事件，将图片缩放后进行插入，并将原始图片路径保存到mMsgList内
 */
void MessageTextEdit::dropEvent(QDropEvent *event)
{
    insertFromMimeData(event->mimeData());
    event->accept();
}

// 通过enter的发送事件
/**
 * @brief MessageTextEdit::keyPressEvent
 * @param e
 *
 * 不允许鼠标选中，且不需要支持 Delete 键
 */
void MessageTextEdit::keyPressEvent(QKeyEvent * e)
{
    // 表示单独按下主键盘的enter或者小键盘的enter，同时没有按下shift建
    if((e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) && !(e->modifiers() & Qt::ShiftModifier))
    {
        emit send();
        return;
    }

    // 按下退格键
    if (e->key() == Qt::Key_Backspace) {
        QTextCursor cursor = textCursor();

        // 解析选中的内容，如果为图片，则额外进行一步删除
        QTextCharFormat fmt = cursor.charFormat();
        if (fmt.isImageFormat()) {
            QTextImageFormat imgFmt = fmt.toImageFormat();
            QString deletedName = imgFmt.name();

            // 从_mMsgList内删除
            auto it = std::find_if(_mMsgList.begin(), _mMsgList.end(), [&](const MsgInfo& info) {
                return info.content == deletedName;
            });

            if (it != _mMsgList.end()) {
                _mMsgList.erase(it);
            }
        }

        // 如果不是图片，则不需要更新_mMsgList的内容
        QTextEdit::keyPressEvent(e);
        return;
    }
    QTextEdit::keyPressEvent(e);
}

// 插入图片
void MessageTextEdit::insertImages(const QString &url)
{
    QImage image(url);
    //按比例缩放图片
    if(image.width()> 120 || image.height() > 80)
    {
        image = image.scaled(120, 80, Qt::KeepAspectRatio);
    }
    // 获取当前的光标位置
    QTextCursor cursor = this->textCursor();
    cursor.movePosition(QTextCursor::End);
    QString new_url = url + "_" + QString::number(_file_uid);
    _file_uid ++;

    qDebug() << "new_url" + new_url;

    // 插入到输入框中
    cursor.insertImage(image, new_url);
    setTextCursor(cursor);
    insertMsgList(_mMsgList, "image" , new_url, QPixmap::fromImage(image));
}

// 判断当前框是否愿意接受输入当前类型的数据
bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return QTextEdit::canInsertFromMimeData(source);
}

/**
 * @brief MessageTextEdit::insertFromMimeData
 * @param source
 * 将拖拽的内容插入
 */
void MessageTextEdit::insertFromMimeData(const QMimeData *source)
{
    // 取出url的内容
    QStringList urls = getUrl(source->text());

    qDebug() << urls;

    if(urls.isEmpty())
        return;

    // 枚举所有的urls
    foreach (QString url, urls)
    {
        if(isImage(url))
            insertImages(url);
    }
}

// 判断是否是图片
bool MessageTextEdit::isImage(QString url)
{
    QString imageFormat = "bmp,jpg,png,tif,gif,pcx,tga,exif,fpx,svg,psd,cdr,pcd,dxf,ufo,eps,ai,raw,wmf,webp";
    QStringList imageFormatList = imageFormat.split(",");
    // 获取后缀名
    QFileInfo fileInfo(url);
    QString suffix = fileInfo.suffix();
    // 判断后缀是否包含在里面
    if(imageFormatList.contains(suffix, Qt::CaseInsensitive)){
        return true;
    }
    return false;
}
// 插入文本
void MessageTextEdit::insertMsgList(QVector<MsgInfo> &list, QString flag, QString text, QPixmap pix)
{
    MsgInfo msg;
    msg.msgFlag = flag;
    msg.content = text;
    msg.pixmap = pix;
    list.append(msg);
}

// 从拖拽的文件路径中获取到对应的url
/**
 * @brief MessageTextEdit::getUrl
 * @param text
 * @return
 * file:///C:/photo.jpg
 * file:///C:/document.pdf
 * 可能存在多行，因此通过\n分割
 * 然后通过///分割出前后两个
 * 取at(1)取到后面的那一部分
 */
QStringList MessageTextEdit::getUrl(QString text)
{
    QStringList urls;
    if(text.isEmpty()) return urls;

    QStringList list = text.split("\n");
    foreach (QString url, list) {
        if(!url.isEmpty()){
            QStringList str = url.split("///");
            if(str.size() >= 2)
                urls.append(str.at(1));
        }
    }
    return urls;
}

void MessageTextEdit::textEditChanged()
{

}
