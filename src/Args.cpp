#include <string.h>

#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>
#include "Args.h"
#include "global.h"


Args::Args(int argc, char **argv)
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

    if (longname && shortname) optionSpecs.push_back(as);
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


void Args::parse(int argc, char **argv)
{
    int i = 0;
    while (i < argc) {
        std::string arg = argv[i];
        if ( (arg.size() > 0) && (arg[0] == '-')) {
            // look if it is a known option
            if (arg.size() == 2) {
                // look if theer is a related option specification
                const ArgOptionSpec *aos = getOptSpec(arg[1]);
                if (aos) {
                    int n = aos->argnum;
                    if (argc < i + n + 1) {
                        fprintf(stderr, "Missing argument(s) for option '%s'. See '--help'.\n", arg.c_str());
                        exit(1);
                    }

                    // grab the option
                    // TODO

                } else nonOptionvalues.push_back(arg);

            } else if (arg.size() > 2) {
                if (arg.substr(0, 2) == "--") {
                    const ArgOptionSpec *aos = getOptSpec(arg.substr(2).c_str());
                    if (aos) {

                    } else nonOptionvalues.push_back(arg);

                } else nonOptionvalues.push_back(arg);

            } else nonOptionvalues.push_back(arg);
        }

        i++;
    }
    if (nonOptionLimit > 0 && nonOptionvalues.size() > nonOptionLimit) {
        fprintf(stderr, "Too many arguments. See '--help'.\n");
        exit(1);
    }
}

std::string Args::getHelp()
{
    return "todo";
}



