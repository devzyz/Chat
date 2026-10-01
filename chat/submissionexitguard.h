#pragma once
#include <QMessageBox>

/** @brief 主动退出前确认未落盘内容；Cancel 返回 false，不改变提交任务。 */
inline bool confirmSubmissionExit(QWidget *parent, bool hasPending)
{
    return !hasPending || QMessageBox::question(parent, QObject::tr("未提交内容"),
        QObject::tr("仍有未落盘的消息或附件。退出将丢弃这些内容，是否退出？"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}
