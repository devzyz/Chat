#ifndef APPLYFRIEND_H
#define APPLYFRIEND_H

#include <QDialog>
#include <userdata.h>
#include "clickedlabel.h"
#include "friendlabel.h"

namespace Ui {
class ApplyFriend;
}

class ApplyFriend : public QDialog
{
    Q_OBJECT

public:
    explicit ApplyFriend(QWidget *parent = nullptr);
    ~ApplyFriend();
    void InitTipLabels();
    void AddTipLabels(ClickedLabel *, QPoint cur_point, QPoint &next_point, int text_width, int text_height);
    void SetSearchInfo(std::shared_ptr<SearchInfo> si);
protected:
    bool eventFilter(QObject * obj, QEvent * event);

private:
    Ui::ApplyFriend *ui;
    void resetLabels();

    // 已经创建的标签
    QMap<QString, ClickedLabel*> _add_labels;
    std::vector<QString> _add_label_keys;

    // 用来在输入框显示添加信号有的标签
    QMap<QString, FriendLabel*> _friend_labels;
    std::vector<QString> _friend_label_keys;
    QPoint _label_point; // 编辑框的偏移

    void addLabel(QString name);
    std::vector<QString> _tip_data;
    QPoint _tip_cur_point;
    std::shared_ptr<SearchInfo> _si;

public slots:
    void slot_show_more_label();
    void slot_label_enter();
    void slot_remove_friend_label(QString);
    void slot_change_friend_label_by_tip(QString, ClickLabelState);
    void slot_label_text_change(const QString& text);
    void slot_add_edit_finished();
    void slot_add_friend_label_by_click_tip(QString text);
    void slot_apply_sure();
    void slot_apply_cancel();
};

#endif // APPLYFRIEND_H
