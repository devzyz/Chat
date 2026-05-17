#include "chatdialog.h"
#include "ui_chatdialog.h"
#include <QAction>
#include <QRandomGenerator>
#include "chatuserwidget.h"
#include "loadingdialog.h"
#include <QThread>
#include <QMouseEvent>

ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent), _mode(ChatUIMode::ChatMode), _state(ChatUIMode::ChatMode)
    , ui(new Ui::ChatDialog), _b_loading(false)
{
    ui->setupUi(this);

    // 添加按钮的高亮设置
    ui->add_btn->SetState("normal", "hover", "press");

    // 搜索框最大长度限制
    ui->search_edit->SetMaxLength(15);

    QAction *searchAction = new QAction(ui->search_edit);
    searchAction->setIcon(QIcon(":/res/search.png"));
    ui->search_edit->addAction(searchAction, QLineEdit::LeadingPosition);
    // 设置默认文本
    ui->search_edit->setPlaceholderText(QStringLiteral("搜索"));

    QAction *clearAction = new QAction(ui->search_edit);
    clearAction->setIcon(QIcon(":/res/close_transport.png"));
    // 初始时不显示清除图标，将清除动作添加到LineEdit的末尾
    ui->search_edit->addAction(clearAction, QLineEdit::TrailingPosition);

    // 当需要显示清除图标时，更改为实际的清除图标
    connect(ui->search_edit, &QLineEdit::textChanged, [clearAction](const QString& text) {
        if (!text.isEmpty()) {
            clearAction->setIcon(QIcon(":/res/close_search.png"));
        }else {
            clearAction->setIcon(QIcon(":/res/close_transport.png"));
        }
    });

    // 点击清除图标后的逻辑
    // 清空输入框，清除图标删除，失去焦点
    connect(clearAction, &QAction::triggered, [this, clearAction]() {
        ui->search_edit->clear();
        clearAction->setIcon(QIcon(":/res/close_transport.png"));
        ui->search_edit->clearFocus();

        // 隐藏搜索列表
        ShowSearch(false);
    });

    // 默认隐藏
    ShowSearch(false);
    // 连接加载新聊条记录的信号与槽
    connect(ui->chat_user_list, &ChatUserList::sig_loading_chat_user, this, &ChatDialog::slot_loading_chat_user);
    // 添加聊天测试数据，测试数据，后面需要通过tcp补充
    addChatUserList();

    // 将头像设置上去
    QPixmap pixmap(":/res/head_1.jpg");
    pixmap = pixmap.scaled(ui->side_head_label->size(), Qt::KeepAspectRatio);
    ui->side_head_label->setPixmap(pixmap);
    ui->side_head_label->setScaledContents(true);

    ui->side_chat_label->SetState("leave", "hover", "select");

    ui->side_user_label->SetState("leave", "hover", "select");

    AddLabelGroup(ui->side_chat_label);
    AddLabelGroup(ui->side_user_label);

    // 绑定list切换
    connect(ui->side_chat_label, &StateWidget::clicked, this, &ChatDialog::slot_midlist_to_chat_list);
    connect(ui->side_user_label, &StateWidget::clicked, this, &ChatDialog::slot_midlist_to_user_list);

    // 链接搜索框的显示，当搜索内容不空时，显示出搜索框
    connect(ui->search_edit, &QLineEdit::textChanged, this, &ChatDialog::slot_search_edit_text_changed);

    // 安装事件过滤器，检测鼠标点击位置，清空搜索框
    this->installEventFilter(this);

    // 设置聊天为默认选中界面
    ui->side_chat_label->SetSelected(true);
}

ChatDialog::~ChatDialog()
{
    delete ui;
}

// 临时的聊天记录
std::vector<QString> messages = {"See you tomorrow",
                                 "Let's catch up soon",
                                 "Don't forget the meeting",
                                 "Haha that's funny",
                                 "Thanks for your help"};
std::vector<QString> names = {"Alice", "Bob", "Charlie", "Diana", "Ethan"};
std::vector<QString> heads = {":/res/head_1.jpg",
                              ":/res/head_2.jpg",
                              ":/res/head_3.jpg",
                              ":/res/head_5.jpg"};

/**
 * @brief ChatDialog::addChatUserList
 * 聊天列表测试，手动创建几个列表添加上去
 */
void ChatDialog::addChatUserList()
{
    // 创建ChatUserList
    for (int i = 0; i < 13; i ++ ) {
        // 生成0~99之间的随机整数
        int randomValue = QRandomGenerator::global()->bounded(100);

        int message_i = randomValue % messages.size();
        int head_i = randomValue % heads.size();
        int name_i = randomValue % names.size();

        // 创建自定义的ChatUserWidget
        auto * chat_user_widget = new ChatUserWidget();
        chat_user_widget->SetInfo(names[name_i], heads[head_i], messages[message_i]);

        // 创建一个能够往QListWidget内部填充的Item
        QListWidgetItem * item = new QListWidgetItem();
        // 将item的大小设置为自定义的widget的大小
        item->setSizeHint(chat_user_widget->sizeHint());
        // 将这个item放到QListWidget内部，然后将这个item设置成我自定义的widget
        ui->chat_user_list->addItem(item);
        ui->chat_user_list->setItemWidget(item, chat_user_widget);
    }
}

