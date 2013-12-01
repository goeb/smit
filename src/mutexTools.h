#ifndef _mutexTools_h
#define _mutexTools_h

#if defined(_WIN32)
  #include "mg_win32.h"
#else
  #include <pthread.h>
#endif


class Locker {
public:
    Locker();
    ~Locker();
    void lockForWriting();
    void unlockForWriting();
    void lockForReading();
    void unlockForReading();

private:
    pthread_mutex_t readOnlyMutex;
    pthread_mutex_t readWriteMutex;
    int nReaders; // number of concurrent readers
};


enum LockMode { LOCK_READ_ONLY, LOCK_READ_WRITE };

class ScopeLocker {
public:
    inline ScopeLocker(Locker & L, enum LockMode m) : locker(L), mode(m) {
        if (mode == LOCK_READ_ONLY) locker.lockForReading();
        else locker.lockForWriting();
    }
    inline ~ScopeLocker() {
        if (mode == LOCK_READ_ONLY) locker.unlockForReading();
        else locker.unlockForWriting();
    }

private:
    Locker & locker;
    enum LockMode mode;
};




#endif
