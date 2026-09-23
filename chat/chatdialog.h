#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include "global.h"
#include "statewidget.h"
#include "userdata.h"
#include "messagerecord.h"
#include <QListWidgetItem>

namespace Ui {
class ChatDialog;
}

/** @brief 组织账号内的会话、联系人和聊天页面，响应网络结果与界面导航。 */
class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    /** @brief 创建聊天界面并连接账号、消息和会话列表事件。 */
    explicit ChatDialog(QWidget *parent = nullptr);
    /** @brief 释放本对象持有的界面或运行资源，Qt 子对象按所有权关系清理。 */
    ~ChatDialog();
    /** @brief 按当前账号和分页游标请求会话列表。 */
    void loadChatUserList();

protected:
    /**
     * @brief eventFilter
     * @return
     * 处理点击位置，因为要调用handleGlobalMousePress
     */
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    /** @brief 切换搜索结果列表与常规列表的可见状态。 */
    void showSearch(bool bsearch = false);
    // 将stateWidget添加到_label_list组内
    /** @brief 将stateWidget添加到_label_list组内。 */
    void addLabelGroup(StateWidget * label);
    // 清空组内元素
    /** @brief 清空组内元素。 */
    void clearLabelState(StateWidget * label);
    // 处理全局的点击，当在搜索界面，点击其他位置时，退出搜索界面，返回到上一次显示的界面
    /** @brief 处理全局的点击，当在搜索界面，点击其他位置时，退出搜索界面，返回到上一次显示的界面。 */
    void handleGlobalMousePress(QMouseEvent * event);
    /** @brief 按对方 UID 选中会话项，未指定时选中首项。 */
    void setSelectChatItem(int uid = 0);
    /** @brief 按对方 UID 展示聊天页，未指定时使用首项。 */
    void setSelectChatPage(int uid = 0);
    // 加载更多聊天记录
    /** @brief 加载更多聊天记录。 */
    void tcpLoadingMoreChatMsg(int chatId, qint64 beforeMessageId);
    /** @brief 追加一页联系人并推进账号缓存的分页游标。 */
    void loadingMoreContact();
    // 当搜索到聊天或者是从好友列表点击聊天后，如果在当前item中找不到
    // 则触发tcp请求，去服务器拉取或者创建新的聊天
    /** @brief 发送创建或获取双方私聊会话的请求，附带可选对方资料。 */
    void loadOncePrivateChat(int self_id, int other_id, QJsonObject json);
    // 添加新的会话到聊天列表中
    /** @brief 添加新的会话到聊天列表中。 */
    void addNewChat(std::shared_ptr<ChatInfo> chat_info);

    Ui::ChatDialog *ui;
    ChatUIMode _mode; // 当前的模式
    ChatUIMode _state; // 需要切换为的模式
    bool _b_chat_loading; // 是否在加载聊天列表
    bool _b_contact_loading; // 是否在加载联系人列表

    // 用于保存侧边栏中，已添加的StateWidget，因为每次只能选中一个
    QList<StateWidget * > _label_list;

    // 保存所有的聊天列表的item key = chat_id
    QMap<int, QListWidgetItem*> _chat_item_map;

    // 现在正在聊天的chat_id
    int _cur_chat_id;

    // 心跳定时器
private slots:
    // 加载更多聊天列表
    /** @brief 加载更多聊天列表。 */
    void loadingChatList();
    // 加载更多联系人列表
    /** @brief 加载更多联系人列表。 */
    void loadingContactList();
    // 切换到聊天
    /** @brief 切换到聊天。 */
    void midlistToChatList();
    // 切换到联系人
    /** @brief 切换到联系人。 */
    void midlistToUserList();
    /** @brief 根据搜索输入变化更新搜索列表显示状态。 */
    void searchEditTextChanged(const QString& str);
    // 切换到添加新朋友界面
    /** @brief 切换到添加新朋友界面。 */
    void switchApplyFriendListPage();

    // 添加新的会话到好友列表
    /** @brief 添加新的会话到好友列表。 */
    void tcpAddChatList(std::shared_ptr<ChatInfo>);
    /** @brief 从搜索结果打开已有私聊，必要时请求创建会话。 */
    void fromSearchJumpChatItem(std::shared_ptr<SearchInfo>);
    /** @brief 从好友资料打开私聊，必要时请求创建会话。 */
    void fromFriendJumpChatItem(std::shared_ptr<UserInfo>);
    /** @brief 切换到好友资料页并显示选中好友的信息。 */
    void switchFriendInfoPage(std::shared_ptr<UserInfo>);
    /** @brief 打开点击的会话并加载相应聊天内容。 */
    void chatItemClicked(QListWidgetItem *);
    /** @brief 将待发送消息加入对应会话的兼容缓存。 */
    void appendSendTextCacheMsg(QString, std::shared_ptr<ChatDataBase>);
    // 服务器通知我添加聊天数据，将数据刷新到聊天界面上
    /** @brief 服务器通知我添加聊天数据，将数据刷新到聊天界面上。 */
    void updateTextChatMsg(int , int , int , std::vector<std::shared_ptr<ChatDataBase>>& );
    // 切换右侧界面为setting界面
    /** @brief 切换右侧界面为setting界面。 */
    void switchUserInfoPage();
    /** @brief 将一页会话结果加入列表并恢复选中会话。 */
    void tcpLoadChatFinish(QJsonArray);
    /** @brief 将新建私聊加入列表并选中对应页面。 */
    void createPrivateChatFinish(std::shared_ptr<ChatInfo>);
    /** @brief 将历史消息页交给聊天页面合并。 */
    void tcpLoadingMoreChatFinish(int, std::vector<std::shared_ptr<ChatDataBase>>, bool, qint64);
    /** @brief 结束失败会话的历史加载状态并允许以后重试。 */
    void tcpLoadingMoreChatFailed(int);
    /** @brief 将服务端提交确认交给聊天页合并，不作为已读证据。 */
    void textChatMsgRspFinish(int, QVector<MessageAcknowledgement>);
    /** @brief 将失败 UUID 对应消息更新为可见失败状态。 */
    void textChatMsgFailed(int, QVector<QString>);
public slots:
    /** @brief 缓存新的好友申请并更新申请提醒。 */
    void tcpAddFriendApply(std::shared_ptr<ApplyInfo>);
};

#endif // CHATDIALOG_H
