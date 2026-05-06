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

#endif // GLOBAL_H
