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
class QDBusPendingCallWatcher;
class QDBusInterface;

class SailfishUserManager : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    explicit SailfishUserManager(QObject *parent = nullptr);
    ~SailfishUserManager();

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
    void currentUserChanged(uint uid);
    void currentUserChangeFailed(uint uid);
    void aboutToChangeCurrentUser(uint uid);

public slots:
    QList<SailfishUserManagerEntry> users();
    uint addUser(const QString &name);
    void removeUser(uint uid);
    void modifyUser(uint uid, const QString &new_name);
    void setCurrentUser(uint uid);
    uint currentUser();

private slots:
    void userServiceStop(QDBusPendingCallWatcher *replyWatcher);
    void autologinServiceStop(QDBusPendingCallWatcher *replyWatcher);
    void autologinServiceStart(QDBusPendingCallWatcher *replyWatcher);

private:
    bool checkAccessRights(uint uid_to_modify);
    uid_t checkCallerUid();

    QTimer *m_timer;
    LibUserHelper *m_lu;
    uid_t m_switchUser;
    uid_t m_currentUid;
    QDBusInterface *m_systemd;
};

#endif // SAILFISHUSERMANAGER_H
