#define DEFINE_DIS \
    QPointer<TelegramDialogListModel> dis = this;

#define PROCESS_ROW_CHANGE(KEY, ROLES) \
    int row = p->list.indexOf(KEY); \
    if(row >= 0) \
        Q_EMIT dataChanged(index(row), index(row), QVector<int>()ROLES);


#include "telegramdialoglistmodel.h"
#include "telegramtools.h"
#include "telegramshareddatamanager.h"

#include <QDateTime>
#include <QQmlEngine>
#include <QtQml>
#include <telegram.h>

class TelegramDialogListItem
{
public:
    TelegramDialogListItem() {}
    virtual ~TelegramDialogListItem() {}
    QByteArray id;
    TelegramSharedPointer<DialogObject> dialog;
    TelegramSharedPointer<ChatObject> chat;
    TelegramSharedPointer<UserObject> user;
    TelegramSharedPointer<MessageObject> topMessage;
    TelegramSharedPointer<UserObject> topMessageUser;
};

class TelegramDialogListModelPrivate
{
public:
    int visibility;
    QList<qint32> sortFlag;

    QHash<QByteArray, TelegramDialogListItem> items;
    QList<QByteArray> list;

    int resortTimer;
    QJSValue dateConvertorMethod;
    bool refreshing;

    QHash<ChatObject*, QHash<UserObject*, int> > typingChats;
};

TelegramDialogListModel::TelegramDialogListModel(QObject *parent) :
    TelegramAbstractListModel(parent)
{
    p = new TelegramDialogListModelPrivate;
    p->resortTimer = 0;
    p->refreshing = false;
    p->visibility = VisibilityAll;
    p->sortFlag << SortByDate << SortByUnreads << SortByName << SortByType << SortByOnline;
}

int TelegramDialogListModel::visibility() const
{
    return p->visibility;
}

void TelegramDialogListModel::setVisibility(int visibility)
{
    if(p->visibility == visibility)
        return;

    p->visibility = visibility;
    refresh();
    Q_EMIT visibilityChanged();
}

QList<qint32> TelegramDialogListModel::sortFlag() const
{
    return p->sortFlag;
}

void TelegramDialogListModel::setSortFlag(const QList<qint32> &sortFlag)
{
    if(p->sortFlag == sortFlag)
        return;

    p->sortFlag = sortFlag;
    Q_EMIT sortFlagChanged();
}

QJSValue TelegramDialogListModel::dateConvertorMethod() const
{
    return p->dateConvertorMethod;
}

void TelegramDialogListModel::setDateConvertorMethod(const QJSValue &method)
{
    if(p->dateConvertorMethod.isNull() && method.isNull())
        return;

    p->dateConvertorMethod = method;
    Q_EMIT dateConvertorMethodChanged();
}

bool TelegramDialogListModel::refreshing() const
{
    return p->refreshing;
}

QByteArray TelegramDialogListModel::id(const QModelIndex &index) const
{
    return p->list.at(index.row());
}

int TelegramDialogListModel::count() const
{
    return p->list.count();
}

QVariant TelegramDialogListModel::data(const QModelIndex &index, int role) const
{
    QVariant result;
    const QByteArray &key = id(index);
    const TelegramDialogListItem &item = p->items.value(key);
    switch(role)
    {
    case Qt::DisplayRole:
    case RoleName:
        if(item.user) result = QString("%1 %2").arg(item.user->firstName(), item.user->lastName()).trimmed();
        if(item.chat) result = item.chat->title();
        break;
    case RoleMessageDate:
        if(item.topMessage) result = convetDate(QDateTime::fromTime_t(item.topMessage->date()));
        break;
    case RoleMessage:
        if(item.topMessage) result = item.topMessage->message();
        else result = QString();
        break;
    case RoleMessageUnread:
        if(item.topMessage) result = item.topMessage->unread();
        else result = true;
        break;
    case RoleLastOnline:
        if(item.user) result = QDateTime::fromTime_t(item.user->status()->wasOnline());
        break;
    case RoleIsOnline:
        if(item.user) result = (item.user->status()->classType() == UserStatusObject::TypeUserStatusOnline);
        else result = false;
        break;
    case RoleStatus:
        if(item.user) result = item.user->status()->classType();
        else result = 0;
        break;
    case RoleStatusText:
        result = statusText(item);
        break;
    case RoleTyping:
    {
        QVariantList users;
        QHash<UserObject*, int> data = p->typingChats.value(item.chat);
        QHashIterator<UserObject*, int> i(data);
        while(i.hasNext())
        {
            i.next();
            users << QVariant::fromValue<QObject*>(i.key());
        }

        result = QVariant::fromValue<QVariantList>(users);
    }
        break;
    case RoleUnreadCount:
        result = item.dialog->unreadCount();
        break;
    case RoleMute:
        result = (QDateTime::fromTime_t(item.dialog->notifySettings()->muteUntil()) > QDateTime::currentDateTime());
        break;
    case RoleDialogItem:
        result = QVariant::fromValue<DialogObject*>(item.dialog);
        break;
    case RoleChatItem:
        result = QVariant::fromValue<ChatObject*>(item.chat);
        break;
    case RoleUserItem:
        result = QVariant::fromValue<UserObject*>(item.user);
        break;
    case RoleTopMessageItem:
        result = QVariant::fromValue<MessageObject*>(item.topMessage);
        break;
    }
    return result;
}

