
#include <string.h>
#include <stdio.h>

#include "utest.h"
#include "parseConfig.h"

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

main()
{
    // step 1: parse "parseConfig1.txt"
    FILE* f = fopen("parseConfig1.txt", "r");
    ASSERT(f!=0);
    uint8_t buffer[1000];
    size_t n = fread(buffer, 1, 1000, f);

    std::list<std::list<ustring> > tokens = parseConfig(buffer, n);

    std::list<std::list<ustring> >::iterator i;
    for (i=tokens.begin(); i!=tokens.end(); i++) {
        std::list<ustring> line = *i;
        std::list<ustring>::iterator tok;
        printf("line: ");
        for (tok=line.begin(); tok!= line.end(); tok++) {
            printf(" [%s] ", tok->c_str());
        }
        printf("\n");
    }

    // check result
    ASSERT(tokens.size() == 7);
    std::list<std::list<ustring> >::iterator line;
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
    std::list<ustring>::iterator tok = line->begin();
    tok++; tok++; tok++;
    ASSERT(0 == tok->compare((uint8_t*)"d e \"f"));

    line++;
    ASSERT(line->size() == 2);
    fclose(f);

    // step 2: parse "parseConfig2.txt"
    f = fopen("parseConfig2.txt", "r");
    ASSERT(f!=0);
    n = fread(buffer, 1, 1000, f);

    tokens = parseConfig(buffer, n);

    for (i=tokens.begin(); i!=tokens.end(); i++) {
        std::list<ustring> line = *i;
        std::list<ustring>::iterator tok;
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
    ASSERT(0 == tok->compare((uint8_t*)"<"));

    line++;
    ASSERT(line->size() == 3);
    tok = line->begin();
    tok++; tok++;
    ASSERT(0 == tok->compare((uint8_t*)"<"));

    line++;
    ASSERT(line->size() == 2);
    tok = line->begin();
    ASSERT(0 == tok->compare((uint8_t*)"message"));
    tok++;
    ASSERT(0 == tok->compare((uint8_t*)"#message-yy"));

    line++;
    ASSERT(line->size() == 2);

    utestEnd();
}
