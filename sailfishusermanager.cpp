/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#include "sailfishusermanager.h"
#include "usermanager_adaptor.h"
#include "libuserhelper.h"
#include "logging.h"

#include <QDBusConnection>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QString>

#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

const char *USER_GROUP = "users";
const char *GROUPS_USER = "USER_GROUPS";
const auto GROUPS_IDS_FILE = QStringLiteral("/usr/share/sailfish-setup/group.ids");
const auto SKEL_DIR = QStringLiteral("/etc/skel");
const auto USER_HOME = QStringLiteral("/home/%1");
const int HOME_MODE = 0700;
const int QUIT_TIMEOUT = 60 * 1000; // One minute quit timeout

SailfishUserManager::SailfishUserManager(QObject *parent) : QObject(parent), m_lu(new LibUserHelper())
{
    qDBusRegisterMetaType<SailfishUserManagerEntry>();
    qDBusRegisterMetaType<QList<SailfishUserManagerEntry>>();

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.registerObject(SAILFISH_USERMANAGER_DBUS_OBJECT_PATH, this)) {
        qCCritical(lcSUM, "Cannot register D-Bus object at %s", SAILFISH_USERMANAGER_DBUS_OBJECT_PATH);
    }

    if (!connection.registerService(SAILFISH_USERMANAGER_DBUS_INTERFACE)) {
        qCCritical(lcSUM, "Cannot register D-Bus service at %s", SAILFISH_USERMANAGER_DBUS_INTERFACE);
    }

    new UsermanagerAdaptor(this);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, qApp, &QCoreApplication::quit);
    m_timer->start(QUIT_TIMEOUT);
}

QList<SailfishUserManagerEntry> SailfishUserManager::users()
{
    m_timer->start();
    QList<SailfishUserManagerEntry> rv;

    struct group *grent = getgrnam(USER_GROUP);
    if (grent) {
        for (int i = 0; grent->gr_mem[i]; i++) {
            SailfishUserManagerEntry user;
            user.user = grent->gr_mem[i];
            struct passwd *pw = getpwnam(grent->gr_mem[i]);
            if (pw && pw->pw_gecos) {
                user.uid = pw->pw_uid;
                user.name = pw->pw_gecos;
                // Trim out other gecos fields
                int i = user.name.indexOf(',');
                if (i != -1)
                    user.name.remove(i, user.name.length());
            }
            rv.append(user);
        }
    } else {
        qCWarning(lcSUM) << "Getting user group failed";
        sendErrorReply(QDBusError::Failed, "Getting user group failed");
    }

    return rv;
}

bool SailfishUserManager::addUserToGroups(const QString &user)
{
    QFile file(GROUPS_IDS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSUM) << "Failed to open groups file";
        return false;
    }

    QByteArray line;

    while (!file.atEnd()) {
        line = file.readLine();
        if (line.startsWith(GROUPS_USER)) {
            foreach (const QString& group, line.mid(strlen(GROUPS_USER) + 1).split(',')) {
                if (!m_lu->addUserToGroup(user, group.trimmed())) {
                    file.close();
                    return false;
                }
            }
        }
    }
    file.close();

    return true;
}

bool SailfishUserManager::copyDir(const QString &source, const QString &destination, uint uid, uint guid)
{
    QDir sourceDir(source);
    if (!sourceDir.mkdir(destination)) {
        qCWarning(lcSUM) << "Directory already exists";
        return false;
    }
    if (chown(destination.toUtf8(), uid, guid)) {
        qCWarning(lcSUM) << "Directory ownership change failed";
        return false;
    }

    foreach (const QString &dir, sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (!copyDir(sourceDir.path() + '/' + dir, destination + '/' + dir, uid, guid))
            return false;
    }

    foreach (const QString &file, sourceDir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden)) {
        QString destFile = QString("%1/%2").arg(destination).arg(file);
        if (!QFile::copy(QString("%1/%2").arg(sourceDir.path()).arg(file), destFile)) {
            qCWarning(lcSUM) << "Failed to copy file";
            return false;
        }
        if (chown(destFile.toUtf8(), uid, guid)) {
            qCWarning(lcSUM) << "Failed to change file ownership";
            return false;
        }
    }

    return true;
}