/**
 * @brief ChatDialog::eventFilter
 * @param watched
 * @param event
 * @return
 * 事件过滤器，实现当处于search状态，并且点击非search list区域时
 * 将search列表切换为上一次显示的列表，并清空搜索框
 */
bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent * mouseEvent = static_cast<QMouseEvent*> (event);
        handleGlobalMousePress(mouseEvent);
    }

    return QDialog::eventFilter(watched, event);
}

/**
 * @brief ChatDialog::ShowSearch
 * @param bsearch
 * 下方有三种形式，聊天列表，搜索列表，联系人列表
 */
void ChatDialog::ShowSearch(bool bsearch)
{
    if (bsearch) {
        ui->chat_user_list->hide();
        ui->contact_user_list->hide();
        ui->search_list->show();
        _mode = ChatUIMode::SearchMode;
    }else if (_state == ChatUIMode::ChatMode) {
        ui->chat_user_list->show();
        ui->contact_user_list->hide();
        ui->search_list->hide();
        _mode = ChatUIMode::ChatMode;
    }else if (_state == ChatUIMode::ContactMode) {
        ui->chat_user_list->hide();
        ui->contact_user_list->show();
        ui->search_list->hide();
        _mode = ChatUIMode::ContactMode;
    }
}

/**
 * @brief ChatDialog::AddLabelGroup
 * @param label
 * 用一个列表存储左侧在同一个组里的StateWdiget组件
 * 因为右边的显示区域只会显示一个
 */
void ChatDialog::AddLabelGroup(StateWidget *label)
{
    _label_list.push_back(label);
}

// 清除其他stateWidget的选中状态，为了保持只有一个被选中
void ChatDialog::ClearLabelState(StateWidget *label)
{
    for (auto & ele : _label_list) {
        if (ele == label) {
            continue;
        }
        ele->ClearState();
    }
}

/**
 * @brief ChatDialog::handleGlobalMousePress
 * @param event
 * 为了处理，当正处在搜索页面的时候，点击其他位置，可以关闭搜索的窗口
 */
void ChatDialog::handleGlobalMousePress(QMouseEvent *event)
{
    // 如果不是搜索模式，则直接返回
    if (_mode != ChatUIMode::SearchMode) {
        return ;
    }

    // 获取相对于整个窗口的坐标，并转换为相对于搜索列表下的坐标
    QPointF posInSearchList = ui->search_list->mapFromGlobal(event->globalPosition());

    // 判断点是不是在搜索列表范围内
    if (!ui->search_list->rect().toRectF().contains(posInSearchList)) {
        // 不在范围内，清空搜索框
        ui->search_edit->clear();
        ShowSearch(false);
    }
}

/**
 * @brief ChatDialog::slot_loading_chat_user
 * 加載更多用戶聊天
 * 通过tcp请求来加载，现在进行模拟
 * todo...
 */
void ChatDialog::slot_loading_chat_user()
{
    // 判断当前是否在加载
    if (_b_loading) {
        return ;
    }

    _b_loading = true;
    // 创建一个加载的动画
    LoadingDialog * loadingDialog = new LoadingDialog(this);
    loadingDialog->setModal(true); // 拦截所有的鼠标和键盘事件
    loadingDialog->show();
    // QThread::sleep(2);

    addChatUserList();
    // 加载完毕后关闭对话框
    loadingDialog->deleteLater();

    _b_loading = false;
}

/**
 * @brief ChatDialog::slot_side_chat
 * 点击左侧聊天后，列表切换到聊天记录列表
 */
void ChatDialog::slot_midlist_to_chat_list()
{
    qDebug() << "receive side chat clicked";
    // 传入聊天StateWidget
    ClearLabelState(ui->side_chat_label);
    // 设置右面为聊天界面
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    _state = ChatUIMode::ChatMode;
    ShowSearch(false);
}

/**
 * @brief ChatDialog::slot_side_user
 * 点击左侧联系人后，列表切换到联系人列表
 */
void ChatDialog::slot_midlist_to_user_list()
{
    qDebug() << "receive side user clicked";
    ClearLabelState(ui->side_user_label);
    // 设置右面为好友申请列表
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
    _state = ChatUIMode::ContactMode;
    ShowSearch(false);
}

// 如果搜索框不空，则显示搜索列表
void ChatDialog::slot_search_edit_text_changed(const QString &str)
{
    if (!str.isEmpty()) {
        ShowSearch(true);
    }
}

