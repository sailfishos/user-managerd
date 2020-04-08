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
#include <sailfishaccesscontrol.h>
#include <systemd/sd-login.h>
#include <qmcecallstate.h>

const char *USER_GROUP = "users";
const char *GROUPS_USER = "USER_GROUPS";
const auto GROUPS_IDS_FILE = QStringLiteral("/usr/share/sailfish-setup/group.ids");
const auto SKEL_DIR = QStringLiteral("/etc/skel");
const auto USER_HOME = QStringLiteral("/home/%1");
const int HOME_MODE = 0700;
const int QUIT_TIMEOUT = 60 * 1000; // One minute quit timeout
const int MAX_RESERVED_UID = 99999;
const int OWNER_USER_UID = 100000;
const auto SYSTEMD_MANAGER_SERVICE = QStringLiteral("org.freedesktop.systemd1");
const auto SYSTEMD_MANAGER_PATH = QStringLiteral("/org/freedesktop/systemd1");
const auto SYSTEMD_MANAGER_INTERFACE = QStringLiteral("org.freedesktop.systemd1.Manager");
const auto SYSTEMD_MANAGER_START = QStringLiteral("StartUnit");
const auto SYSTEMD_MANAGER_STOP = QStringLiteral("StopUnit");
const auto USER_SERVICE = QStringLiteral("user@%1.service");
const auto SYSTEMD_MANAGER_REPLACE = QStringLiteral("replace");
const auto AUTOLOGIN_SERVICE = QStringLiteral("autologin@%1.service");
const auto ENVIRONMENT_FILE = QStringLiteral("/etc/environment");
const QByteArray LAST_LOGIN_UID_KEY("LAST_LOGIN_UID=");

static_assert(SAILFISH_UNDEFINED_UID > MAX_RESERVED_UID,
              "SAILFISH_UNDEFINED_UID must be in the valid range of UIDs");

SailfishUserManager::SailfishUserManager(QObject *parent) :
    QObject(parent),
    m_lu(new LibUserHelper()),
    m_switchUser(0),
    m_currentUid(0),
    m_systemd(nullptr)
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

    m_exitTimer = new QTimer(this);
    connect(m_exitTimer, &QTimer::timeout, qApp, &QCoreApplication::quit);
    m_exitTimer->start(QUIT_TIMEOUT);
}

SailfishUserManager::~SailfishUserManager()
{
    delete m_lu;
    m_lu = nullptr;
}

QList<SailfishUserManagerEntry> SailfishUserManager::users()
{
    m_exitTimer->start();
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
    // When adding user there is no uid to modify, use special value instead
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return 0;

    m_exitTimer->start();

    if (name.isEmpty()) {
        qCWarning(lcSUM) << "Empty name";
        sendErrorReply(QDBusError::InvalidArgs, "Empty name");
        return 0;
    }

    // Parse user name
    QString simplified = name.simplified().toLower();
    QString cleanName;
    int i;
    for (i = 0; i < simplified.length(); i++) {
        if (simplified[i].isLetterOrNumber() && simplified[i] <= 'z')
            cleanName.append(simplified[i]);
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
        auto message = QStringLiteral("Home directory already exists");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorHomeCreateFailed), message);
        return 0;
    }

    uint guid = m_lu->addGroup(user);
    if (!guid) {
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorGroupCreateFailed), QStringLiteral("Creating user group failed"));
        return 0;
    }

    uint uid = m_lu->addUser(user, name, guid);
    if (!uid) {
        m_lu->removeGroup(user);
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserAddFailed), QStringLiteral("Adding user failed"));
        return 0;
    }

    if (!addUserToGroups(user)) {
        m_lu->removeUser(uid);
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserModifyFailed), QStringLiteral("Adding user to groups failed"));
        return 0;
    }

    if (!makeHome(user)) {
        m_lu->removeUser(uid);
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorHomeCreateFailed), QStringLiteral("Creating user home failed"));
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
    if (!checkAccessRights(uid))
        return;

    if (uid == OWNER_USER_UID) {
        sendErrorReply(QDBusError::InvalidArgs, "Can not remove device owner");
        return;
    }

    m_exitTimer->start();

    if (!removeHome(uid)) {
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorHomeRemoveFailed), QStringLiteral("Removing user home failed"));
        return;
    }

    if (!m_lu->removeUser(uid)) {
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserRemoveFailed), QStringLiteral("User remove failed"));
        return;
    }

    emit userRemoved(uid);
}

void SailfishUserManager::modifyUser(uint uid, const QString &new_name)
{
    if (!checkAccessRights(uid))
        return;

    m_exitTimer->start();

    if (!m_lu->modifyUser(uid, new_name)) {
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserModifyFailed), QStringLiteral("User modify failed"));
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

