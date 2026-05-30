#pragma once
#include <string>

// 用户基本信息
struct UserInfo {
	UserInfo() : _uid(0), _name(""), _password(""), _email(""), _description(""), _icon(""), _sex(0) {}
	int _uid; // id
	std::string _name; // 昵称
	std::string _password; // 密码
	std::string _email; // 邮箱
	std::string _description; // 个性签名
	std::string _icon; // 头像
	int _sex; // 性别
};

// 申请好友的信息
struct ApplyInfo {
    ApplyInfo() {};
    ApplyInfo(int uid, std::string name, std::string description,
        std::string icon, int sex, int status)
        :_uid(uid), _name(name), _description(description),
        _icon(icon), _sex(sex), _status(status) {
    }

    void SetIcon(std::string head) {
        _icon = head;
    }
    int _uid; // 用户id
    std::string _name; // 用户名
    std::string _description; // 用户请求描述信息
    std::string _icon; // 头像
    int _sex; // 性别
    int _status; // 状态，是否添加
};