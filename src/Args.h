#ifndef _Args_h
#define _Args_h

#include <map>
#include <vector>
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
    Args();
    inline void setUsage(const char *u) { usageString = u; }
    inline void setDescription(const char *d) { description = d; }
    void setOpt(const char *longname, char shortname, const char *help, int argnum);
    void setNonOptionLimit(int n);
    void parse(int argc, char **argv);
    void usage(bool withDescription = false) const;
    const char *get(const char *optName);
    const char *pop();

private:
    std::string usageString;
    std::string description;
    std::list<ArgOptionSpec> optionSpecs; // specification of options
    std::map<std::string, std::string> optionValues;
    int nonOptionLimit;
    const ArgOptionSpec *getOptSpec(const char c);
    const ArgOptionSpec *getOptSpec(const char *s);
    int grabOption(int argc, char **argv, const ArgOptionSpec *aos, int pos, const char *optName);
    std::vector<std::string> nonOptionvalues;
    size_t consumedNonOptionArgOffset;
};


#endif