QHash<int, QByteArray> TelegramDialogListModel::roleNames() const
{
    static QHash<int, QByteArray> *result = 0;
    if(result)
        return *result;

    result = new QHash<int, QByteArray>();
    result->insert(RoleName, "title");
    result->insert(RoleMessageDate, "messageDate");
    result->insert(RoleMessageUnread, "messageUnread");
    result->insert(RoleMessage, "message");
    result->insert(RoleLastOnline, "lastOnline");
    result->insert(RoleIsOnline, "isOnline");
    result->insert(RoleStatus, "status");
    result->insert(RoleStatusText, "statusText");
    result->insert(RoleTyping, "typing");
    result->insert(RoleUnreadCount, "unreadCount");
    result->insert(RoleMute, "mute");

    result->insert(RoleDialogItem, "dialog");
    result->insert(RoleChatItem, "chat");
    result->insert(RoleUserItem, "user");
    result->insert(RoleTopMessageItem, "topMessage");
    return *result;
}

void TelegramDialogListModel::refresh()
{
    if(!mEngine || !mEngine->telegram())
    {
        clean();
        return;
    }

    getDialogsFromServer(InputPeer::null, 200);
}

void TelegramDialogListModel::clean()
{
    changed(QHash<QByteArray, TelegramDialogListItem>());
}

void TelegramDialogListModel::resort()
{
    if(p->resortTimer)
        killTimer(p->resortTimer);
    p->resortTimer = QObject::startTimer(200);
}

void TelegramDialogListModel::timerEvent(QTimerEvent *e)
{
    if(e->timerId() == p->resortTimer)
    {
        changed(QHash<QByteArray, TelegramDialogListItem>(p->items));
        killTimer(p->resortTimer);
        p->resortTimer = 0;
    }

    TelegramAbstractListModel::timerEvent(e);
}

void TelegramDialogListModel::getDialogsFromServer(const InputPeer &offset, int limit, QHash<QByteArray, TelegramDialogListItem> *items)
{
    if(mEngine->state() != TelegramEngine::AuthLoggedIn)
        return;
    if(!items)
        items = new QHash<QByteArray, TelegramDialogListItem>();

    int offsetId = offset.userId();
    if(!offsetId) offsetId = offset.chatId();
    if(!offsetId) offsetId = offset.channelId();

    setRefreshing(true);

    Telegram *tg = mEngine->telegram();
    DEFINE_DIS;
    tg->messagesGetDialogs(0, offsetId, offset, limit, [this, items, limit, dis](TG_MESSAGES_GET_DIALOGS_CALLBACK) {
        Q_UNUSED(msgId)
        if(!dis) {
            delete items;
            return;
        }

        setRefreshing(false);

        if(!error.null) {
            setError(error.errorText, error.errorCode);
            delete items;
            return;
        }

        const InputPeer lastInputPeer = processOnResult(result, items);
        const int count = 0;//result.dialogs().count();
        if(count == 0 || count < limit) {
            /*! finished !*/
            changed(*items);
            delete items;
        } else {
            /*! There are also another dialogs !*/
            getDialogsFromServer(lastInputPeer, limit, items);
        }
    });
}

