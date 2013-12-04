/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
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
    LOG_DEBUG("lockForWriting %p", &readWriteMutex);

    int r = pthread_mutex_lock(&readWriteMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));
    }

}
void Locker::unlockForWriting()
{
    LOG_FUNC();
    LOG_DEBUG("unlockForWriting %p", &readWriteMutex);

    int r = pthread_mutex_unlock(&readWriteMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));
    }

}

void Locker::lockForReading()
{
    LOG_FUNC();
    LOG_DEBUG("lockForReading %p", this);

    int r = pthread_mutex_lock(&readOnlyMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));
    }
    if (nReaders == 0) {
        // first reader
        lockForWriting();
    }
    nReaders++;
    r = pthread_mutex_unlock(&readOnlyMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));
    }

}

void Locker::unlockForReading()
{
    LOG_FUNC();
    LOG_DEBUG("unlockForReading %p", this);

    int r = pthread_mutex_lock(&readOnlyMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_lock error: (%d) %s", r, strerror(r));
    }

    if (nReaders <= 0) {
        // error
        LOG_ERROR("unlockForReading error: nReaders == %d", nReaders);
    } else {
        nReaders--;
    }

    if (nReaders == 0) {
        unlockForWriting();
    }
    r = pthread_mutex_unlock(&readOnlyMutex);
    if (r != 0) {
        LOG_ERROR("pthread_mutex_unlock error: (%d) %s", r, strerror(r));
    }

}

