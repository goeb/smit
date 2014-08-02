
#include <string.h>
#include <stdio.h>

#include "utest.h"
#include "parseConfig.h"
#include "stringTools.h"

// Expected result:
// line:  [single] 
// line:  [columns]  [owner]  [select]  [user] 
// line:  [columns]  [status]  [select]  [open]  [closed] 
// line:  [columns]  [target_version]  [select]  [non #renseigne]  [v0.1]  [v0.2]  [v1.1] 
// line:  [columns]  [tags]  [select_multiple]  [v4.0]  [v4.1]  [v5.0] 
// line:  [embedded-double-quote]  [yyy]  [a b c]  [d e "f]  [g h i]  [jkl-mnop]  [u"v"w]  [abcdefgh] 
// line:  [multi-line-double-quote]  [1
// 2
// 3] 
//

int main()
{
    // step 1: parse "parseConfig1.txt"
    FILE* f = fopen("parseConfig1.txt", "r");
    ASSERT(f!=0);
    if (!f) {
        utestEnd();
        return 1;
    }
    char buffer[1000];
    size_t n = fread(buffer, 1, 1000, f);
    fclose(f);

    std::list<std::list<std::string> > tokens = parseConfigTokens(buffer, n);

    std::list<std::list<std::string> >::iterator i;
    for (i=tokens.begin(); i!=tokens.end(); i++) {
        std::list<std::string> line = *i;
        std::list<std::string>::iterator tok;
        printf("line (%lu): ", L(line.size()));
        for (tok=line.begin(); tok!= line.end(); tok++) {
            printf(" [%s] ", tok->c_str());
        }
        printf("\n");
    }

    // check result
    ASSERT(tokens.size() == 8);
    std::list<std::list<std::string> >::iterator line;
    line = tokens.begin();
    ASSERT(line->size() == 1);

    line++;
    ASSERT(line->size() == 4);

    line++;
    ASSERT(line->size() == 5);

    line++;
    ASSERT(line->size() == 7);

    line++;
    ASSERT(line->size() == 6);

    line++;
    ASSERT(line->size() == 8);
    std::list<std::string>::iterator tok = line->begin();
    tok++; tok++; tok++;
    ASSERT(0 == tok->compare("d e \"f"));

    line++;
    ASSERT(line->size() == 2);

    line++;
    ASSERT(line->size() == 2);
    ASSERT(line->front() == "one-void-value");
    ASSERT(line->back() == "");

    // step 2: parse "parseConfig2.txt"
    f = fopen("parseConfig2.txt", "r");
    ASSERT(f!=0);
    n = fread(buffer, 1, 1000, f);

    tokens = parseConfigTokens(buffer, n);

    for (i=tokens.begin(); i!=tokens.end(); i++) {
        std::list<std::string> line = *i;
        std::list<std::string>::iterator tok;
        printf("line: ");
        for (tok=line.begin(); tok!= line.end(); tok++) {
            printf(" [%s] ", tok->c_str());
        }
        printf("\n");
    }

    // check result
    ASSERT(tokens.size() == 4);
    line = tokens.begin();
    ASSERT(line->size() == 4);
    tok = line->begin();
    tok++; tok++; tok++;
    ASSERT(0 == tok->compare("<"));

    line++;
    ASSERT(line->size() == 3);
    tok = line->begin();
    tok++; tok++;
    ASSERT(0 == tok->compare("<"));

    line++;
    ASSERT(line->size() == 2);
    tok = line->begin();
    ASSERT(0 == tok->compare("message"));
    tok++;
    ASSERT(0 == tok->compare("#message-yy"));

    line++;
    ASSERT(line->size() == 2);

    std::string s = doubleQuote("a b\nc d\\");
    ASSERT(s == "\"a b\\nc d\\\\\"");

    s = doubleQuote("a \"b\"");
    ASSERT(s == "\"a \\\"b\\\"\"");

    std::list<std::string> values;
    values.push_back("toto");
    values.push_back("a\nb");
    values.push_back("a b c");
    values.push_back("a..\"..b");

    s = serializeProperty("xyz", values);
    ASSERT(s == "xyz toto \"a\\nb\" \"a b c\" \"a..\\\"..b\"\n");

    utestEnd();
}
