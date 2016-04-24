/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

/** The read/write mutex pattern has been influenced by
  * Windows weak mutex architecture: a thread cannot release
  * a mutex locked by another thread. Hence the sleep during a few milliseconds.
  * Maybe should be improved. I should read the MSDN doc.
  *
  * TODO, use:
  * - pthread_rwlock on Linux
  * - Slim Reader/Writer (SRW) Locks on Windows
  *
  */
#include "mutexTools.h"
#include "logging.h"

Locker::Locker()
{
    LOG_FUNC();
    nReaders = 0;
    pthread_mutex_init(&readOnlyMutex, 0);
    pthread_mutex_init(&readWriteMutex, 0);

}

Locker::~Locker()
{
    LOG_FUNC();

    pthread_mutex_destroy(&readOnlyMutex);
    pthread_mutex_destroy(&readWriteMutex);
}

void Locker::lockForWriting()
{
    LOG_FUNC();

    int r = pthread_mutex_lock(&readWriteMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));

    // now wait until there is no reader
    // TODO add a timeout
    int n = 1;
    while (n > 0) {
        r = pthread_mutex_lock(&readOnlyMutex);
        if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));

        n = nReaders;
        LOG_DEBUG("lockForWriting: nReaders=%d", nReaders);

        r = pthread_mutex_unlock(&readOnlyMutex);
        if (r != 0) LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));

        if (n == 0) break; // skip the following msleep...

        msleep(10); // prevent CPU consuming loop
    }
}
void Locker::unlockForWriting()
{
    LOG_FUNC();

    int r = pthread_mutex_unlock(&readWriteMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));

}

void Locker::lockForReading()
{
    LOG_FUNC();
    // lock && unlock the write mutex.
    // this is to prevent new reader while a writer has taken the mutex
    int r = pthread_mutex_lock(&readWriteMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));
    r = pthread_mutex_unlock(&readWriteMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));

    // now get the read mutex, for updating nReaders
    r = pthread_mutex_lock(&readOnlyMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));

    nReaders++;

    r = pthread_mutex_unlock(&readOnlyMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));
}

void Locker::unlockForReading()
{
    LOG_FUNC();

    int r = pthread_mutex_lock(&readOnlyMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));

    if (nReaders <= 0) {
        // error
        LOG_ERROR("unlockForReading error: nReaders == %d", nReaders);
    } else {
        nReaders--;
    }

    r = pthread_mutex_unlock(&readOnlyMutex);
    if (r != 0) LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));
}

