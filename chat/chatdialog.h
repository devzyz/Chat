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

/**
 * @brief The ChatDialog class
 * 整个聊天界面的根本Dialog
 */
class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatDialog(QWidget *parent = nullptr);
    ~ChatDialog();
    // 加载聊天列表
    void LoadChatUesrList();

protected:
    /**
     * @brief eventFilter
     * @param watched
     * @param event
     * @return
     * 处理点击位置，因为要调用handleGlobalMousePress
     */
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    void ShowSearch(bool bsearch = false);
    // 将stateWidget添加到_label_list组内
    void AddLabelGroup(StateWidget * label);
    // 清空组内元素
    void ClearLabelState(StateWidget * label);
    // 处理全局的点击，当在搜索界面，点击其他位置时，退出搜索界面，返回到上一次显示的界面
    void handleGlobalMousePress(QMouseEvent * event);
    // 选中当前的item
    void SetSelectChatItem(int uid = 0);
    // 更新当前用户对应的聊天记录
    void SetSelectChatPage(int uid = 0);
    // 加载更多聊天记录
    void TcpLoadingMoreChatMsg(int chatId, qint64 beforeMessageId);
    // 加载更多联系人
    void LoadingMoreContact();
    // 当搜索到聊天或者是从好友列表点击聊天后，如果在当前item中找不到
    // 则触发tcp请求，去服务器拉取或者创建新的聊天
    void LoadOncePrivateChat(int self_id, int other_id, QJsonObject json);
    // 添加新的会话到聊天列表中
    void AddNewChat(std::shared_ptr<ChatInfo> chat_info);

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
    QTimer * _timer;
private slots:
    // 加载更多聊天列表
    void slot_loading_chat_list();
    // 加载更多联系人列表
    void slot_loading_contact_list();
    // 切换到聊天
    void slot_midlist_to_chat_list();
    // 切换到联系人
    void slot_midlist_to_user_list();
    void slot_search_edit_text_changed(const QString& str);
    // 切换到添加新朋友界面
    void slot_switch_apply_friend_list_page();

    // 添加新的会话到好友列表
    void slot_tcp_add_chat_list(std::shared_ptr<ChatInfo>);
    // 搜索到的是好友后，跳转到与该好友的聊天界面
    void slot_from_search_jump_chat_item(std::shared_ptr<SearchInfo>);
    // 在联系人详细信息界面点击聊天后，跳转到聊天
    void slot_from_friend_jump_chat_item(std::shared_ptr<UserInfo>);
    // 当点击联系人列表的某个联系人后，右侧跳转到好友详细信息界面
    void slot_switch_friend_info_page(std::shared_ptr<UserInfo>);
    // 聊天列表的item被点击后，触发的槽函数
    void slot_chat_item_clicked(QListWidgetItem *);
    // 将发送的文本，插入到聊天记录中
    void slot_append_send_text_cache_msg(QString, std::shared_ptr<ChatDataBase>);
    // 服务器通知我添加聊天数据，将数据刷新到聊天界面上
    void slot_update_text_chat_msg(int , int , int , std::vector<std::shared_ptr<ChatDataBase>>& );
    // 切换右侧界面为setting界面
    void slot_switch_user_info_page();
    // 聊天列表加载完成
    void slot_tcp_load_chat_finish(QJsonArray);
    // 创建私聊完成槽函数
    void slot_create_private_chat_finish(std::shared_ptr<ChatInfo>);
    // 增量加载聊天记录完成处理函数
    void slot_tcp_loading_more_chat_finish(int, std::vector<std::shared_ptr<ChatDataBase>>, bool, qint64);
    void slot_tcp_loading_more_chat_failed(int);
    // 发送消息后回后，在这里将聊天的未读信息进行更新
    void slot_text_chat_msg_rsp_finish(int, QVector<MessageAcknowledgement>);
    void slot_text_chat_msg_failed(int, QVector<QString>);
public slots:
    void slot_tcp_add_friend_apply(std::shared_ptr<ApplyInfo>);
};

#endif // CHATDIALOG_H
