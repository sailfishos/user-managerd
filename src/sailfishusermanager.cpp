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
#include "systemdmanager.h"
#include "logging.h"

#include <QDBusConnection>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QString>
#include <QCollator>

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <qmcecallstate.h>
#include <sailfishaccesscontrol.h>
#include <sys/mount.h>
#include <linux/quota.h>
#include <sys/quota.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <systemd/sd-login.h>
#include <unistd.h>

namespace {

const char *USER_GROUP = "users";
const auto GROUP_IDS_FILE = QStringLiteral("/usr/share/sailfish-setup/group_ids.env");
const char *GROUP_IDS_KEY_PREFIX = "USER_GROUPS";
const char GROUP_IDS_VALUE_SEPARATOR = '=';
const char GROUP_IDS_GROUP_SEPARATOR = ',';
const auto SKEL_DIR = QStringLiteral("/etc/skel");
const auto USER_HOME = QStringLiteral("/home/%1");
const auto GUEST_USER = QStringLiteral("sailfish-guest");
const int HOME_MODE = 0700;
const int QUIT_TIMEOUT = 60 * 1000; // One minute quit timeout
const int SWITCHING_DELAY = 1000; // One second time before changing currentUser
const int MAX_RESERVED_UID = 99999;
const int OWNER_USER_UID = 100000;
const auto DEFAULT_TARGET = QStringLiteral("default.target");
const auto USER_SERVICE = QStringLiteral("user@%1.service");
const auto AUTOLOGIN_SERVICE = QStringLiteral("autologin@%1.service");
const auto ENVIRONMENT_FILE = QStringLiteral("/etc/environment");
const QByteArray LAST_LOGIN_UID_KEY("LAST_LOGIN_UID=");
const int MAX_USERNAME_LENGTH = 20;
const auto USER_ENVIRONMENT_DIR = QStringLiteral("/home/.system/var/lib/environment/%1");
const auto USER_REMOVE_SCRIPT_DIR = QStringLiteral("/usr/share/user-managerd/remove.d");
const auto USER_CREATE_SCRIPT_DIR = QStringLiteral("/usr/share/user-managerd/create.d");
const quint64 MAXIMUM_QUOTA_LIMIT = 2000000000ULL;
const auto SAILFISH_GROUP_PREFIX = QStringLiteral("sailfish-");
const auto ACCOUNT_GROUP_PREFIX = QStringLiteral("account-");

static_assert(SAILFISH_UNDEFINED_UID > MAX_RESERVED_UID,
              "SAILFISH_UNDEFINED_UID must be in the valid range of UIDs");

QByteArray findHomeDevice()
{
    QStorageInfo info(USER_HOME.arg(""));
    return info.device();
}

};

/* Try to keep documentation inside 80 character limit, please. */

/*!
  \class SailfishUserManager
  \inmodule Sailfish User Manager Daemon

  \brief The SailfishUserManager provides tools for user management.

  This is a D-Bus interface to list, create, remove and modify users. It is
  provided by \c org.sailfishos.usermanager service on system bus with path \c
  / and interface \c org.sailfishos.usermanager.

  Useful defines, \l SailfishUserManagerEntry struct and serializing operators
  are provided by \l <sailfishusermanagerinterface.h>. It can be used directly
  with Qt projects utilizing this interface.

  Some operations may return \c QDBusError::AccessDenied if caller is not
  authorized to do them and \c QDBusError::InvalidArgs if arguments are not
  acceptable.
 */

/*!
  \brief Constructs SailfishUserManager, for internal use only.
  \internal
 */
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
    connect(m_exitTimer, &QTimer::timeout, this, &SailfishUserManager::exitTimeout);
    m_exitTimer->start(QUIT_TIMEOUT);
}

/*!
  \brief Destructs SailfishUserManager, for internal use only.
  \internal
 */
SailfishUserManager::~SailfishUserManager()
{
    delete m_lu;
    m_lu = nullptr;
}

void SailfishUserManager::exitTimeout()
{
    // Quit if user switching is not in progress
    if (m_switchUser == 0) {
        qCDebug(lcSUM) << "Exit timeout reached, quitting";
        qApp->quit();
    } else {
        qCDebug(lcSUM) << "User switching in progress, not quitting yet";
    }
}

