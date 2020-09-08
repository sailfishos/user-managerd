/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#ifndef SAILFISHUSERMANAGERINTERFACE_H
#define SAILFISHUSERMANAGERINTERFACE_H

#include <QString>
#include <QDBusArgument>

#define SAILFISH_USERMANAGER_DBUS_INTERFACE "org.sailfishos.usermanager"
#define SAILFISH_USERMANAGER_DBUS_OBJECT_PATH "/"
#define SAILFISH_USERMANAGER_GUEST_UID 105000
#define SAILFISH_USERMANAGER_GUEST_HOME "/home/sailfish_guest"

// Luks has eight slots where one slot is reserved for backup
#define SAILFISH_USERMANAGER_MAX_USERS 7

#define SailfishUserManagerErrorBusy "org.sailfishos.usermanager.Error.Busy"
#define SailfishUserManagerErrorHomeCreateFailed "org.sailfishos.usermanager.Error.HomeCreateFailed"
#define SailfishUserManagerErrorHomeRemoveFailed "org.sailfishos.usermanager.Error.HomeRemoveFailed"
#define SailfishUserManagerErrorGroupCreateFailed "org.sailfishos.usermanager.Error.GroupCreateFailed"
#define SailfishUserManagerErrorUserAddFailed "org.sailfishos.usermanager.Error.UserAddFailed"
#define SailfishUserManagerErrorMaxUsersReached "org.sailfishos.usermanager.Error.MaxUsersReached"
#define SailfishUserManagerErrorUserModifyFailed "org.sailfishos.usermanager.Error.UserModifyFailed"
#define SailfishUserManagerErrorUserRemoveFailed "org.sailfishos.usermanager.Error.UserRemoveFailed"
#define SailfishUserManagerErrorGetUidFailed "org.sailfishos.usermanager.Error.GetUidFailed"
#define SailfishUserManagerErrorGetUuidFailed "org.sailfishos.usermanager.Error.GetUuidFailed"
#define SailfishUserManagerErrorUserNotFound "org.sailfishos.usermanager.Error.UserNotFound"
#define SailfishUserManagerErrorAddToGroupFailed "org.sailfishos.usermanager.Error.AddToGroupFailed"
#define SailfishUserManagerErrorRemoveFromGroupFailed "org.sailfishos.usermanager.Error.RemoveFromGroupFailed"

struct SailfishUserManagerEntry {
    QString user;
    QString name;
    uint uid;
};

inline QDBusArgument &operator<<(QDBusArgument &argument, const SailfishUserManagerEntry &user)
{
    argument.beginStructure();
    argument << user.user << user.name << user.uid;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, SailfishUserManagerEntry &user)
{
    argument.beginStructure();
    argument >> user.user >> user.name >> user.uid;
    argument.endStructure();
    return argument;
}

Q_DECLARE_METATYPE(SailfishUserManagerEntry)

#endif // SAILFISHUSERMANAGERINTERFACE_H
