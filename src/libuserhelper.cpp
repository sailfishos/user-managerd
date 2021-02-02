/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#include "libuserhelper.h"
#include "logging.h"

#include <libuser/user.h>
#include <QUuid>

LibUserHelper::LibUserHelper()
{
}

uint LibUserHelper::addGroup(const QString &group, int gid)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);

    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return 0;
    }

    uint rv = 0;
    struct lu_ent *ent_group = lu_ent_new();
    if (lu_group_default(context, group.toUtf8(), false, ent_group)) {
        if (gid)
            lu_ent_set_id(ent_group, LU_GIDNUMBER, gid);

        if (lu_group_add(context, ent_group, &error)) {
            rv = lu_ent_get_first_id(ent_group, LU_GIDNUMBER);
            if (rv == LU_VALUE_INVALID_ID) {
                qCWarning(lcSUM) << "Invalid group id";
                rv = 0;
            }
        } else {
            qCWarning(lcSUM) << "Adding group failed:" << lu_strerror(error);
            lu_error_free(&error);
        }
    } else {
        qCWarning(lcSUM) << "Getting group defaults failed";
    }

    lu_ent_free(ent_group);
    lu_end(context);

    return rv;
}

bool LibUserHelper::removeGroup(uint gid)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);

    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return 0;
    }

    bool rv = true;
    struct lu_ent *ent_group = lu_ent_new();
    if (lu_group_lookup_id(context, gid, ent_group, &error)) {
        if (!lu_group_delete(context, ent_group, &error)) {
            qCWarning(lcSUM) << "Group delete failed";
            lu_error_free(&error);
            rv = false;
        }
    } else {
        qCWarning(lcSUM) << "Could not find group";
        if (error)
            lu_error_free(&error);
        rv = false;
    }

    lu_ent_free(ent_group);
    lu_end(context);

    return rv;
}

bool LibUserHelper::addUserToGroup(const QString &user, const QString &group)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return false;
    }

    bool rv = true;
    struct lu_ent *ent = lu_ent_new();
    if (lu_group_lookup_name(context, group.toUtf8(), ent, &error)) {
        GValue *value = g_slice_new0(GValue);
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, user.toUtf8());
        lu_ent_add(ent, LU_MEMBERNAME, value);
        if (!lu_group_modify(context, ent, &error)) {
            qCWarning(lcSUM) << "Group modify failed:" << lu_strerror(error);
            lu_error_free(&error);
            rv = false;
        }
        g_value_unset(value);
        g_slice_free(GValue, value);
    } else {
        qCWarning(lcSUM) << "Could not find group";
        if (error)
            lu_error_free(&error);
        rv = false;
    }

    lu_ent_free(ent);
    lu_end(context);

    return rv;
}

bool LibUserHelper::removeUserFromGroup(const QString &user, const QString &group)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return false;
    }

    bool rv = true;
    struct lu_ent *ent = lu_ent_new();
    if (lu_group_lookup_name(context, group.toUtf8(), ent, &error)) {
        GValue *value = g_slice_new0(GValue);
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, user.toUtf8());
        lu_ent_del(ent, LU_MEMBERNAME, value);
        if (!lu_group_modify(context, ent, &error)) {
            qCWarning(lcSUM) << "Group modify failed:" << lu_strerror(error);
            lu_error_free(&error);
            rv = false;
        }
        g_value_unset(value);
        g_slice_free(GValue, value);
    } else {
        qCWarning(lcSUM) << "Could not find group";
        if (error)
            lu_error_free(&error);
        rv = false;
    }

    lu_ent_free(ent);
    lu_end(context);

    return rv;
}

