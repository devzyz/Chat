#ifndef GLOBAL_H
#define GLOBAL_H

#include <QWidget>
#include <functional>
#include "Qstyle"
#include <QRegularExpression>
#include <memory>
#include <iostream>
#include <mutex>
#include <QByteArray>
#include <QJsonObject>
#include <QDir>
#include <QSettings>
#include <QString>

/**
 * @brief repolish 用于刷新qss
 */
extern std::function<void(QWidget*)> repolish;

/**
 * @brief xorString 用于对密码进行哈希，简单哈希
 */
extern std::function<QString(QString)> xorString;

enum ReqId {
    ID_GET_VERIFY_CODE = 1001, // 获取验证码
    ID_REG_USER = 1002, // 注册用户
    ID_RESET_PWD = 1003, // 重置密码
    ID_LOGIN_UESR = 1004, // 用户登录
    ID_CHAT_LOGIN = 1005, // 登录聊天服务器
    ID_CHAT_LOGIN_RSP = 1006, // 登录聊天服务器回包
};

enum Modules {
    REGISTERMOD = 0,
    RESETMOD = 1,
    LOGINMOD = 2,
};

enum ErrorCodes {
    SUCCESS = 0,
    ERR_JSON = 1, // json解析失败
    ERR_NETWORK = 2, // 网络错误
};

enum TipErr {
    TIP_SUCCESS = 0, // 成功
    TIP_EMAIL_ERR = 1, // 邮件错误
    TIP_PWD_ERR = 2, // 密码格式错误
    TIP_CONFIRM_ERR = 3, // 确认密码格式错误
    TIP_PWD_CONFIRM = 4, // 密码与确认密码不匹配
    TIP_VARIFY_ERR = 5, // 验证码错误
    TIP_USER_ERR = 6 // 用户错误
};

enum ClickLabelState {
    Normal = 0,
    Selected = 1,
};

struct ServerInfo {
    int Uid;
    QString Host;
    QString Port;
    QString Token;
};

extern QString gate_url_prefix;

// 聊天界面模式
enum ChatUIMode {
    SearchMode, // 搜索模式
    ChatMode, // 聊天模式
    ContactMode, // 联系模式
};

// 自定义QListWidgetItem的几种类型
enum ListItemType {
    CHAT_USER_ITEM, // 聊天用户
    CONTACT_USER_ITEM, // 联系人用户
    SEARCH_USER_ITEM, // 搜索到的用户
    ADD_USER_TIP_ITEM, // 提示添加用户
    INVALID_ITEM, // 不可点击条目
    GROUP_TIP_ITEM, // 分组提示条目
    LINE_ITEM, // 分割线
    APPLY_FRIEND_ITEM, // 好友申请
};

// 聊天item类型，分为自己和别人
enum class ChatRole {
    Self,
    Other
};

// 发送信息
struct MsgInfo {
    QString msgFlag; // 哪一种文件，test, image, file
    QString content; // 文本或者图片的url
    QPixmap pixmap; // 文件和图片的缩略图
};

// 申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_EDIT_LENGTH = 40;
const QString add_prefix = "添加标签 ";
const int tip_offset = 5;

#endif // GLOBAL_H
