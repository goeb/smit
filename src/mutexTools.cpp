
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

