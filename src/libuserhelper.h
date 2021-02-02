/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#ifndef LIBUSERHELPER_H
#define LIBUSERHELPER_H

#include <QString>

class LibUserHelper
{
public:
    LibUserHelper();
    uint addGroup(const QString &group, int gid = 0);
    bool removeGroup(uint gid);
    bool addUserToGroup(const QString &user, const QString &group);
    bool removeUserFromGroup(const QString &user, const QString &group);
    uint addUser(const QString &user, const QString &name, uint uid = 0, const QString &home = QString());
    bool removeUser(uint uid);
    bool modifyUser(uint uid, const QString &newName) const;
    QString homeDir(uint uid);
    QStringList groups(uint uid);
    QString getUserUuid(uint uid) const;
};

#endif // LIBUSERHELPER_H
