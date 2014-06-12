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
        printf("Cannot open '%s': %s\n", filename.c_str(), strerror(errno));
        return -1;
    }

    std::string header = filename + ".h";
    std::string body = filename + ".c";
    std::string cSymbolStem = filename;
    size_t n = cSymbolStem.size();
    size_t i;
    for (i=0; i<n; i++) if (!isalnum(cSymbolStem[i])) cSymbolStem[i] = '_';

    // "em" for "embbedded"
    std::string binaryDeclaration = "const char* em_binary_data_" + cSymbolStem;
    std::string sizeDeclaration = "const size_t em_binary_size_" + cSymbolStem;
    
    // generate c file
    FILE* fbody = fopen(body.c_str(), "wb");
    if (!fbody) {
        printf("Cannot open '%s' for writing: %s\n", body.c_str(), strerror(errno));
        return -1;
    }
    fprintf(fbody, "#include \"%s\"\n", header.c_str());
    fprintf(fbody, "%s = \n", binaryDeclaration.c_str());
    fprintf(fbody, "    \"");
    char c;
    while (f.get(c)) {
        fprintf(fbody, "\\x%02x", (unsigned int)c);
    }
    fprintf(fbody, "\"\n;\n");
    fprintf(fbody, "%s;\n", sizeDeclaration.c_str());
    fclose(fbody);

    // generate header file
    FILE* fheader = fopen(header.c_str(), "wb");
    if (!fheader) {
        printf("Cannot open '%s' for writing: %s\n", header.c_str(), strerror(errno));
        return -1;
    }
    fprintf(fheader, "#ifndef _%s_h\n", cSymbolStem.c_str());
    fprintf(fheader, "#define _%s_h\n", cSymbolStem.c_str());
    fprintf(fheader, "#include <stdio.h>\n");
    fprintf(fheader, "extern %s;\n", binaryDeclaration.c_str());
    fprintf(fheader, "extern %s;\n", sizeDeclaration.c_str());
    fprintf(fheader, "#endif\n");
    fclose(fheader);
    return 0;
}