/*!
  \brief List users on device.

  Returns list of \l SailfishUserManagerEntry structs.

  If list of users can not be fetched this returns error \c QDBusError::Failed.
 */
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
                rv.append(user);
            }
        }
    } else {
        auto message = QStringLiteral("Getting user group failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::Failed, message);
    }

    return rv;
}

bool SailfishUserManager::addUserToGroups(const QString &user)
{
    QFile file(GROUP_IDS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSUM) << "Failed to open groups file";
        return false;
    }

    QByteArray line;

    bool success = true;
    while (!file.atEnd()) {
        line = file.readLine();
        if (line.startsWith(GROUP_IDS_KEY_PREFIX) && line.contains(GROUP_IDS_VALUE_SEPARATOR)) {
            QByteArray groups = line.mid(line.indexOf(GROUP_IDS_VALUE_SEPARATOR)+1).trimmed();
            for (const QString& group : groups.split(GROUP_IDS_GROUP_SEPARATOR)) {
                if (!m_lu->addUserToGroup(user, group.trimmed())) {
                    qCWarning(lcSUM) << "Failed to add" << user << "to group" << group.trimmed();
                    success = false;
                }
            }
        }
    }
    file.close();

    return success;
}

bool SailfishUserManager::copyDir(const QString &source, const QString &destination, uint uid, uint guid)
{
    QDir sourceDir(source);
    if (!sourceDir.exists(destination) && !sourceDir.mkdir(destination)) {
        qCWarning(lcSUM) << "Directory create failed";
        return false;
    }
    if (chown(destination.toUtf8(), uid, guid)) {
        qCWarning(lcSUM) << "Directory ownership change failed";
        return false;
    }

    for (const QString &dir : sourceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (!copyDir(sourceDir.path() + '/' + dir, destination + '/' + dir, uid, guid))
            return false;
    }

    for (const QString &file : sourceDir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden)) {
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

    QString destination = pw->pw_dir;

    if (!copyDir(SKEL_DIR, destination, pw->pw_uid, pw->pw_gid))
        return false;

    if (chmod(destination.toUtf8(), HOME_MODE)) {
        qCWarning(lcSUM) << "Home directory permissions change failed";
        return false;
    }

    return true;
}

/*!
  \brief Creates user with \a name as real name.

  New users can not be created if there are already maximum number of users.
  \e {Guest user} is not counted to this maximum. \a name must not be empty and
  it may not contain \c : or \c , characters. In practice it is recommended to
  only have letters, numbers, spaces, dots and dashes in \a name.

  Returns \e UID (\e {User IDentifier}) of the new user. Use \l users to get
  \e username if needed.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorMaxUsersReached},
  \l {D-Bus errors} {SailfishUserManagerErrorUserAddFailed},
  \l {D-Bus errors} {SailfishUserManagerErrorUserModifyFailed} and
  \l {D-Bus errors} {SailfishUserManagerErrorHomeCreateFailed}.
 */
uint SailfishUserManager::addUser(const QString &name)
{
    // When adding user there is no uid to modify, use special value instead
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return 0;

    m_exitTimer->start();

    if (name.isEmpty()) {
        auto message = QStringLiteral("Empty name");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
        return 0;
    }

    int count = 0;
    struct group *grent = getgrnam(USER_GROUP);
    if (grent) {
        for (int i = 0; grent->gr_mem[i]; i++) {
            // Guest user is not counted to number of users that can be created
            struct passwd *pw = getpwnam(grent->gr_mem[i]);
            if (pw && (pw->pw_uid != SAILFISH_USERMANAGER_GUEST_UID))
                count++;
        }
    }
    if (count > (SAILFISH_USERMANAGER_MAX_USERS - 1)) {
        // Master user reserves one slot above
        auto message = QStringLiteral("Maximum number of users reached");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorMaxUsersReached), message);
        return 0;
    }

    // Parse user name
    QString simplified = name.simplified().toLower();
    QString cleanName;
    int i;
    for (i = 0; i < simplified.length() && cleanName.length() < MAX_USERNAME_LENGTH; i++) {
        if (simplified[i].isLetterOrNumber() && simplified[i] <= 'z')
            cleanName.append(simplified[i]);
    }
    if (cleanName.isEmpty())
        cleanName = "user";

    i = 0;
    QString user(cleanName);
    // Append number until it's unused
    while (getpwnam(user.toUtf8()) || getgrnam(user.toUtf8())
            || QFile::exists(QString(USER_HOME).arg(user))) {
        user = cleanName + QString::number(i++);
    }

    return addSailfishUser(user, name);
}

