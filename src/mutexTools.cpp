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
    nReaders = 0;
    pthread_mutex_init(&readOnlyMutex, 0);
    pthread_mutex_init(&readWriteMutex, 0);

}

Locker::~Locker()
{
    pthread_mutex_destroy(&readOnlyMutex);
    pthread_mutex_destroy(&readWriteMutex);
}

void Locker::lockForWriting()
{
    pthread_mutex_lock(&readWriteMutex);
}
void Locker::unlockForWriting()
{
    pthread_mutex_unlock(&readWriteMutex);
}

void Locker::lockForReading()
{
    pthread_mutex_lock(&readOnlyMutex);
    if (nReaders == 0) {
        // first reader
        lockForWriting();
    }
    nReaders++;
    pthread_mutex_unlock(&readOnlyMutex);
}

void Locker::unlockForReading()
{
    pthread_mutex_lock(&readOnlyMutex);
    if (nReaders <= 0) {
        // error
        LOG_ERROR("unlockForReading error: nReaders == %d", nReaders);
    } else if (nReaders) {
        nReaders--;
    }

    if (nReaders == 0) {
        unlockForWriting();
    }
    pthread_mutex_unlock(&readOnlyMutex);

}

