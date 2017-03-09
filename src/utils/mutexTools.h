#ifndef _mutexTools_h
#define _mutexTools_h

#include <pthread.h>

class Locker {
public:
    Locker();
    ~Locker();
    void lockForWriting();
    void lockForReading();
    void unlock();

private:
    pthread_rwlock_t rwMutex;
};

// helpers
#define LOCK_SCOPE(_a, _b) ScopeLocker __scopeLockerObject(_a, _b);
#define LOCK_SCOPE_I(_a, _b, _i) ScopeLocker __scopeLockerObject##_i(_a, _b);

enum LockMode { LOCK_READ_ONLY, LOCK_READ_WRITE };

class ScopeLocker {
public:
    inline ScopeLocker(Locker &L, enum LockMode m) : locker(L), mode(m) {
        if (mode == LOCK_READ_ONLY) locker.lockForReading();
        else locker.lockForWriting();
    }
    inline ~ScopeLocker() {
        locker.unlock();
    }

private:
    Locker &locker;
    enum LockMode mode;
};




#endif
