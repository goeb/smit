/** Exportation of mongoose-redefined symbols
  * For WIN32 only.
  *
  */

#ifndef _mg_win32_h
#define _mg_win32_h

#if defined(_WIN32)

  #include <windows.h>
  #include <time.h>

extern "C" {

  typedef HANDLE pthread_mutex_t;
  int pthread_mutex_init(pthread_mutex_t *mutex, void *unused);
  int pthread_mutex_destroy(pthread_mutex_t *mutex);
  int pthread_mutex_lock(pthread_mutex_t *mutex);
  int pthread_mutex_unlock(pthread_mutex_t *mutex);

  #define sleep(x) Sleep((x) * 1000)

}


  inline struct tm *localtime_r(const time_t *timep, struct tm *result) {
      struct tm *lt = localtime(timep); // TODO probably not thread-safe
      *result = *lt;
      return result;
  }

  inline long gettid()
  {
    long tid = GetCurrentThreadId();
    return tid;
  }

  #define msleep(x) Sleep(x)

  // end #if _WIN32
#else
  // linux

  #include <sys/syscall.h>
  #include <unistd.h>

  inline long gettid()
  {
    long tid = syscall(SYS_gettid);
    return tid;
  }
  #define msleep(x) usleep((x)*1000);


#endif

extern "C" {

const char *mg_strcasestr(const char *big_str, const char *small_str);
const char *mg_get_builtin_mime_type(const char *path);

}

#endif // _mg_win32_h

