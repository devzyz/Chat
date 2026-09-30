#include "chatuserlist.h"
#include "listviewbehavior.h"

ChatUserList::ChatUserList(QWidget *parent) : QListWidget(parent)
{
    auto *behavior = new ListViewBehavior(this);
    connect(behavior, &ListViewBehavior::bottomReached, this, &ChatUserList::moreChatsRequested);
}