InputPeer TelegramDialogListModel::processOnResult(const MessagesDialogs &result, QHash<QByteArray, TelegramDialogListItem> *items)
{
    TelegramSharedDataManager *tsdm = mEngine->sharedData();

    Q_FOREACH(const Dialog &dlg, result.dialogs())
    {
        QByteArray key;
        TelegramDialogListItem item;
        item.dialog = tsdm->insertDialog(dlg, &key);
        item.id = key;
        (*items)[key] = item;
        connectDialogSignals(key, item.dialog);
    }

    QHash<int, Chat> chats;
    Q_FOREACH(const Chat &chat, result.chats())
    {
        chats[chat.id()] = chat;
        const QByteArray &id = TelegramTools::identifier(chat);
        if(!items->contains(id))
            continue;

        TelegramSharedPointer<ChatObject> chatPtr = tsdm->insertChat(chat);
        (*items)[id].chat = chatPtr;
        connectChatSignals(id, chatPtr);
    }

    QHash<int, User> users;
    Q_FOREACH(const User &user, result.users())
    {
        users[user.id()] = user;
        const QByteArray &id = TelegramTools::identifier(user);
        if(!items->contains(id))
            continue;

        TelegramSharedPointer<UserObject> userPtr = tsdm->insertUser(user);
        (*items)[id].user = userPtr;
        connectUserSignals(id, userPtr);
    }

    Q_FOREACH(const Message &msg, result.messages())
    {
        const Peer &peer = TelegramTools::messagePeer(msg);
        const QByteArray &id = TelegramTools::identifier(peer);
        if(!items->contains(id)) continue;
        if((*items)[id].dialog->topMessage() != msg.id()) continue;
        TelegramSharedPointer<MessageObject> msgPtr = tsdm->insertMessage(msg);
        (*items)[id].topMessage = msgPtr;
        connectMessageSignals(id, msgPtr);

        if(users.contains(peer.userId()))
        {
            const User &user = users.value(peer.userId());
            TelegramSharedPointer<UserObject> userPtr = tsdm->insertUser(user);
            (*items)[id].topMessageUser = userPtr;
        }
    }

    Peer peer = result.dialogs().last().peer();
    InputPeer lastInputPeer;
    switch(static_cast<int>(peer.classType()))
    {
    case Peer::typePeerChannel:
        lastInputPeer = TelegramTools::chatInputPeer(chats.value(peer.channelId()));
        break;
    case Peer::typePeerChat:
        lastInputPeer = TelegramTools::chatInputPeer(chats.value(peer.chatId()));
        break;
    case Peer::typePeerUser:
        lastInputPeer = TelegramTools::userInputPeer(users.value(peer.userId()));
        break;
    }

    return lastInputPeer;
}

