/*
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#include "systemdmanager.h"
#include "logging.h"
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingReply>

namespace Systemd {
const auto Service = QStringLiteral("org.freedesktop.systemd1");
const auto ManagerPath = QStringLiteral("/org/freedesktop/systemd1");
const auto ManagerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
const auto Replace = QStringLiteral("replace");
const auto Fail = QStringLiteral("fail");
const auto StartUnit = QStringLiteral("StartUnit");
const auto StopUnit = QStringLiteral("StopUnit");
const auto ResultDone = QStringLiteral("done");
const auto ResultSkipped = QStringLiteral("skipped");
}

// This is currently implemented so that it can do one thing at a time
// as there is no need for anything more complicated.
// It would be quite easily possible to add individual queues with
// queue numbers if needed.

SystemdManager::SystemdManager(QObject *parent) :
    QObject(parent),
    m_pendingCall(nullptr),
    m_systemd(new QDBusInterface(Systemd::Service, Systemd::ManagerPath,
                                 Systemd::ManagerInterface,
                                 QDBusConnection::systemBus(), this))
{
    if (!m_systemd->isValid())
        qCCritical(lcSUM) << "Could not create interface to systemd, can not function!";
    if (!connect(m_systemd, SIGNAL(JobRemoved(uint, QDBusObjectPath, QString, QString)),
                 this, SLOT(onJobRemoved(uint, QDBusObjectPath, QString, QString))))
        qCCritical(lcSUM) << "Could not connect to JobRemoved signal, can not function!";
}

SystemdManager::~SystemdManager()
{
    delete m_systemd;
    m_systemd = nullptr;
}

bool SystemdManager::busy()
{
    // Busy if there is something on queue or a pending call or job removal is waited for
    return !m_jobs.isEmpty() || m_pendingCall || !m_currentJob.isEmpty();
}

void SystemdManager::addUnitJob(Job job)
{
    addUnitJobs(JobList() << job);
}

void SystemdManager::addUnitJobs(JobList &jobs)
{
    Q_ASSERT_X(!jobs.isEmpty(), "addUnitJobs", "jobs must never be empty");
    bool wasEmpty = m_jobs.isEmpty();
    m_jobs.append(jobs);
    processNextJob();
    if (wasEmpty)
        emit busyChanged();
}

void SystemdManager::processNextJob()
{
    if (m_pendingCall || !m_currentJob.isEmpty())
        return;

    qCDebug(lcSUM) << "Process next systemd job";

    QDBusPendingCall call = m_systemd->asyncCall(
            (m_jobs.first().type == StopJob) ? Systemd::StopUnit : Systemd::StartUnit,
            m_jobs.first().unit, (m_jobs.first().replace) ? Systemd::Replace : Systemd::Fail);
    QDBusPendingCallWatcher *m_pendingCall = new QDBusPendingCallWatcher(call, this);
    connect(m_pendingCall, &QDBusPendingCallWatcher::finished, this, &SystemdManager::pendingCallFinished);
}

void SystemdManager::pendingCallFinished(QDBusPendingCallWatcher *call)
{
    m_pendingCall = nullptr;
    QDBusPendingReply<QDBusObjectPath> reply = *call;
    if (reply.isError()) {
        // This basically means that the job didn't do anything yet
        qCWarning(lcSUM) << "Systemd job start failed" << reply.error();
        JobList remaining;
        remaining.swap(m_jobs);
        emit creatingJobFailed(remaining);
        if (!busy())
            emit busyChanged();
    } else {
        m_currentJob = reply.value().path();
        qCDebug(lcSUM) << "Current systemd job is now" << m_currentJob;
    }
    call->deleteLater();
}

void SystemdManager::onJobRemoved(uint id, QDBusObjectPath job, QString unit, QString result)
{
    Q_UNUSED(id)
    if (job.path() == m_currentJob) {
        if (result != Systemd::ResultDone) {
            // Uh, Houston, we've had a problem
            qCWarning(lcSUM) << "Systemd" << (m_jobs.first().type == StopJob) ? "stop" : "start" << "job"
                             << job.path() << "for unit" << unit << "ended with result" << result;
            m_currentJob.clear(); // Clear busyness before signal
            JobList remaining;
            remaining.swap(m_jobs);
            if (result == Systemd::ResultSkipped) {
                // This means that the job didn't do anything yet
                emit creatingJobFailed(remaining);
            } else {
                Job failed = remaining.takeFirst();
                emit unitJobFailed(failed, remaining);
            }
        } else {
            qCDebug(lcSUM) << "Systemd" << (m_jobs.first().type == StopJob) ? "stop" : "start" << "job"
                           << job.path() << "for unit" << unit << "ended with result" << result;
            Job done = m_jobs.takeFirst();
            emit unitJobFinished(done);
            m_currentJob.clear(); // Clear busyness *after* signal
            if (!m_jobs.isEmpty())
                processNextJob();
        }
        if (!busy()) // Check if we are busy still
            emit busyChanged();
    }
}
