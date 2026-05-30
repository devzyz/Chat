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
    Q_OBJECT
public:
    SearchList(QWidget * parent = nullptr);
    /**
     * @brief CloseFindDialog
     * 关闭搜索弹出框
     */
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

signals:
    /**
     * @brief sig_jump_chat_item
     * 搜索后，如果搜索到的是自己的好友，则进行跳转
     */
    void sig_jump_chat_item(std::shared_ptr<SearchInfo>);

private slots:
    void slot_item_clicked(QListWidgetItem * item);
    /**
     * @brief slot_search_user_finish
     * @param si
     * 搜索用户的tcp网络请求结束
     */
    void slot_tcp_search_user_finish(std::shared_ptr<SearchInfo> si);
};

#endif // SEARCHLIST_H