void TelegramDialogListModel::changed(const QHash<QByteArray, TelegramDialogListItem> &items)
{
    QByteArrayList list = getSortedList(items);
    Q_FOREACH(const QByteArray &id, list)
    {
        const TelegramDialogListItem &itemNew = items.value(id);
        TelegramDialogListItem &itemOld = p->items[id];
        if(itemNew.topMessage != itemOld.topMessage)
        {
            itemOld.topMessage = itemNew.topMessage;
            PROCESS_ROW_CHANGE(id, <<RoleTopMessageItem
                               <<RoleMessageDate
                               <<RoleMessageUnread
                               <<RoleMessage);
        }

        const Dialog &dlg = itemNew.dialog->core();
        if(dlg.peer().classType() == Peer::typePeerChannel && !(p->visibility & VisibilityChannels))
            list.removeOne(id);
        else
        if(dlg.peer().classType() == Peer::typePeerChat && !(p->visibility & VisibilityChats))
            list.removeOne(id);
        else
        if(dlg.peer().classType() == Peer::typePeerUser)
        {
            if(!(p->visibility & VisibilityUsers))
                list.removeOne(id);
            else
            if(itemNew.user)
            {
                const User &user = itemNew.user->core();
                if(user.bot() && !(p->visibility & VisibilityBots))
                    list.removeOne(id);
                else
                if(user.contact() && !(p->visibility & VisibilityContacts))
                    list.removeOne(id);
                else
                if(!user.contact() && !(p->visibility & VisibilityNonContacts))
                    list.removeOne(id);
                else
                if(user.status().classType() != UserStatus::typeUserStatusOnline && (p->visibility & VisibilityOnlineUsersOnly))
                    list.removeOne(id);
            }
        }
    }

    p->items.unite(items);

    bool count_changed = (list.count()==p->list.count());

    for( int i=0 ; i<p->list.count() ; i++ )
    {
        const QByteArray &item = p->list.at(i);
        if( list.contains(item) )
            continue;

        beginRemoveRows(QModelIndex(), i, i);
        p->list.removeAt(i);
        i--;
        endRemoveRows();
    }

    QList<QByteArray> temp_list = list;
    for( int i=0 ; i<temp_list.count() ; i++ )
    {
        const QByteArray &item = temp_list.at(i);
        if( p->list.contains(item) )
            continue;

        temp_list.removeAt(i);
        i--;
    }
    while( p->list != temp_list )
        for( int i=0 ; i<p->list.count() ; i++ )
        {
            const QByteArray &item = p->list.at(i);
            int nw = temp_list.indexOf(item);
            if( i == nw )
                continue;

            beginMoveRows( QModelIndex(), i, i, QModelIndex(), nw>i?nw+1:nw );
            p->list.move( i, nw );
            endMoveRows();
        }

    for( int i=0 ; i<list.count() ; i++ )
    {
        const QByteArray &item = list.at(i);
        if( p->list.contains(item) )
            continue;

        beginInsertRows(QModelIndex(), i, i );
        p->list.insert( i, item );
        endInsertRows();
    }

    p->items = items;
    if(count_changed)
        Q_EMIT countChanged();
}

const QHash<QByteArray, TelegramDialogListItem> *tg_dlist_model_lessthan_items = 0;
int tg_dlist_model_lessthan_sortFlag = 0;
bool tg_dlist_model_sort(const QByteArray &s1, const QByteArray &s2);

QByteArrayList TelegramDialogListModel::getSortedList(const QHash<QByteArray, TelegramDialogListItem> &items)
{
    QByteArrayList list = items.keys();

    QList<int> flagOrders = p->sortFlag;
    QList<int> defaultOrders;
    defaultOrders << SortByDate << SortByUnreads << SortByName << SortByType << SortByOnline;
    Q_FOREACH(int flag, defaultOrders)
        if(!flagOrders.contains(flag))
            flagOrders << flag;

    tg_dlist_model_lessthan_items = &items;
    for(int i=flagOrders.count()-1; i>=0; i--)
    {
        const int flag = flagOrders.at(i);
        tg_dlist_model_lessthan_sortFlag = flag;
        qStableSort(list.begin(), list.end(), tg_dlist_model_sort);
    }

    return list;
}

void TelegramDialogListModel::connectChatSignals(const QByteArray &id, ChatObject *chat)
{
    connect(chat, &ChatObject::titleChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleName<<Qt::DisplayRole)
    });
}

void TelegramDialogListModel::connectUserSignals(const QByteArray &id, UserObject *user)
{
    std::function<void ()> callback = [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleName<<Qt::DisplayRole);
    };
    connect(user, &UserObject::firstNameChanged, this, callback);
    connect(user, &UserObject::lastNameChanged, this, callback);
    connect(user->status(), &UserStatusObject::coreChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleStatusText<<RoleStatus<<RoleIsOnline);
        resort();
    });
}

void TelegramDialogListModel::connectMessageSignals(const QByteArray &id, MessageObject *message)
{
    connect(message, &MessageObject::unreadChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleMessageUnread);
    });
}

void TelegramDialogListModel::connectDialogSignals(const QByteArray &id, DialogObject *dialog)
{
    connect(dialog, &DialogObject::topMessageChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleMessage<<RoleMessageDate<<RoleMessageUnread);
    });
    connect(dialog, &DialogObject::unreadCountChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleUnreadCount);
    });
    connect(dialog->notifySettings(), &PeerNotifySettingsObject::coreChanged, this, [this, id](){
        PROCESS_ROW_CHANGE(id, <<RoleMute);
    });
}

