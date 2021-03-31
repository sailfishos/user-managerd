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

#ifndef __GLIBC__
#include <sys/types.h>
#endif

#include "sailfishusermanagerinterface.h"
#include "systemdmanager.h"
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
    static int removeUserFiles(const char *user);

private:
    bool addUserToGroups(const QString &user);
    bool makeHome(const QString &user);
    bool removeDir(const QString &dir);
    bool removeHome(uint uid);
    bool copyDir(const QString &source, const QString &destination, uint uid, uint guid);
    static void executeScripts(uint uid, const QString &directory);
    static int removeUserFiles(uint uid);
    static void setUserLimits(uint uid);
    uint addSailfishUser(const QString &user, const QString &name, uint userId = 0, const QString &home = QString());

signals:
    void userAdded(const SailfishUserManagerEntry &user);
    void userRemoved(uint uid);
    void userModified(uint uid, const QString &new_name);
    void currentUserChanged(uint uid);
    void currentUserChangeFailed(uint uid);
    void aboutToChangeCurrentUser(uint uid);
    void guestUserEnabled(bool enabled);

public slots:
    QList<SailfishUserManagerEntry> users();
    uint addUser(const QString &name);
    void removeUser(uint uid);
    void modifyUser(uint uid, const QString &new_name);
    void setCurrentUser(uint uid);
    uint currentUser();
    QString currentUserUuid();
    QString userUuid(uint uid);
    QStringList usersGroups(uint uid);
    void addToGroups(uint uid, const QStringList &groups);
    void removeFromGroups(uint uid, const QStringList &groups);
    void enableGuestUser(bool enable);

private slots:
    void exitTimeout();
    void onBusyChanged();
    void onUnitJobFinished(SystemdManager::Job &job);
    void onUnitJobFailed(SystemdManager::Job &job, SystemdManager::JobList &remaining);
    void onCreatingJobFailed(SystemdManager::JobList &remaining);

private:
    bool checkAccessRights(uint uid_to_modify);
    uid_t checkCallerUid();
    bool checkIsPermissionGroup(const QStringList &groups);
    void updateEnvironment(uint uid);
    void initSystemdManager();

    QTimer *m_exitTimer;
    LibUserHelper *m_lu;
    uid_t m_switchUser;
    uid_t m_currentUid;
    SystemdManager *m_systemd;
};

#endif // SAILFISHUSERMANAGER_H
