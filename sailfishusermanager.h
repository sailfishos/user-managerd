/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#ifndef SAILFISHUSERMANAGER_H
#define SAILFISHUSERMANAGER_H

#include "sailfishusermanagerinterface.h"

#include <QDBusContext>

class QTimer;
class LibUserHelper;

class SailfishUserManager : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    explicit SailfishUserManager(QObject *parent = nullptr);

private:
    bool addUserToGroups(const QString &user);
    bool makeHome(const QString &user);
    bool removeDir(const QString &dir);
    bool removeHome(uint uid);
    bool copyDir(const QString &source, const QString &destination, uint uid, uint guid);

signals:
    void userAdded(const SailfishUserManagerEntry &user);
    void userRemoved(uint uid);
    void userModified(uint uid, const QString &new_name);

public slots:
    QList<SailfishUserManagerEntry> users();
    uint addUser(const QString &name);
    void removeUser(uint uid);
    void modifyUser(uint uid, const QString &new_name);

private:
    QTimer *m_timer;
    LibUserHelper *m_lu;
};

#endif // SAILFISHUSERMANAGER_H