void TelegramDialogListModel::onUpdateShortMessage(qint32 id, qint32 userId, const QString &message, qint32 pts, qint32 pts_count, qint32 date, const MessageFwdHeader &fwd_from, qint32 reply_to_msg_id, bool unread, bool out)
{
    if(!mEngine)
        return;

    Peer peer(Peer::typePeerUser);
    peer.setUserId(out? userId : mEngine->telegram()->ourId());

    Message msg(Message::typeMessage);
    msg.setId(id);
    msg.setFromId(out? mEngine->telegram()->ourId() : userId);
    msg.setToId(peer);
    msg.setMessage(message);
    msg.setDate(date);
    msg.setFwdFrom(fwd_from);
    msg.setReplyToMsgId(reply_to_msg_id);
    msg.setUnread(unread);
    msg.setOut(out);

    Update update(Update::typeUpdateNewMessage);
    update.setMessage(msg);
    update.setPts(pts);
    update.setPtsCount(pts_count);

    insertUpdate(update);
}

void TelegramDialogListModel::onUpdateShortChatMessage(qint32 id, qint32 fromId, qint32 chatId, const QString &message, qint32 pts, qint32 pts_count, qint32 date, const MessageFwdHeader &fwd_from, qint32 reply_to_msg_id, bool unread, bool out)
{
    Peer peer(Peer::typePeerChat);
    peer.setChatId(chatId);

    Message msg(Message::typeMessage);
    msg.setId(id);
    msg.setFromId(fromId);
    msg.setToId(peer);
    msg.setMessage(message);
    msg.setDate(date);
    msg.setFwdFrom(fwd_from);
    msg.setReplyToMsgId(reply_to_msg_id);
    msg.setUnread(unread);
    msg.setOut(out);

    Update update(Update::typeUpdateNewMessage);
    update.setMessage(msg);
    update.setPts(pts);
    update.setPtsCount(pts_count);

    insertUpdate(update);
}

void TelegramDialogListModel::onUpdateShort(const Update &update, qint32 date)
{
    Q_UNUSED(date)
    insertUpdate(update);
}

void TelegramDialogListModel::onUpdatesCombined(const QList<Update> &updates, const QList<User> &users, const QList<Chat> &chats, qint32 date, qint32 seqStart, qint32 seq)
{
    Q_FOREACH(const Update &update, updates)
        insertUpdate(update);
}

void TelegramDialogListModel::onUpdates(const QList<Update> &updates, const QList<User> &users, const QList<Chat> &chats, qint32 date, qint32 seq)
{
    Q_FOREACH(const Update &update, updates)
        insertUpdate(update);
}

