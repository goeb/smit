#ifndef _Args_h
#define _Args_h

#include <map>
#include <list>
#include <string>

struct ArgOptionSpec {
    const char *longname;
    char shortname;
    const char *help;
    int argnum; // number of arguments
};

/*
 * int main(int argc, const *argv)
 * {
 *     Arg a;
 *     a.setArgSpec("user", "u", "Specify the user name", 1);
 *     a.setArgSpec("passwd", "p", "Specify the password", 1);      
 *     a.parse(argc, argv);
 *
 *     std::string user;
 *     if (a.get("user")) user = a.get("user");
 *
 * }
 */

class Args {
public:
    Args(int argc, char **argv);
    void setOpt(const char *longname, char shortname, const char *help, int argnum);
    void setNonOptionLimit(int n);
    void parse(int argc, char **argv);
    std::string getHelp();
private:
    std::list<ArgOptionSpec> optionSpecs; // specification of options
    std::map<std::string, std::string> optionValues;
    std::list<std::string> nonOptionvalues;
    int nonOptionLimit;
    const ArgOptionSpec *getOptSpec(const char c);
    const ArgOptionSpec *getOptSpec(const char *s);
};


#endif