uint LibUserHelper::addUser(const QString &user, const QString& name, uint uid, const QString &home)
{
    if (name.contains(',') || name.contains(':')) {
        qCWarning(lcSUM) << "Invalid user name, comma or colon is not allowed";
        return 0;
    }
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return 0;
    }

    uint gid = addGroup(user, uid);
    if (!gid)
        return 0;

    uint rv = 0;
    struct lu_ent *ent_user = lu_ent_new();
    if (lu_user_default(context, user.toUtf8(), false, ent_user)) {
        QString nameUuid(name);
        nameUuid += ',';
        nameUuid += QUuid::createUuid().toString();
        if (uid) {
            // Explicitly selected uid
            lu_ent_set_id(ent_user, LU_UIDNUMBER, uid);
        }
        if (!home.isEmpty()) {
            // Explicitly selected home
            lu_ent_set_string(ent_user, LU_HOMEDIRECTORY, home.toUtf8().constData());
        }

        lu_ent_set_id(ent_user, LU_GIDNUMBER, gid);
        lu_ent_set_string(ent_user, LU_GECOS, nameUuid.toUtf8());

        if (lu_user_add(context, ent_user, &error)) {
            rv = lu_ent_get_first_id(ent_user, LU_UIDNUMBER);
            if (rv == LU_VALUE_INVALID_ID) {
                qCWarning(lcSUM) << "Invalid user id";
                rv = 0;
            }
            if (uid && (rv != uid))
                qCWarning(lcSUM) << "User id" << rv << "is different from requested id" << uid;
            if (gid != rv)
                qCWarning(lcSUM) << "Group id" << gid << "is not the same as user id" << rv;
        } else {
            // Clean up after failed user add attempt
            removeGroup(gid);
            qCWarning(lcSUM) << "Adding user failed:" << lu_strerror(error);
            lu_error_free(&error);
        }
    } else {
        qCWarning(lcSUM) << "Getting user defaults failed";
    }

    lu_ent_free(ent_user);
    lu_end(context);

    return rv;
}

bool LibUserHelper::removeUser(uint uid)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return false;
    }

    bool rv = true;
    struct lu_ent *ent = lu_ent_new();
    struct lu_ent *entGroup = lu_ent_new();

    if (lu_user_lookup_id(context, uid, ent, &error)) {
        const char *user = lu_ent_get_first_string(ent, LU_USERNAME);
        // Forced to use deprecated GValueArray that libuser is using
        GValueArray *groups = lu_groups_enumerate_by_user(context, user, &error);
        if (groups) {
            GValue *value = g_slice_new0(GValue);
            g_value_init(value, G_TYPE_STRING);
            g_value_set_string(value, user);

            // Remove user from groups
            for (uint i = 0; i < groups->n_values; i++) {
                if (lu_group_lookup_name(context, g_value_get_string(g_value_array_get_nth(groups, i)), entGroup, &error)) {
                    lu_ent_del(entGroup, LU_MEMBERNAME, value);
                    if (!lu_group_modify(context, entGroup, &error)) {
                        qCWarning(lcSUM) << "Group modify failed:" << lu_strerror(error);
                        lu_error_free(&error);
                        rv = false;
                        break;
                    }
                }
            }

            g_value_unset(value);
            g_slice_free(GValue, value);
            g_value_array_free(groups);
        } else {
            if (error)
                lu_error_free(&error);
            error = nullptr;
        }

        if (lu_group_lookup_id(context, lu_ent_get_first_id(ent, LU_GIDNUMBER), entGroup, &error)) {
            if (!lu_group_delete(context, entGroup, &error)) {
                qCWarning(lcSUM) << "Group delete failed:" << lu_strerror(error);
                lu_error_free(&error);
                error = nullptr;
                rv = false;
            }
        } else {
            qCWarning(lcSUM) << "Could not find group";
            if (error) {
                lu_error_free(&error);
                error = nullptr;
            }
            rv = false;
        }

        if (!lu_user_delete(context, ent, &error)) {
            qCWarning(lcSUM) << "User delete failed:" << lu_strerror(error);
            lu_error_free(&error);
            rv = false;
        }
    } else {
        qCWarning(lcSUM) << "Could not find user";
        if (error)
            lu_error_free(&error);
        rv = false;
    }

    lu_ent_free(ent);
    lu_ent_free(entGroup);
    lu_end(context);

    return rv;
}