void TelegramDialogListModel::insertUpdate(const Update &update)
{
    if(!mEngine)
        return;

    Telegram *tg = mEngine->telegram();
    if(!tg)
        return;

    const uint type = static_cast<int>(update.classType());
    switch(type)
    {
    case Update::typeUpdateNewChannelMessage:
    case Update::typeUpdateNewMessage:
    {
        const Message &msg = update.message();
        const Peer &peer = TelegramTools::messagePeer(msg);
        const QByteArray &id = TelegramTools::identifier(peer);
        if(!p->items.contains(id))
            return;

        TelegramDialogListItem &item = p->items[id];
        TelegramSharedPointer<MessageObject> oldMsg = item.topMessage;
        if(oldMsg && oldMsg->date() <= msg.date())
        {
            TelegramSharedPointer<MessageObject> msgObj = mEngine->sharedData()->insertMessage(msg);
            item.topMessage = msgObj;

            PROCESS_ROW_CHANGE(id, <<RoleTopMessageItem
                               <<RoleMessageDate
                               <<RoleMessageUnread
                               <<RoleMessage);

            resort();
        }
    }
        break;
    case Update::typeUpdateMessageID:
        break;
    case Update::typeUpdateDeleteMessages:
    {
        const QList<qint32> &messages = update.messages();
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.topMessage && messages.contains(item.topMessage->id()))
            {
                const QByteArray &id = item.id;
                p->items[id].topMessage = 0;
                PROCESS_ROW_CHANGE(id, <<RoleTopMessageItem
                                   <<RoleMessageDate
                                   <<RoleMessageUnread
                                   <<RoleMessage);

                InputPeer peer = item.chat? TelegramTools::chatInputPeer(item.chat->core())
                                          : TelegramTools::userInputPeer(item.user->core());

                // Get last top message again...
                DEFINE_DIS;
                tg->messagesGetHistory(peer, 0, 0, 0, 1, 0, 0, [this, dis, id](TG_MESSAGES_GET_HISTORY_CALLBACK){
                    Q_UNUSED(msgId)
                    if(!dis) return;
                    if(!error.null) {
                        setError(error.errorText, error.errorCode);
                        return;
                    }
                    if(result.messages().isEmpty() || !mEngine)
                        return;
                    p->items[id].topMessage = mEngine->sharedData()->insertMessage(result.messages().first());
                    PROCESS_ROW_CHANGE(id, <<RoleTopMessageItem
                                       <<RoleMessageDate
                                       <<RoleMessageUnread
                                       <<RoleMessage);
                });
//                refresh();
            }
    }
        break;
    case Update::typeUpdateUserTyping:
    case Update::typeUpdateChatUserTyping:
    {
        const qint32 userId = update.userId();
        const qint32 chatId = update.chatId();
        UserObject *user = 0;
        ChatObject *chat = 0;
        QByteArray id;
        Q_FOREACH(TelegramDialogListItem item, p->items)
        {
            if(item.user && item.user->id() == userId)
            {
                user = item.user;
                if(type == Update::typeUpdateUserTyping)
                    id = item.id;
            }
            else
            if(item.chat && item.chat->id() == chatId)
            {
                chat = item.chat;
                if(type == Update::typeUpdateChatUserTyping)
                    id = item.id;
            }
        }

        if(user)
        {
            p->typingChats[chat][user]++;
            PROCESS_ROW_CHANGE(id, <<RoleTyping);
            startTimer(4000, [this, chat, user, id](){
                int &count = p->typingChats[chat][user];
                count--;
                if(count == 0) {
                    p->typingChats[chat].remove(user);
                    if(p->typingChats.value(chat).isEmpty())
                        p->typingChats.remove(chat);
                }
                PROCESS_ROW_CHANGE(id, <<RoleTyping);
            });
        }
    }
        break;
    case Update::typeUpdateChatParticipants:
    {
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.chat && item.chat->id() == update.participants().chatId())
                item.chat->setParticipantsCount(update.participants().participants().count());
    }
        break;
    case Update::typeUpdateUserStatus:
    {
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.user && item.user->id() == update.userId())
                item.user->status()->operator =(update.status());
    }
        break;
    case Update::typeUpdateUserName:
    {
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.user && item.user->id() == update.userId())
            {
                UserObject *user = item.user;
                user->setFirstName(update.firstName());
                user->setLastName(update.lastName());
                user->setUsername(update.username());
            }
    }
        break;
    case Update::typeUpdateUserPhoto:
    {
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.user && item.user->id() == update.userId())
            {
                UserObject *user = item.user;
                user->photo()->operator =(update.photo());
            }
    }
        break;
    case Update::typeUpdateContactRegistered:
    {
        refresh();
    }
        break;
    case Update::typeUpdateContactLink:
        break;
    case Update::typeUpdateNewAuthorization:
        break;
    case Update::typeUpdateNewEncryptedMessage:
        break;
    case Update::typeUpdateEncryptedChatTyping:
        break;
    case Update::typeUpdateEncryption:
        break;
    case Update::typeUpdateEncryptedMessagesRead:
        break;
    case Update::typeUpdateChatParticipantAdd:
    {
        Peer peer(Peer::typePeerChat);
        peer.setChatId(update.chatId());
        const QByteArray &id = TelegramTools::identifier(peer);
        if(p->items.contains(id))
        {
            TelegramDialogListItem &item = p->items[id];
            if(item.chat)
                item.chat->setParticipantsCount(item.chat->participantsCount()+1);
        }
    }
        break;
    case Update::typeUpdateChatParticipantDelete:
    {
        Peer peer(Peer::typePeerChat);
        peer.setChatId(update.chatId());
        const QByteArray &id = TelegramTools::identifier(peer);
        if(p->items.contains(id))
        {
            TelegramDialogListItem &item = p->items[id];
            if(item.chat)
                item.chat->setParticipantsCount(item.chat->participantsCount()-1);
        }
    }
        break;
    case Update::typeUpdateDcOptions:
        break;
    case Update::typeUpdateUserBlocked:
        break;
    case Update::typeUpdateNotifySettings:
    {
        const NotifyPeer &peer = update.peerNotify();
        const PeerNotifySettings &settings = update.notifySettings();
        switch(static_cast<int>(peer.classType()))
        {
        case NotifyPeer::typeNotifyAll:
            Q_FOREACH(const TelegramDialogListItem &item, p->items)
                if(item.dialog) item.dialog->notifySettings()->operator =(settings);
            break;
        case NotifyPeer::typeNotifyChats:
            Q_FOREACH(const TelegramDialogListItem &item, p->items)
                if(item.chat && item.dialog) item.dialog->notifySettings()->operator =(settings);
            break;
        case NotifyPeer::typeNotifyUsers:
            Q_FOREACH(const TelegramDialogListItem &item, p->items)
                if(item.user && item.dialog) item.dialog->notifySettings()->operator =(settings);
            break;
        case NotifyPeer::typeNotifyPeer:
        {
            const QByteArray &id = TelegramTools::identifier(peer.peer());
            if(p->items.contains(id))
            {
                TelegramDialogListItem &item = p->items[id];
                if(item.dialog) item.dialog->notifySettings()->operator =(settings);
            }
        }
            break;
        }
    }
        break;
    case Update::typeUpdateServiceNotification:
        break;
    case Update::typeUpdatePrivacy:
        break;
    case Update::typeUpdateUserPhone:
    {
        Q_FOREACH(TelegramDialogListItem item, p->items)
            if(item.user && item.user->id() == update.userId())
            {
                UserObject *user = item.user;
                user->setPhone(update.phone());
            }
    }
        break;
    case Update::typeUpdateReadHistoryInbox:
    {
        const QByteArray &id = TelegramTools::identifier(update.peer());
        if(p->items.contains(id))
        {
            TelegramDialogListItem &item = p->items[id];
            if(item.dialog)
            {
                item.dialog->setReadInboxMaxId(update.maxId());
                item.dialog->setUnreadCount(0);
            }
            if(item.topMessage)
                item.topMessage->setUnread(false);
        }

    }
        break;
    case Update::typeUpdateReadHistoryOutbox:
    {
        const QByteArray &id = TelegramTools::identifier(update.peer());
        if(p->items.contains(id))
        {
            TelegramDialogListItem &item = p->items[id];
            if(item.topMessage)
                item.topMessage->setUnread(false);
        }
    }
        break;
    case Update::typeUpdateWebPage:
        break;
    case Update::typeUpdateReadMessagesContents:
        qDebug() << "typeUpdateReadMessagesContents";
        break;
    case Update::typeUpdateChannelTooLong:
        break;
    case Update::typeUpdateChannel:
        break;
    case Update::typeUpdateChannelGroup:
        break;
    case Update::typeUpdateReadChannelInbox:
    {
        Peer peer(Peer::typePeerChannel);
        peer.setChannelId(update.channelId());
        const QByteArray &id = TelegramTools::identifier(peer);
        if(p->items.contains(id))
        {
            TelegramDialogListItem &item = p->items[id];
            if(item.dialog)
            {
                item.dialog->setReadInboxMaxId(update.maxId());
                item.dialog->setUnreadCount(0);
            }
            if(item.topMessage)
                item.topMessage->setUnread(false);
        }
    }
        break;
    case Update::typeUpdateDeleteChannelMessages:
        break;
    case Update::typeUpdateChannelMessageViews:
        break;
    case Update::typeUpdateChatAdmins:
        break;
    case Update::typeUpdateChatParticipantAdmin:
        break;
    case Update::typeUpdateNewStickerSet:
        break;
    case Update::typeUpdateStickerSetsOrder:
        break;
    case Update::typeUpdateStickerSets:
        break;
    case Update::typeUpdateSavedGifs:
        break;
    case Update::typeUpdateBotInlineQuery:
        break;
    case Update::typeUpdateBotInlineSend:
        break;
    }
}