void SailfishUserManager::executeScripts(uint uid, const QString &directory)
{
    QDir scripts(directory, "*.sh", QDir::NoSort, QDir::Files | QDir::Executable);

    auto entryList = scripts.entryList();

    QCollator collator(QLocale::C);
    collator.setNumericMode(true);

    std::sort(entryList.begin(), entryList.end(), collator);

    for (const QString &entry : entryList) {
        int exitCode = QProcess::execute(directory + '/' + entry, QStringList() << QString::number(uid));
        if (exitCode)
            qCWarning(lcSUM) << "User scripts" << directory + '/' + entry << "returned:" << exitCode;
    }
}

uint SailfishUserManager::addSailfishUser(const QString &user, const QString &name, uint userId, const QString &home)
{
    uint uid = m_lu->addUser(user, name, userId, home);
    if (!uid) {
        auto message = QStringLiteral("Adding user failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserAddFailed), message);
        return 0;
    }

    if (!addUserToGroups(user)) {
        m_lu->removeUser(uid);
        auto message = QStringLiteral("Adding user to groups failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserModifyFailed), message);
        return 0;
    }

    if (userId != SAILFISH_USERMANAGER_GUEST_UID && !makeHome(user)) {
        m_lu->removeUser(uid);
        auto message = QStringLiteral("Creating user home failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorHomeCreateFailed), message);
        return 0;
    }

    // Execute user creation scripts
    executeScripts(uid, USER_CREATE_SCRIPT_DIR);

    setUserLimits(uid);

    SailfishUserManagerEntry entry;
    entry.user = user;
    entry.name = name;
    entry.uid = uid;
    emit userAdded(entry);

    return uid;
}

int SailfishUserManager::removeUserFiles(uint uid)
{
    int rv = EXIT_FAILURE;
    QDir dir(USER_ENVIRONMENT_DIR.arg(uid));
    if (dir.removeRecursively())
        rv = EXIT_SUCCESS;
    else
        qCWarning(lcSUM) << "Removing user environment directory failed";

    // Execute user removal scripts
    executeScripts(uid, USER_REMOVE_SCRIPT_DIR);

    return rv;
}

/*!
  \brief Sets user quota limits, if supported by kernel and enabled on /home
  filesystem.
 */
void SailfishUserManager::setUserLimits(uint uid)
{
    struct statvfs info;
    memset(&info, 0, sizeof(info));
    errno = 0;
    if (statvfs(USER_HOME.arg("").toUtf8().data(), &info) < 0) {
        qCWarning(lcSUM) << "Could not set limits, could not stat filesystem:" << strerror(errno);
    } else {
        // Soft limit of max(20 %, MAXIMUM_QUOTA_LIMIT), soft limit turns into hard after grace period
        fsblkcnt_t softlimit = info.f_blocks * 20 / 100;
        if (softlimit > (fsblkcnt_t)(MAXIMUM_QUOTA_LIMIT / info.f_frsize))
            softlimit = (fsblkcnt_t)(MAXIMUM_QUOTA_LIMIT / info.f_frsize);
        // Hard limit is 120 % of soft limit
        fsblkcnt_t hardlimit = softlimit * 120 / 100;
        qCDebug(lcSUM) << "Setting quota limits for" << uid << "to"
                       << hardlimit << "and" << softlimit << "blocks of size" << info.f_frsize;
        // Sets block limits and clears inode limits
        struct if_dqblk quota = {
            .dqb_bhardlimit = fs_to_dq_blocks(hardlimit, info.f_frsize),
            .dqb_bsoftlimit = fs_to_dq_blocks(softlimit, info.f_frsize),
            .dqb_curspace = 0,
            .dqb_ihardlimit = 0,
            .dqb_isoftlimit = 0,
            .dqb_curinodes = 0,
            .dqb_btime = 0,
            .dqb_itime = 0,
            .dqb_valid = QIF_LIMITS
        };
        errno = 0;
        if (quotactl(QCMD(Q_SETQUOTA, USRQUOTA), findHomeDevice().data(), (uid_t)uid, (caddr_t)&quota) < 0) {
            if (errno == ENOSYS) {
                qCWarning(lcSUM) << "Could not set limits, kernel doesn't support it";
            } else if (errno == ESRCH) {
                qCWarning(lcSUM) << "Could not set limits, it is not enabled on the filesystem";
            } else {
                // Unexpected cases
                qCWarning(lcSUM) << "Could not set limits:" << strerror(errno);
            }
        }
    }
}

/*!
  \brief Removes extra files for user, for internal use only.
  \internal
 */
int SailfishUserManager::removeUserFiles(const char *user)
{
    struct passwd *pwd = getpwnam(user);
    if (pwd)
        return removeUserFiles(pwd->pw_uid);
    return EXIT_FAILURE;
}

/*!
  \brief Removes user with given \a uid from the system.

  Only \e {additional users} can be removed. Current user or \e {Device owner}
  can not be removed. See \l enableGuestUser for disabling \e {guest user}.

  \warning Removing a user destroys all data for that user.

  This may return error
  \l {D-Bus errors} {SailfishUserManagerErrorUserRemoveFailed}.
 */
void SailfishUserManager::removeUser(uint uid)
{
    if (!checkAccessRights(uid))
        return;

    if (uid == OWNER_USER_UID) {
        auto message = QStringLiteral("Can not remove device owner");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
        return;
    }

    if (uid == currentUser()) {
        auto message = QStringLiteral("Can not remove current user");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
        return;
    }

    m_exitTimer->start();

    if (uid != SAILFISH_USERMANAGER_GUEST_UID && !removeHome(uid)) {
        qCWarning(lcSUM) << "Removing user home failed";
    }

    removeUserFiles(uid);

    if (!m_lu->removeUser(uid)) {
        auto message = QStringLiteral("User remove failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserRemoveFailed), message);
    } else {
        emit userRemoved(uid);
    }
}

/*!
  \brief Changes real name to \a new_name for user with given \a uid.
 */
void SailfishUserManager::modifyUser(uint uid, const QString &new_name)
{
    if (!checkAccessRights(uid))
        return;

    m_exitTimer->start();

    if (!m_lu->modifyUser(uid, new_name)) {
        auto message = QStringLiteral("User modify failed");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserModifyFailed), message);
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

/*!
  \brief Gets caller uid and checks it has proper rights.

  Returns with the uid if ok, otherwise \c SAILFISH_UNDEFINED_UID.
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

    if (info.group() != QStringLiteral("privileged") && !sailfish_access_control_hasgroup(uid, "privileged")) {
        // Non-privileged applications are not allowed
        auto message = QStringLiteral("PID %1 is not in privileged group").arg(pid);
        qCWarning(lcSUM) << "Access denied:" << message;
        sendErrorReply(QDBusError::AccessDenied, message);
        return SAILFISH_UNDEFINED_UID;
    }

    return uid;
}

/*!
  \brief Check that calling D-Bus client is allowed to make the operation.

  \a uid_to_modify is \e UID of the user that is going to be changed or
  removed. Special value \c SAILFISH_UNDEFINED_UID can be used to denote
  non-existing user that does not match to calling process' user but is in the
  valid range.
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

void SailfishUserManager::initSystemdManager()
{
    m_systemd = new SystemdManager(this);
    connect(m_systemd, &SystemdManager::busyChanged, this, &SailfishUserManager::onBusyChanged);
    connect(m_systemd, &SystemdManager::unitJobFinished, this, &SailfishUserManager::onUnitJobFinished);
    connect(m_systemd, &SystemdManager::unitJobFailed, this, &SailfishUserManager::onUnitJobFailed);
    connect(m_systemd, &SystemdManager::creatingJobFailed, this, &SailfishUserManager::onCreatingJobFailed);
}

/*!
  \brief Sets current user to user with given \a uid.

  This will end current user session and start user session for \a uid
  which must be different from current user's \e UID.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorGetUidFailed},
  \l {D-Bus errors} {SailfishUserManagerErrorBusy} and
  \l {D-Bus errors} {SailfishUserManagerErrorUserNotFound}.
 */
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

    m_currentUid = currentUser();
    if (m_currentUid == SAILFISH_UNDEFINED_UID)
        return;

    if (m_currentUid == uid) {
        auto message = QStringLiteral("User already active");
        qCWarning(lcSUM) << message;
        sendErrorReply(QDBusError::InvalidArgs, message);
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

    QMceCallState callState;
    if (callState.state() == QMceCallState::Active || callState.state() == QMceCallState::Ringing) {
        auto message = QStringLiteral("Call active");
        qCWarning(lcSUM) << message;
        sendErrorReply(SailfishUserManagerErrorBusy, message);
        return;
    }

    qCDebug(lcSUM) << "About to switch user to uid" << uid;
    emit aboutToChangeCurrentUser(uid);

    m_switchUser = uid;

    // Remove guest user's extra data, if there is any left from a previous session
    if (uid == SAILFISH_USERMANAGER_GUEST_UID)
        removeUserFiles(SAILFISH_USERMANAGER_GUEST_UID);

    QTimer::singleShot(SWITCHING_DELAY, [this] {
        if (!m_systemd) {
            initSystemdManager();
        }
        qCDebug(lcSUM) << "Switching user from" << m_currentUid << "to" << m_switchUser << "now";
        m_systemd->addUnitJobs(SystemdManager::JobList()
                               << SystemdManager::Job::stop(USER_SERVICE.arg(m_currentUid))
                               << SystemdManager::Job::stop(AUTOLOGIN_SERVICE.arg(m_currentUid))
                               << SystemdManager::Job::start(AUTOLOGIN_SERVICE.arg(m_switchUser))
                               << SystemdManager::Job::start(USER_SERVICE.arg(m_switchUser), false));
    });
}

void SailfishUserManager::onBusyChanged()
{
    if (!m_systemd->busy()) {
        qCDebug(lcSUM) << "Systemd job queue cleared, can exit";
        m_exitTimer->start();
    }
}

void SailfishUserManager::onUnitJobFinished(SystemdManager::Job &job)
{
    if (job.type == SystemdManager::StartJob && job.unit == USER_SERVICE.arg(m_switchUser)) {
        // Everything went well
        emit currentUserChanged(m_switchUser);
        updateEnvironment(m_switchUser);
        m_switchUser = 0;
    } else if (job.type == SystemdManager::StartJob && job.unit == DEFAULT_TARGET) {
        // Backup plan
        if (m_currentUid != currentUser())
            emit currentUserChanged(currentUser());
    }// else it's not interesting
}

void SailfishUserManager::onUnitJobFailed(SystemdManager::Job &job, SystemdManager::JobList &remaining) {
    if (job.type == SystemdManager::StopJob && job.unit == USER_SERVICE.arg(m_currentUid)) {
        // session systemd is fubar, autologin is probably still up
        qCWarning(lcSUM) << "Unit failed while stopping session, trying to continue";
        m_systemd->addUnitJobs(remaining); // Try to continue anyway
    } else if (job.type == SystemdManager::StopJob && job.unit == AUTOLOGIN_SERVICE.arg(m_currentUid)) {
        // session systemd is down, autologind stop failed
        qCWarning(lcSUM) << "Autologin failed while stopping it, trying to continue";
        m_systemd->addUnitJobs(remaining); // Try to continue anyway
    } else if (job.type == SystemdManager::StartJob && job.unit == AUTOLOGIN_SERVICE.arg(m_switchUser)) {
        // session systemd is already down, autologind didn't come back again
        // Try to start to user session normally still
        qCWarning(lcSUM) << "User session start failed, trying to start default target as fallback";
        m_systemd->addUnitJob(SystemdManager::Job::start(DEFAULT_TARGET));
        m_switchUser = 0;
        // Inform UI
        emit currentUserChangeFailed(m_switchUser);
    } else if (job.type == SystemdManager::StartJob && job.unit == USER_SERVICE.arg(m_switchUser)) {
        // autologind was started but starting user@.service failed, probably because it was already starting
        qCWarning(lcSUM) << "Starting session systemd failed, is it already starting?";
        m_switchUser = 0;
        // Inform UI
        emit currentUserChangeFailed(m_switchUser);
    }
}

void SailfishUserManager::onCreatingJobFailed(SystemdManager::JobList &remaining) {
    if (remaining.count() == 1) {
        if (remaining.first().unit == USER_SERVICE.arg(m_switchUser)) {
            // autologind was started but session systemd wasn't, probably because it was already starting
            qCWarning(lcSUM) << "Could not start session systemd, is it already starting?";
        } // else it was DEFAULT_TARGET and there isn't much that can be done
    } else if (remaining.count() == 2) {
        if (remaining.first().unit == AUTOLOGIN_SERVICE.arg(m_switchUser)) {
            // Try to start to user session normally still
            qCWarning(lcSUM) << "Could not start user session, trying to start default target as fallback";
            m_systemd->addUnitJob(SystemdManager::Job::start(DEFAULT_TARGET));
        }
    } else if (remaining.count() == 3) {
        if (remaining.first().unit == AUTOLOGIN_SERVICE.arg(m_currentUid)) {
            // session systemd is stopped but autologin is still up and it wasn't brought down
            // TODO: What to do?
            qCWarning(lcSUM) << "Could not stop autologin, user switch failed";
            // Inform UI
            emit currentUserChangeFailed(m_switchUser);
        }
    } else { // nothing was done
        qCWarning(lcSUM) << "User switching did not begin";
        emit currentUserChangeFailed(m_switchUser);
    }
    m_switchUser = 0;
}

/*!
  \brief Returns current user's \e UID (\e {User IDentifier}).

  Current user is the user that is using the device, i.e. is active on \c
  seat0.
 */
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

/*!
  \brief Returns \e UUID (\e {Universally Unique IDentifier}) for current
  user.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorGetUidFailed} and
  \l {D-Bus errors} {SailfishUserManagerErrorGetUuidFailed}.
 */
QString SailfishUserManager::currentUserUuid()
{
    const auto uid = currentUser();
    if (uid == SAILFISH_UNDEFINED_UID) {
        return QString();
    }
    return userUuid(uid);
}

/*!
  \brief Returns \e UUID (\e {Universally Unique IDentifier}) for user with
  given \a uid.

  This may return error
  \l {D-Bus errors} {SailfishUserManagerErrorGetUuidFailed}.
 */
QString SailfishUserManager::userUuid(uint uid)
{
    m_exitTimer->start();
    const auto userUuid = m_lu->getUserUuid(uid);
    if (userUuid.isEmpty()) {
        auto message = QStringLiteral("Failed to get user uuid");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorGetUuidFailed), message);
        return QString();
    }
    return userUuid;
}

void SailfishUserManager::updateEnvironment(uint uid)
{
    // Nothing here for guest
    if (uid == SAILFISH_USERMANAGER_GUEST_UID)
        return;

    // Remove guest user's extra data
    if (m_currentUid == SAILFISH_USERMANAGER_GUEST_UID)
        removeUserFiles(SAILFISH_USERMANAGER_GUEST_UID);

    if (uid < MAX_RESERVED_UID || uid > MAX_RESERVED_UID + SAILFISH_USERMANAGER_MAX_USERS) {
        // This could be also an assert but it only results in device booting up as wrong user
        qCWarning(lcSUM) << "updateEnvironment: uid" << uid
                         << "is outside allowed range. Not setting LAST_LOGIN_UID.";
        return;
    }

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

/*!
  \brief Returns groups for user with given \a uid.
 */
QStringList SailfishUserManager::usersGroups(uint uid)
{
    m_exitTimer->start();
    return m_lu->groups(uid);
}

/*!
  \brief Adds user with given \a uid to \a groups.

  This is used to add permissions for user. All groups must begin with
  \c sailfish- or \c account- prefix.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorUserNotFound} and
  \l {D-Bus errors} {SailfishUserManagerErrorAddToGroupFailed}.
 */
void SailfishUserManager::addToGroups(uint uid, const QStringList &groups)
{
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return;

    m_exitTimer->start();

    if (!checkIsPermissionGroup(groups))
        return;

    struct passwd *pwd = getpwuid(uid);
    if (!pwd) {
        auto message = QStringLiteral("User not found");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserNotFound), message);
        return;
    }

    QStringList original = m_lu->groups(uid);
    QStringList revert;
    for (const QString &group : groups) {
        if (!original.contains(group)) {
            if (m_lu->addUserToGroup(pwd->pw_name, group)) {
                revert.append(group);
            } else {
                auto message = QStringLiteral("Failed to add user to group");
                qCWarning(lcSUM) << message;
                sendErrorReply(QStringLiteral(SailfishUserManagerErrorAddToGroupFailed), message);

                // Revert back to original groups
                for (const QString &newGroup : revert)
                    m_lu->removeUserFromGroup(pwd->pw_name, newGroup);

                return;
            }
        }
    }
}

/*!
  \brief Removes user with \a uid from \a groups.

  This is used to remove permissions from user. All groups must begin with
  \c sailfish- or \c account- prefix.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorUserNotFound} and
  \l {D-Bus errors} {SailfishUserManagerErrorRemoveFromGroupFailed}.
 */
void SailfishUserManager::removeFromGroups(uint uid, const QStringList &groups)
{
    if (!checkAccessRights(SAILFISH_UNDEFINED_UID))
        return;

    m_exitTimer->start();

    if (!checkIsPermissionGroup(groups))
        return;

    struct passwd *pwd = getpwuid(uid);
    if (!pwd) {
        auto message = QStringLiteral("User not found");
        qCWarning(lcSUM) << message;
        sendErrorReply(QStringLiteral(SailfishUserManagerErrorUserNotFound), message);
        return;
    }

    QStringList original = m_lu->groups(uid);
    QStringList revert;

    for (const QString &group : groups) {
        if (original.contains(group)) {
            if (m_lu->removeUserFromGroup(pwd->pw_name, group)) {
                revert.append(group);
            } else {
                auto message = QStringLiteral("Failed to remove user from group");
                qCWarning(lcSUM) << message;
                sendErrorReply(QStringLiteral(SailfishUserManagerErrorRemoveFromGroupFailed), message);

                // Revert back to original groups
                for (const QString &oldGroup : revert)
                    m_lu->addUserToGroup(pwd->pw_name, oldGroup);

                return;
            }
        }
    }
}

bool SailfishUserManager::checkIsPermissionGroup(const QStringList &groups)
{
    for (const auto group : groups) {
        if (!group.startsWith(SAILFISH_GROUP_PREFIX)
                && !group.startsWith(ACCOUNT_GROUP_PREFIX)) {
            auto message = QStringLiteral("Only %1 and %2 groups can be managed")
                .arg(SAILFISH_GROUP_PREFIX).arg(ACCOUNT_GROUP_PREFIX);
            qCWarning(lcSUM) << message;
            sendErrorReply(QDBusError::InvalidArgs, message);
            return false;
        }
    }
    return true;
}

/*!
  \brief Enables or disables \e {guest user}.

  Argument \a enable chooses whether \e {guest user} should be enabled or not.

  This may return errors
  \l {D-Bus errors} {SailfishUserManagerErrorUserAddFailed},
  \l {D-Bus errors} {SailfishUserManagerErrorUserModifyFailed} and
  \l {D-Bus errors} {SailfishUserManagerErrorUserRemoveFailed}.
 */
void SailfishUserManager::enableGuestUser(bool enable)
{
    if (!checkAccessRights(SAILFISH_USERMANAGER_GUEST_UID))
        return;

    if (enable != (bool)getpwuid(SAILFISH_USERMANAGER_GUEST_UID)) {
        if (enable) {
            if (addSailfishUser(GUEST_USER, "", SAILFISH_USERMANAGER_GUEST_UID, SAILFISH_USERMANAGER_GUEST_HOME))
                emit guestUserEnabled(true);
        } else {
            removeUser(SAILFISH_USERMANAGER_GUEST_UID);
            if (!getpwuid(SAILFISH_USERMANAGER_GUEST_UID))
                emit guestUserEnabled(false);
        }
    }
}

/*!
  \fn void SailfishUserManager::userAdded(const SailfishUserManagerEntry &user)

  \brief Triggered when a new user has been added.

  User information is contained in \a user.

  \sa SailfishUserManagerEntry
 */

/*!
  \fn void SailfishUserManager::userRemoved(uint uid)

  \brief Triggered when user with \a uid has been removed.
 */

/*!
  \fn void SailfishUserManager::userModified(uint uid, const QString &new_name)

  \brief Triggered when user's real name has been changed.

  User with \a uid has \a new_name as their new real name.
 */

/*!
  \fn void SailfishUserManager::currentUserChanged(uint uid)

  \brief Triggered when current user is changed.

  Current user is set to user with \a uid.
 */

/*!
  \fn void SailfishUserManager::currentUserChangeFailed(uint uid)

  \brief Triggered when changing current user fails.

  This follows \l aboutToChangeCurrentUser when changing current user to user
  with \a uid fails.

  The system may be in unusable state after this signal or it may have
  recovered successfully. Rebooting device usually recovers this.
 */

/*!
  \fn void SailfishUserManager::aboutToChangeCurrentUser(uint uid)

  \brief Triggered when system is about to change current user to user with \a
  uid.

  This is mainly useful for user interface to show information about switching
  users. User session is ended a moment later and \l currentUserChanged will
  follow this signal.
 */

/*!
  \fn void SailfishUserManager::guestUserEnabled(bool enabled)

  \brief Triggered when \e {guest user} is enabled or disabled.

  If \a enabled is \c true, \e {guest user} is enabled, otherwise it is
  disabled.
 */