bool SailfishUserManager::makeHome(const QString &user)
{
    struct passwd *pw = getpwnam(user.toUtf8());
    if (!pw) {
        qCWarning(lcSUM) << "User not found";
        return false;
    }

    QString destination = QString(USER_HOME).arg(user);
    if (!copyDir(SKEL_DIR, destination, pw->pw_uid, pw->pw_gid))
        return false;

    if (chmod(destination.toUtf8(), HOME_MODE)) {
        qCWarning(lcSUM) << "Home directory permissions change failed";
        return false;
    }

    return true;
}

uint SailfishUserManager::addUser(const QString &name)
{
    m_timer->start();

    if (name.isEmpty()) {
        qCWarning(lcSUM) << "Empty name";
        sendErrorReply(QDBusError::Failed, "Empty name");
        return 0;
    }

    // Parse user name
    QString simplified = name.simplified().toLower();
    QString cleanName;
    int i;
    for (i = 0; i < simplified.length(); i++) {
        if (simplified[i].isLetterOrNumber() && simplified[i] <= 'z')
            cleanName.append(name[i]);
        if (cleanName.length() >= 10)
            break;
    }
    if (cleanName.isEmpty())
        cleanName = "user";

    i = 0;
    QString user(cleanName);
    // Append number until it's unused
    while (getpwnam(user.toUtf8()))
        user = cleanName + QString::number(i++);

    if (QFile::exists(QString(USER_HOME).arg(user))) {
        qCWarning(lcSUM) << "Home directory already exists";
        sendErrorReply(QDBusError::Failed, "Home directory already exists");
        return 0;
    }

    uint guid = m_lu->addGroup(user);
    if (!guid) {
        sendErrorReply(QDBusError::Failed, "Creating user group failed");
        return 0;
    }

    uint uid = m_lu->addUser(user, name, guid);
    if (!uid) {
        m_lu->removeGroup(user);
        sendErrorReply(QDBusError::Failed, "Adding user failed");
        return 0;
    }

    if (!addUserToGroups(user)) {
        m_lu->removeUser(uid);
        sendErrorReply(QDBusError::Failed, "Adding user to groups failed");
        return 0;
    }

    if (!makeHome(user)) {
        m_lu->removeUser(uid);
        sendErrorReply(QDBusError::Failed, "Creating user home failed");
        return 0;
    }

    SailfishUserManagerEntry entry;
    entry.user = user;
    entry.name = name;
    entry.uid = uid;
    emit userAdded(entry);
    return uid;
}

void SailfishUserManager::removeUser(uint uid)
{
    m_timer->start();


    if (!removeHome(uid)) {
        sendErrorReply(QDBusError::Failed, "Removing user home failed");
        return;
    }

    if (!m_lu->removeUser(uid)) {
        sendErrorReply(QDBusError::Failed, "User remove failed");
        return;
    }

    emit userRemoved(uid);
}

void SailfishUserManager::modifyUser(uint uid, const QString &new_name)
{
    m_timer->start();

    if (!m_lu->modifyUser(uid, new_name)) {
        sendErrorReply(QDBusError::Failed, "User modify failed");
        return;
    }

    emit userModified(uid, new_name);
}

bool SailfishUserManager::removeDir(const QString &dir)
{
    QDir directory(dir);
    if (!directory.removeRecursively()) {
        qCWarning(lcSUM) << "Removing directory failed";
        return false;
    }
    return true;
}

bool SailfishUserManager::removeHome(uint uid)
{
    QString home = m_lu->homeDir(uid);
    if (home.isEmpty())
        return false;

    return removeDir(home);
}
