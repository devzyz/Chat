#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