/*
 * Gets caller uid and checks it has proper rights.
 * Returns with the uid if ok, otherwise SAILFISH_UDEFINED_UID.
 */
uid_t SailfishUserManager::checkCallerUid()
{
    if (!calledFromDBus()) {
        // Local function calls are always allowed
        return 0;
    }

    // Get the PID of the calling process
    pid_t pid = connection().interface()->servicePid(message().service());

    // The /proc/<pid> directory is owned by EUID:EGID of the process
    QFileInfo info(QString("/proc/%1").arg(pid));
    uid_t uid = info.ownerId();

    if (uid == 0) {
        // Root is always allowed to make changes
        return uid;
    }

    if (info.group() != QStringLiteral("privileged")) {
        // Non-privileged applications are not allowed
        auto message = QStringLiteral("PID %1 is not in privileged group").arg(pid);
        qCWarning(lcSUM) << "Access denied:" << message;
        sendErrorReply(QDBusError::AccessDenied, message);
        return SAILFISH_UNDEFINED_UID;
    }

    return uid;
}

/* Check that calling D-Bus client is allowed to make the operation
 *
 * uid_to_modify is uid of the user that is going to be changed or removed.
 * Special value SAILFISH_UNDEFINED_UID can be used to denote non-existing user
 * that does not match to calling process' user but is in the valid range.
 */
bool SailfishUserManager::checkAccessRights(uint uid_to_modify) {
    // Test that uid is in the valid range
    if (uid_to_modify <= MAX_RESERVED_UID) {
        // Users below MAX_RESERVED_UID are system users and can not be modified with manager
        auto message = QStringLiteral("UID %1 and below can not be modified").arg(MAX_RESERVED_UID);
        qCWarning(lcSUM) << "Invalid arg:" << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
        return false;
    }

    uid_t uid = checkCallerUid();
    if (uid == SAILFISH_UNDEFINED_UID)
        return false;

    if (uid && !sailfish_access_control_hasgroup(uid, "sailfish-system") && uid != uid_to_modify) {
        // Users in sailfish-system can change any user, other users can only modify themselves
        auto message = QStringLiteral("UID %1 is not allowed to modify UID %2").arg(uid).arg(uid_to_modify);
        qCWarning(lcSUM) << "Access denied:" << message;
        sendErrorReply(QDBusError::AccessDenied, message);
        return false;
    }

    return true;
}

void SailfishUserManager::setCurrentUser(uint uid)
{
    if (checkCallerUid() == SAILFISH_UNDEFINED_UID)
        return;

    if (m_switchUser) {
        auto message = QStringLiteral("Already switching user");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorBusy), message);
        return;
    }

    bool uidFound = false;
    struct group *grent = getgrnam(USER_GROUP);
    if (grent) {
        for (int i = 0; grent->gr_mem[i]; i++) {
            struct passwd *pw = getpwnam(grent->gr_mem[i]);
            if (pw && pw->pw_uid == uid) {
                uidFound = true;
                break;
            }
        }
    }
    if (!uidFound) {
        auto message = QStringLiteral("User not found");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserNotFound), message);
        return;
    }

    m_currentUid = currentUser();
    if (m_currentUid == SAILFISH_UNDEFINED_UID)
        return;

    if (m_currentUid == uid) {
        auto message = QStringLiteral("User already active");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
        return;
    }

    QMceCallState callState;
    if (callState.state() == QMceCallState::Active || callState.state() == QMceCallState::Ringing) {
        auto message = QStringLiteral("Call active");
        qCWarning(lcSUM) << message;
        sendErrorReply(SailfishUserManagerErrorBusy, message);
        return;
    }

    emit aboutToChangeCurrentUser(uid);

    m_switchUser = uid;
    m_exitTimer->start();

    if (!m_systemd)
        m_systemd = new QDBusInterface(SYSTEMD_MANAGER_SERVICE, SYSTEMD_MANAGER_PATH, SYSTEMD_MANAGER_INTERFACE, QDBusConnection::systemBus());

    // Stop user service
    QDBusPendingCall call = m_systemd->asyncCall(SYSTEMD_MANAGER_STOP, USER_SERVICE.arg(m_currentUid), SYSTEMD_MANAGER_REPLACE);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &SailfishUserManager::userServiceStop);
}

void SailfishUserManager::userServiceStop(QDBusPendingCallWatcher *replyWatcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *replyWatcher;
    if (reply.isError()) {
        qCWarning(lcSUM) << QStringLiteral("Failed to stop current user session: %1").arg(reply.error().message());
        emit currentUserChangeFailed(m_switchUser);
        m_switchUser = 0;
    } else {
        // Stop autologin service
        QDBusPendingCall call = m_systemd->asyncCall(SYSTEMD_MANAGER_STOP, AUTOLOGIN_SERVICE.arg(m_currentUid), SYSTEMD_MANAGER_REPLACE);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, &SailfishUserManager::autologinServiceStop);
    }
    replyWatcher->deleteLater();
}

