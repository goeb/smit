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

#include <stdlib.h>

#include "mutexTools.h"
#include "logging.h"

Locker::Locker()
{
    LOG_FUNC();
    int ret = pthread_rwlock_init(&rwMutex, NULL);
    if (ret != 0) {
        LOG_ERROR("pthread_rwlock_init error: (%d) %s", ret, strerror(ret));
        exit(1);
    }
}

Locker::~Locker()
{
    LOG_FUNC();
    int ret = pthread_rwlock_destroy(&rwMutex);
    if (ret != 0) {
        LOG_ERROR("pthread_rwlock_destroy error: (%d) %s", ret, strerror(ret));
        exit(1);
    }
}

void Locker::lockForWriting()
{
    LOG_FUNC();
    int ret = pthread_rwlock_wrlock(&rwMutex);
    if (ret != 0) {
        LOG_ERROR("pthread_rwlock_wrlock error: (%d) %s", ret, strerror(ret));
        exit(1);
    }
}

void Locker::unlock()
{
    LOG_FUNC();
    int ret = pthread_rwlock_unlock(&rwMutex);
    if (ret != 0) {
        LOG_ERROR("pthread_rwlock_unlock error: (%d) %s", ret, strerror(ret));
        exit(1);
    }
}

void Locker::lockForReading()
{
    LOG_FUNC();
    int ret = pthread_rwlock_rdlock(&rwMutex);
    if (ret != 0) {
        LOG_ERROR("pthread_rwlock_rdlock error: (%d) %s", ret, strerror(ret));
        exit(1);
    }
}
