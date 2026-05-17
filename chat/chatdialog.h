#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include "global.h"
#include "statewidget.h"

namespace Ui {
class ChatDialog;
}

class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatDialog(QWidget *parent = nullptr);
    ~ChatDialog();
    void addChatUserList();

protected:
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    void ShowSearch(bool bsearch = false);
    void AddLabelGroup(StateWidget * label);
    void ClearLabelState(StateWidget * label);
    void handleGlobalMousePress(QMouseEvent * event);
    Ui::ChatDialog *ui;
    ChatUIMode _mode;
    ChatUIMode _state;
    bool _b_loading;

    // 用于保存组内的元素
    QList<StateWidget * > _label_list;
private slots:
    void slot_loading_chat_user();
    // 切换到聊天
    void slot_midlist_to_chat_list();
    // 切换到联系人
    void slot_midlist_to_user_list();
    void slot_search_edit_text_changed(const QString& str);
};

#endif // CHATDIALOG_H
