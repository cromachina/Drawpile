// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef THINSRV_INITSYS_H
#define THINSRV_INITSYS_H
#include <QList>
#include <QString>

/**
 * Integration with the init system.
 *
 * Currently two backends are provided:
 *
 * - dummy (no integration)
 * - systemd
 */
namespace initsys {

/**
 * @brief What this init system is called
 */
QString name();

/**
 * @brief Name of the socket activation file, only used to tell the user to
 * uninstall it.
 */
QString socketActivationName();

/**
 * @brief Send the "server ready" notification
 */
void notifyReady();

/**
 * @brief Send a freeform status update
 * @param status
 */
void notifyStatus(const QString &status);

/**
 * @brief Get file descriptors passed by the init system
 *
 * No longer supported, only here so we can detect it and tell the user to turn
 * it off.
 *
 * @return list of socket file descriptors
 */
QList<int> getListenFds();

/**
 * If a watchdog is enabled, this returns a positive millisecond value in which
 * to call the watchdog function to let the init system know that the process is
 * doing fine. If no watchdog is enabled, it returns a value <= 0. Can only be
 * called once, since it may unset environment variables.
 */
int getWatchdogMsec();

/**
 * Signal the init system watchdog that we're doing fine. Should be called in an
 * interval that getWatchDogMsec gave you.
 */
void watchdog();

}

#endif // INITSYS_H