bool LibUserHelper::modifyUser(uint uid, const QString &newName) const
{
    if (newName.contains(',') || newName.contains(':')) {
        qCWarning(lcSUM) << "Invalid new user name, comma or colon is not allowed";
        return false;
    }
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return false;
    }

    bool rv = true;
    struct lu_ent *ent = lu_ent_new();

    if (lu_user_lookup_id(context, uid, ent, &error)) {
        QString nameUuid(newName);
        // get uuid from gecos field
        auto list = QString(lu_ent_get_first_string(ent, LU_GECOS)).split(',');
        if (list.size() > 1 && !list[1].isEmpty()) {
            // get old user uuid
            nameUuid += ',';
            nameUuid += list[1];
        } else {
            // create user uuid if not found in user db
            if (!nameUuid.endsWith(',')) nameUuid += ',';
            nameUuid += QUuid::createUuid().toString();
        }
        lu_ent_set_string(ent, LU_GECOS, nameUuid.toUtf8());
        if (!lu_user_modify(context, ent, &error)) {
            qCWarning(lcSUM) << "User modify failed:" << lu_strerror(error);
            lu_error_free(&error);
            rv = false;
        }
    } else {
        qCWarning(lcSUM) << "Could not find user";
        if (error)
            lu_error_free(&error);
        rv = false;
    }

    lu_ent_free(ent);
    lu_end(context);

    return rv;
}

QString LibUserHelper::homeDir(uint uid)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return QString();
    }

    QString rv;
    struct lu_ent *ent = lu_ent_new();
    if (lu_user_lookup_id(context, uid, ent, &error)) {
        rv = lu_ent_get_first_string(ent, LU_HOMEDIRECTORY);
    } else {
        qCWarning(lcSUM) << "Could not find user";
        if (error)
            lu_error_free(&error);
    }

    lu_ent_free(ent);
    lu_end(context);

    return rv;
}

QStringList LibUserHelper::groups(uint uid)
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return QStringList();
    }

    QStringList rv;
    struct lu_ent *ent = lu_ent_new();
    if (lu_user_lookup_id(context, uid, ent, &error)) {
        const char *user = lu_ent_get_first_string(ent, LU_USERNAME);
        GValueArray *groups = lu_groups_enumerate_by_user(context, user, &error);
        if (groups) {
            for (uint i = 0; i < groups->n_values; i++) {
                rv.append(g_value_get_string(g_value_array_get_nth(groups, i)));
            }
            g_value_array_free(groups);
        } else {
            qCWarning(lcSUM) << "Error getting users groups:" << lu_strerror(error);
            lu_error_free(&error);
        }
    } else {
        qCWarning(lcSUM) << "Could not find user";
        if (error)
            lu_error_free(&error);
    }

    lu_ent_free(ent);
    lu_end(context);

    return rv;
}

QString LibUserHelper::getUserUuid(uint uid) const
{
    struct lu_error *error = nullptr;
    struct lu_context *context = lu_start(NULL, lu_user, NULL, NULL, NULL, NULL, &error);
    if (!context) {
        qCWarning(lcSUM) << "Error creating context:" << lu_strerror(error);
        lu_error_free(&error);
        return QString();
    }

    struct lu_ent *ent = lu_ent_new();

    QString userUuid;
    if (lu_user_lookup_id(context, uid, ent, &error)) {
        QString gecos = lu_ent_get_first_string(ent, LU_GECOS);
        const auto list = gecos.split(',');
        if (list.size() > 1 && !list[1].isEmpty()) {
            userUuid = list[1];
        } else {
            // try to generate user uuid
            if (modifyUser(uid, list.count() >= 1 ? list[0] : QString())) {
                struct lu_ent *ent_new = lu_ent_new();
                if (lu_user_lookup_id(context, uid, ent_new, &error)) {
                    gecos = lu_ent_get_first_string(ent_new, LU_GECOS);
                    lu_ent_free(ent_new);
                    const auto list = gecos.split(',');
                    if (list.size() > 1 && !list[1].isEmpty()) {
                        userUuid = list[1];
                    }
                } else {
                    qCWarning(lcSUM) << "Could not find user after adding uuid";
                    if (error) {
                        qCWarning(lcSUM) << "Could not find user after adding uuid" << lu_strerror(error);
                        lu_error_free(&error);
                    }
                }
            }
        }
    } else {
        qCWarning(lcSUM) << "Could not find user";
        if (error) {
            qCWarning(lcSUM) << "Could not find user" << lu_strerror(error);
            lu_error_free(&error);
        }
    }

    lu_ent_free(ent);
    lu_end(context);

    return userUuid;
}
