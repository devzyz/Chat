#ifndef GROUPTIPITEM_H
#define GROUPTIPITEM_H

#include <QWidget>
#include "listitembase.h"

namespace Ui {
class GroupTipItem;
}

/**
 * @brief The GroupTipItem class
 * 这是联系人列表的分类group
 * 一种是新的朋友group
 * 一种是已有联系人group
 *
 */
class GroupTipItem : public ListItemBase
{
    Q_OBJECT

public:
    explicit GroupTipItem(QWidget *parent = nullptr);
    ~GroupTipItem();
    /**
     * @brief sizeHint
     * @return
     * 本质他也是作为QListWidgetItem插入的，所以也要重写sizeHint
     * 来设置QListWidgetItem的大小匹配GroupTipItem的大小
     */
    QSize sizeHint() const override;
    /**
     * @brief SetGroupTip
     * @param str
     * 设置本grouptip的标题
     */
    void SetGroupTip(QString str);

private:
    Ui::GroupTipItem *ui;
    // 本组的标题
    QString _tip;
};

#endif // GROUPTIPITEM_H
