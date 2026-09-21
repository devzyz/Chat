#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "userdata.h"

inline QByteArray clientFriendRequest(const UserInfo &self, int toUid,
                                     const QString &description, const QString &backname)
{
    return QJsonDocument(QJsonObject{{"fromuid", self._uid}, {"applyname", self._name},
        {"applydescription", self._description}, {"applyicon", self._icon}, {"applysex", self._sex},
        {"touid", toUid}, {"description", description}, {"backname", backname}}).toJson(QJsonDocument::Compact);
}

inline QByteArray clientAcceptFriendRequest(const UserInfo &self, const ApplyInfo &apply,
                                           const QString &description, const QString &backname)
{
    const QJsonObject applicant{{"applyuid", apply._apply_uid}, {"applyname", apply._apply_name},
        {"applydescription", apply._apply_description}, {"applyicon", apply._apply_icon},
        {"applysex", apply._apply_sex}, {"touid", apply._to_uid},
        {"description", apply._description}, {"backname", apply._backname}};
    const QJsonObject acceptor{{"authuid", self._uid}, {"authname", self._name},
        {"authdescription", self._description}, {"authicon", self._icon}, {"authsex", self._sex},
        {"touid", apply._apply_uid}, {"description", description}, {"backname", backname}};
    return QJsonDocument(QJsonObject{{"authuid", self._uid}, {"applyuid", apply._apply_uid},
        {"applyinfo", applicant}, {"authinfo", acceptor}}).toJson(QJsonDocument::Compact);
}

inline QByteArray clientHistoryRequest(int chatId, qint64 beforeId)
{
    return QJsonDocument(QJsonObject{{"chat_id", chatId}, {"current_msg_id", beforeId}})
        .toJson(QJsonDocument::Compact);
}

inline QByteArray clientPrivateChatRequest(int uid, int otherUid, const QJsonObject &otherInfo = {})
{
    return QJsonDocument(QJsonObject{{"self_id", uid}, {"other_id", otherUid}, {"other_info", otherInfo}})
        .toJson(QJsonDocument::Compact);
}

inline QByteArray clientTextRequest(int uid, int toUid, int chatId, const QJsonArray &texts)
{
    return QJsonDocument(QJsonObject{{"from_uid", uid}, {"to_uid", toUid},
        {"chat_id", chatId}, {"text_array", texts}}).toJson(QJsonDocument::Compact);
}
