#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <QListWidget>
#include <QWidget>
#include "loadingdialog.h"
#include "userdata.h"

/** @brief 发起用户搜索并根据结果展示好友资料或申请入口。 */
class SearchList : public QListWidget
{
    Q_OBJECT
public:
    /** @brief 创建搜索列表并连接用户查询结果。 */
    SearchList(QWidget * parent = nullptr);
    /**
     * @brief closeFindDialog
     * 关闭搜索弹出框
     */
    void closeFindDialog();
    /** @brief 借用搜索输入控件，调用期间要求控件仍由页面持有。 */
    void setSearchEdit(QWidget * edit);

protected:
    // 处理滚轮的一些变化
    /** @brief 观察关联对象事件并处理本控件负责的交互，其余事件交回 Qt。 */
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    /** @brief 进入等待时创建提示并锁定搜索，结束时销毁提示并允许下一次搜索。 */
    void waitPending(bool pending = true);
    /** @brief 添加不可选间隔项及添加用户入口。 */
    void addTipItem();

    bool _send_pending;
    std::shared_ptr<QDialog> _find_dialog;
    QWidget * _search_edit;
    LoadingDialog * _loadingDialog;

signals:
    /**
     * @brief chatRequested
     * 搜索后，如果搜索到的是自己的好友，则进行跳转
     */
    void chatRequested(std::shared_ptr<SearchInfo>);

private slots:
    /** @brief 根据点击条目类型发起搜索或关闭结果对话框。 */
    void itemClicked(QListWidgetItem * item);
    /** @brief 展示查询结果；已是好友时允许跳转私聊。 */
    void tcpSearchUserFinish(std::shared_ptr<SearchInfo> si);
};

#endif // SEARCHLIST_H
