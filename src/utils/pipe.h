#ifndef _pipe_h
#define _pipe_h

#include <string>

/** Encapsulation of popen, fread, pclose.
 */
class Pipe {

public:
    Pipe(): fp(0) {}
    static Pipe *open(const std::string &command, const char *mode);
    static void close(Pipe *&p);
    std::string getline();

private:
    FILE *fp;
    std::string buffer; // data read but not yet consumed
};



#endif // _pipe_h
