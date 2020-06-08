/*
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#ifndef SYSTEMDMANAGER_H
#define SYSTEMDMANAGER_H

#include <QObject>
#include <QString>
#include <QDBusObjectPath>

class QDBusInterface;
class QDBusPendingCallWatcher;

class SystemdManager : public QObject
{
    Q_OBJECT

public:
    explicit SystemdManager(QObject *parent = nullptr);
    ~SystemdManager();

    enum JobType {
        StartJob,
        StopJob,
    };
    Q_ENUM(JobType);

    // A job here means a systemd's job, which can be starting
    // or stopping a systemd unit (usually a service), for instance
    struct Job {
        QString unit;
        JobType type;
        bool replace;

        Job(QString unit, JobType type, bool replace) : unit(unit), type(type), replace(replace) {}

        static Job start(QString unit, bool replace = true) { return Job(unit, StartJob, replace); }
        static Job stop(QString unit, bool replace = true) { return Job(unit, StopJob, replace); }
    };

    typedef QList<Job> JobList;

    bool busy();
    void addUnitJob(Job job);
    void addUnitJobs(JobList &jobs);

signals:
    void busyChanged();
    void unitJobFinished(Job &job);
    void unitJobFailed(Job &job, JobList &remaining);
    void creatingJobFailed(JobList &remaining);

private slots:
    void pendingCallFinished(QDBusPendingCallWatcher *call);
    void onJobRemoved(uint id, QDBusObjectPath job, QString unit, QString result);

private:
    void processNextJob();

    QDBusPendingCallWatcher *m_pendingCall;
    JobList m_jobs;
    QString m_currentJob;
    QDBusInterface *m_systemd;
};

#endif // SYSTEMDMANAGER_H
