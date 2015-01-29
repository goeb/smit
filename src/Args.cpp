#include <string.h>

#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>
#include "Args.h"
#include "global.h"



Args::Args()
{
    nonOptionLimit = -1; // by default, no limit
}

/** Specify an allowed argument pattern
  *
  */
void Args::setOpt(const char *longname, char shortname, const char *help, int argnum)
{
    ArgOptionSpec as;
    as.longname = longname;
    as.shortname = shortname;
    as.help = help;
    as.argnum = argnum;

    if (longname || shortname) optionSpecs.push_back(as);
}

void Args::setNonOptionLimit(int n)
{
    nonOptionLimit = n;
}

/** get specification of a short named option
  */
const ArgOptionSpec *Args::getOptSpec(const char c)
{
    std::list<ArgOptionSpec>::const_iterator os;
    FOREACH(os, optionSpecs) {
        if (os->shortname == c) return &(*os);
    }
    return 0;
}

/** get specification of a long named option
  */
const ArgOptionSpec *Args::getOptSpec(const char *s)
{
    std::list<ArgOptionSpec>::const_iterator os;
    FOREACH(os, optionSpecs) {
        if (0 == strcmp(os->longname, s)) return &(*os);
    }
    return 0;
}

std::string getKey(const ArgOptionSpec *aos)
{
    std::string key = "-";
    if (aos->shortname) key += aos->shortname;
    key += "-";
    if (aos->longname) key += aos->longname;
    return key;
}


/** Grab the options starting at position 'pos'
  *
  * @return
  *     number of arguments consumed
  */
int Args::grabOption(int argc, char **argv, const ArgOptionSpec *aos, int pos, const char *optName)
{
    if (!aos) {
        fprintf(stderr, "Invalid option '%s'. See '--help'.\n", optName);
        exit(1);
    }

    int n = aos->argnum;
    if (argc < pos + n) {
        fprintf(stderr, "Missing arguments for option '%s'. See '--help'.\n", optName);
        exit(1);
    }

    int i = 0; // number of arguments consumed
    while (i < n) {
        // generate a key, used for
        std::string key = getKey(aos);
        optionValues[key] = argv[pos+i];
        i++;
    }
    return i;
}

void Args::parse(int argc, char **argv)
{
    int i = 0;
    bool stopOpt = false; // activated when "--" is encountered
    while (i < argc) {
        std::string arg = argv[i];

        if (!stopOpt && (arg == "--") ) stopOpt = true;

        else if (!stopOpt && (arg.size() >= 2) && (arg[0] == '-') ) {
            // option
            if (arg[1] == '-') {
                // long named option
                const ArgOptionSpec *aos = getOptSpec(arg.substr(2).c_str());
                int skip = grabOption(argc, argv, aos, i+1, arg.c_str());

                i+= skip;
                continue;

            } else {
                // short named option (1 or several)
                int n = arg.size();
                int o = 1;
                while (o < n) {
                    const ArgOptionSpec *aos = getOptSpec(arg.substr(o, 1).c_str());
                    int skip = grabOption(argc, argv, aos, i+1, arg.c_str());
                    i+= skip;
                    o++;
                }
            }
        } else nonOptionvalues.push_back(arg);

            i++;
    }

    if ( (nonOptionLimit > 0) && (nonOptionvalues.size() > nonOptionLimit) ) {
        fprintf(stderr, "Too many arguments. See '--help'.\n");
        exit(1);
    }
}
#define INDENT "  "
void Args::printHelp() const
{
    std::list<ArgOptionSpec>::const_iterator os;
    FOREACH(os, optionSpecs) {
        if (!os->shortname) printf(INDENT "--%s", os->longname);
        else if (!os->longname) printf(INDENT "-%c", os->shortname);
        else printf(INDENT "-%c, --%s", os->shortname, os->longname);

        if (os->argnum >= 1) printf(" ...");
        printf("\n");
        if (os->help) printf(INDENT INDENT INDENT "%s\n\n", os->help);
    }
}

/** Look for a specific option in the arguments
  */
const char *Args::get(const char *optName)
{
    const ArgOptionSpec *aos = getOptSpec(optName);
    if (aos) return 0;
    std::string key = getKey(aos);
    std::map<std::string, std::string>::const_iterator i = optionValues.find(key);
    if (i == optionValues.end()) return 0;
    return i->second.c_str();
}


