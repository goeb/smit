/** Exportation of mongoose-redefined symbols
  * For WIN32 only.
  *
  */

#ifndef _mg_win32_h
#define _mg_win32_h

#if defined(_WIN32)

  #include <windows.h>
  #include <time.h>


  inline long gettid()
  {
    long tid = GetCurrentThreadId();
    return tid;
  }

  #define sleep(x) Sleep((x) * 1000)
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

