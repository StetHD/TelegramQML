#include "telegramabstractlistmodel.h"

TelegramAbstractListModel::TelegramAbstractListModel(QObject *parent) :
    QAbstractListModel(parent)
{
}

QStringList TelegramAbstractListModel::roles() const
{
    QStringList result;
    const QHash<int,QByteArray> &roles = roleNames();
    QHashIterator<int,QByteArray> i(roles);
    while(i.hasNext())
    {
        i.next();
        result << i.value();
    }

    qSort(result.begin(), result.end());
    return result;
}

QQmlListProperty<QObject> TelegramAbstractListModel::items()
{
    return QQmlListProperty<QObject>(this, &_items, QQmlListProperty<QObject>::AppendFunction(append),
                                                      QQmlListProperty<QObject>::CountFunction(count),
                                                      QQmlListProperty<QObject>::AtFunction(at),
                                     QQmlListProperty<QObject>::ClearFunction(clear) );
}

QVariant TelegramAbstractListModel::get(int row, int role) const
{
    if(row >= count() || row < 0)
        return QVariant();

    const QModelIndex &idx = index(row,0);
    return data(idx , role);
}

QVariant TelegramAbstractListModel::get(int index, const QString &roleName) const
{
    const int role = roleNames().key(roleName.toUtf8());
    return get(index, role);
}

QVariantMap TelegramAbstractListModel::get(int index) const
{
    if(index >= count())
        return QVariantMap();

    QVariantMap result;
    const QHash<int,QByteArray> &roles = roleNames();
    QHashIterator<int,QByteArray> i(roles);
    while(i.hasNext())
    {
        i.next();
        result[i.value()] = get(index, i.key());
    }

    return result;
}

void TelegramAbstractListModel::append(QQmlListProperty<QObject> *p, QObject *v)
{
    TelegramAbstractListModel *aobj = static_cast<TelegramAbstractListModel*>(p->object);
    aobj->_items.append(v);
    Q_EMIT aobj->itemsChanged();
}

int TelegramAbstractListModel::count(QQmlListProperty<QObject> *p)
{
    TelegramAbstractListModel *aobj = static_cast<TelegramAbstractListModel*>(p->object);
    return aobj->_items.count();
}

QObject *TelegramAbstractListModel::at(QQmlListProperty<QObject> *p, int idx)
{
    TelegramAbstractListModel *aobj = static_cast<TelegramAbstractListModel*>(p->object);
    return aobj->_items.at(idx);
}

void TelegramAbstractListModel::clear(QQmlListProperty<QObject> *p)
{
    TelegramAbstractListModel *aobj = static_cast<TelegramAbstractListModel*>(p->object);
    aobj->_items.clear();
    Q_EMIT aobj->itemsChanged();
}

TelegramAbstractListModel::~TelegramAbstractListModel()
{
}
