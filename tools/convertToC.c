#include <fstream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

int usage()
{
    printf("Usage: convertToC <file>\n");
    return -1;
}
int main(int argc, char **argv)
{
    // load input file
    if (argc != 2) return usage();

    std::string filename = argv[1];
    std::ifstream f(filename.c_str());
    if (!f.good()) {
        printf("Cannot open '%s': %s\n", argv[1], strerror(errno));
        return -1;
    }

    // generate header file
    std::string header = filename + ".h";
    std::ofstream hfile(header.c_str());
    if (!hfile.good()) {
        printf("Cannot open '%s' for writing: %s\n", argv[1], strerror(errno));
        return -1;
    }
    std::string fcopy = filename;
    size_t n = fcopy.size();
    size_t i;
    for (i=0; i<n; i++) if (!isalnum(fcopy[i])) fcopy[i] = '_';
    
    hfile << "#ifndef _" << fcopy << "_h\n";
    hfile << "#define _" << fcopy << "_h\n";
    hfile << "extern const char* embedded_" << fcopy << "_data;\n";
    hfile << "extern size_t embedded_" << fcopy << "_size;\n";
    hfile << "#endif\n";



    return 0;

}
