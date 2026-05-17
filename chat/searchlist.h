#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <QListWidget>
#include <QWidget>
#include "loadingdialog.h"
#include "userdata.h"

/**
 * @brief The SearchList class
 * 搜索列表自定义的QWidgetList
 *
 */

class SearchList : public QListWidget
{
public:
    SearchList(QWidget * parent = nullptr);
    void CloseFindDialog();
    void SetSearchEdit(QWidget * edit);

protected:
    // 处理滚轮的一些变化
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    void waitPending(bool pending = true);
    void addTipItem();

    bool _send_pending;
    std::shared_ptr<QDialog> _find_dialog;
    QWidget * _search_edit;
    LoadingDialog * _loadingDialog;

private slots:
    void slot_item_clicked(QListWidgetItem * item);
    void slot_user_search(std::shared_ptr<SearchInfo> si);
};

#endif // SEARCHLIST_H