QString TelegramDialogListModel::statusText(const TelegramDialogListItem &item) const
{
    if(item.user)
    {
        switch(item.user->status()->classType())
        {
        case UserStatusObject::TypeUserStatusEmpty:
            return QString();
            break;
        case UserStatusObject::TypeUserStatusLastMonth:
            return tr("Last month");
            break;
        case UserStatusObject::TypeUserStatusLastWeek:
            return tr("Last week");
            break;
        case UserStatusObject::TypeUserStatusOffline:
            return tr("Last seen %1").arg(convetDate(QDateTime::fromTime_t(item.user->status()->wasOnline())));
            break;
        case UserStatusObject::TypeUserStatusOnline:
            return tr("Online");
            break;
        case UserStatusObject::TypeUserStatusRecently:
            return tr("Last seen recently");
            break;
        }
    }
    else
    if(item.chat)
        return tr("%1 members").arg(item.chat->participantsCount());

    return QString();
}

QString TelegramDialogListModel::convetDate(const QDateTime &td) const
{
    QQmlEngine *engine = qmlEngine(this);
    if(p->dateConvertorMethod.isCallable() && engine)
        return p->dateConvertorMethod.call(QJSValueList()<<engine->toScriptValue<QDateTime>(td)).toString();
    else
    if(!p->dateConvertorMethod.isNull() && !p->dateConvertorMethod.isUndefined())
        return p->dateConvertorMethod.toString();
    else
    {
        const QDateTime &current = QDateTime::currentDateTime();
        const qint64 secs = td.secsTo(current);
        const int days = td.daysTo(current);
        if(secs < 24*60*60)
            return days? td.toString("Tomorrow HH:mm") : td.toString("HH:mm");
        else
            return td.toString("MMM dd, HH:mm");
    }
}

