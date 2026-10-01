#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <QListWidget>
#include <QWidget>
#include <QPointer>
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
    /** @brief 展示搜索结果，页面根据业务身份决定跳转。 */
    void tcpSearchUserFinish(std::shared_ptr<SearchInfo> si);
    /** @brief 根据搜索流程状态显示或销毁等待提示。 */
    void waitPending(bool pending);
    /** @brief 用户关闭等待窗或离开搜索界面时请求取消当前查询。 */
    void cancelSearch();

private:

    /** @brief 添加不可选间隔项及添加用户入口。 */
    void addTipItem();

    bool _send_pending;
    QPointer<QDialog> _find_dialog;
    QPointer<LoadingDialog> _loadingDialog;

signals:
    /** @brief 用户取消当前查询，控制器应终止等待并使编号失效。 */
    void searchCancelled();
    /** @brief 用户请求查找，查询文本由页面提供。 */
    void searchRequested();
    /**
     * @brief chatRequested
     * 搜索后，如果搜索到的是自己的好友，则进行跳转
     */
    void chatRequested(std::shared_ptr<SearchInfo>);

private slots:
    /** @brief 根据点击条目类型发起搜索或关闭结果对话框。 */
    void itemClicked(QListWidgetItem * item);

};

#endif // SEARCHLIST_H
