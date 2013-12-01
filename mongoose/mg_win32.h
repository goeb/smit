/** Exportation of mongoose-redefined symbols
  * For WIN32 only.
  *
  */

#ifndef _mg_win32_h
#define _mg_win32_h

#if defined(_WIN32)

  #include <windows.h>
  #include <time.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

  typedef HANDLE pthread_mutex_t;
  int pthread_mutex_init(pthread_mutex_t *mutex, void *unused);
  int pthread_mutex_destroy(pthread_mutex_t *mutex);
  int pthread_mutex_lock(pthread_mutex_t *mutex);
  int pthread_mutex_unlock(pthread_mutex_t *mutex);

  int mg_mkdir(const char *path, int mode);
  const char *mg_strcasestr(const char *big_str, const char *small_str);
  #define sleep(x) Sleep((x) * 1000)

#ifdef __cplusplus
}
#endif // __cplusplus


  inline struct tm *localtime_r(const time_t *timep, struct tm *result) {
      result = localtime(timep);
      return result;
  }


  // end #if _WIN32
#else
  // linux

  #define mg_mkdir(x, y) mkdir(x, y)



#endif

#endif // _mg_win32_h