void TelegramDialogListModel::setRefreshing(bool stt)
{
    if(p->refreshing == stt)
        return;

    p->refreshing = stt;
    Q_EMIT refreshingChanged();
}

TelegramDialogListModel::~TelegramDialogListModel()
{
    delete p;
}



bool tg_dlist_model_sort(const QByteArray &s1, const QByteArray &s2)
{
    const TelegramDialogListItem &i1 = tg_dlist_model_lessthan_items->value(s1);
    const TelegramDialogListItem &i2 = tg_dlist_model_lessthan_items->value(s2);
    switch(tg_dlist_model_lessthan_sortFlag)
    {
    case TelegramDialogListModel::SortByDate:
        if(!i1.topMessage) return false;
        if(!i2.topMessage) return true;
        return i1.topMessage->date() > i2.topMessage->date();
        break;
    case TelegramDialogListModel::SortByName:
    {
        QString n1, n2;
        if(i1.dialog->peer()->classType() == PeerObject::TypePeerUser && i1.user)
            n1 = i1.user->firstName() + " " + i1.user->lastName();
        else
        if(i1.chat)
            n1 = i1.chat->title();
        if(i2.dialog->peer()->classType() == PeerObject::TypePeerUser && i2.user)
            n2 = i2.user->firstName() + " " + i2.user->lastName();
        else
        if(i2.chat)
            n2 = i2.chat->title();
        return n1.toLower() < n2.toLower();
    }
        break;
    case TelegramDialogListModel::SortByOnline:
        if(!i2.user || i2.user->status()->classType() == UserStatusObject::TypeUserStatusEmpty) return true;
        if(!i1.user || i1.user->status()->classType() == UserStatusObject::TypeUserStatusEmpty) return false;
        if( i1.user->status()->classType() == UserStatusObject::TypeUserStatusOnline &&
            i2.user->status()->classType() == UserStatusObject::TypeUserStatusOffline)
            return true;
        else
        if( i1.user->status()->classType() == UserStatusObject::TypeUserStatusOffline &&
            i2.user->status()->classType() == UserStatusObject::TypeUserStatusOnline)
            return false;
        else
        if(i1.user->status()->classType() == i2.user->status()->classType())
            return i1.user->status()->wasOnline() > i2.user->status()->wasOnline();
        else
            return i1.user->status()->classType() < i2.user->status()->classType();
        break;
    case TelegramDialogListModel::SortByType:
        if(i2.dialog->peer()->classType() == PeerObject::TypePeerUser)
            return false;
        else if(i1.dialog->peer()->classType() == PeerObject::TypePeerUser)
            return true;
        else if(i2.dialog->peer()->classType() == PeerObject::TypePeerChat)
            return false;
        else if(i1.dialog->peer()->classType() == PeerObject::TypePeerChat)
            return true;
        else
            return false;
        break;
    case TelegramDialogListModel::SortByUnreads:
        return i1.dialog->unreadCount() >= i2.dialog->unreadCount();
        break;
    }
    return s1 < s2;
}