void SailfishUserManager::autologinServiceStop(QDBusPendingCallWatcher *replyWatcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *replyWatcher;
    if (reply.isError()) {
        qCWarning(lcSUM) << QStringLiteral("Failed to stop current user autologin: %1").arg(reply.error().message());
        emit currentUserChangeFailed(m_switchUser);
        m_switchUser = 0;

        // Try to recover by restarting the old user service
        m_systemd->asyncCall(SYSTEMD_MANAGER_START, USER_SERVICE.arg(m_currentUid), SYSTEMD_MANAGER_REPLACE);
    } else {
        // Start new autologin service
        QDBusPendingCall call = m_systemd->asyncCall(SYSTEMD_MANAGER_START, AUTOLOGIN_SERVICE.arg(m_switchUser), SYSTEMD_MANAGER_REPLACE);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, &SailfishUserManager::autologinServiceStart);
    }
    replyWatcher->deleteLater();
}

void SailfishUserManager::autologinServiceStart(QDBusPendingCallWatcher *replyWatcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *replyWatcher;
    if (reply.isError()) {
        qCWarning(lcSUM) << QStringLiteral("Failed to start autologin: %1").arg(reply.error().message());
        emit currentUserChangeFailed(m_switchUser);

        // Try to recover by restarting the old autologin service
        m_systemd->asyncCall(SYSTEMD_MANAGER_START, AUTOLOGIN_SERVICE.arg(m_currentUid), SYSTEMD_MANAGER_REPLACE);
    } else {
        emit currentUserChanged(m_switchUser);
        updateEnvironment(m_switchUser);
    }
    m_switchUser = 0;
    replyWatcher->deleteLater();
}

uint SailfishUserManager::currentUser()
{
    m_exitTimer->start();
    uid_t uid;
    if (sd_seat_get_active("seat0", nullptr, &uid) < 0) {
        auto message = QStringLiteral("Failed to get current user id");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorGetUidFailed), message);
        return SAILFISH_UNDEFINED_UID;
    }
    return uid;
}

void SailfishUserManager::updateEnvironment(uint uid)
{
    QFile file(ENVIRONMENT_FILE);
    if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        QByteArray line;
        QByteArray rest;
        while (!(line = file.readLine()).isEmpty()) {
            if (line.startsWith(LAST_LOGIN_UID_KEY)) {
                qint64 pos = file.pos();
                rest = file.readAll();
                file.seek(pos - line.length());
                break;
            }
        }
        file.write(LAST_LOGIN_UID_KEY);
        file.write(QByteArray::number(uid));
        file.write("\n");
        file.write(rest);
        file.resize(file.pos());
        file.close();
    }
}

QStringList SailfishUserManager::usersGroups(uint uid)
{
    m_exitTimer->start();
    return m_lu->groups(uid);
}

void SailfishUserManager::addToGroups(uint uid, const QStringList &groups)
{
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return;

    m_exitTimer->start();

    struct passwd *pwd = getpwuid(uid);
    if (!pwd) {
        auto message = QStringLiteral("User not found");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserNotFound), message);
        return;
    }

    QStringList original = m_lu->groups(uid);
    QStringList revert;
    foreach (const QString &group, groups) {
        if (!original.contains(group)) {
            if (m_lu->addUserToGroup(pwd->pw_name, group)) {
                revert.append(group);
            } else {
                auto message = QStringLiteral("Failed to add user to group");
                qCWarning(lcSUM) << message;
                sendErrorReply(QStringLiteral(SailfishUserManagerErrorAddToGroupFailed), message);

                // Revert back to original groups
                foreach (const QString &newGroup, revert)
                    m_lu->removeUserFromGroup(pwd->pw_name, newGroup);

                return;
            }
        }
    }
}

void SailfishUserManager::removeFromGroups(uint uid, const QStringList &groups)
{
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return;

    m_exitTimer->start();

    struct passwd *pwd = getpwuid(uid);
    if (!pwd) {
        auto message = QStringLiteral("User not found");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserNotFound), message);
        return;
    }

    QStringList original = m_lu->groups(uid);
    QStringList revert;

    foreach (const QString &group, groups) {
        if (original.contains(group)) {
            if (m_lu->removeUserFromGroup(pwd->pw_name, group)) {
                revert.append(group);
            } else {
                auto message = QStringLiteral("Failed to remove user from group");
                qCWarning(lcSUM) << message;
                sendErrorReply(QStringLiteral(SailfishUserManagerErrorRemoveFromGroupFailed), message);

                // Revert back to original groups
                foreach (const QString &oldGroup, revert)
                    m_lu->addUserToGroup(pwd->pw_name, oldGroup);

                return;
            }
        }
    }
}
