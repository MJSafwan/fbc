#include <stdio.h>
#define FBC_IMPLEMENTATION
#include "fbc.h"

void eval_file(char *fn) {
    FILE *f = fopen(fn, "r");
    xassert(f, "Cannot open file!\n%s: %s\n", fn, strerror(errno));
    size_t bcap = 1028;
    char *buff = malloc(bcap);
    xassert(buff, "Cannot allocate buffer for reading file\n");
    memset(buff, 0, bcap);
    while (fgets(buff, bcap, f)) {
        fbc_line(buff);
        memset(buff, 0, bcap);
    }
    fclose(f);
}

int main(void) {
    fbc_init();
    eval_file("std.fbc");
    char *buff = NULL;
    for (;;) {
        buff = realloc(buff, 1028);
        memset(buff, 0, 1028);
        fgets(buff, 1027, stdin);
        buff[strlen(buff)-1] = 0;
        if (buff == NULL) /* C-d EOF */
            exit(0);
        
        eval_type val = fbc_line(buff);
        if (fbc_did_error() > 0) {
            printf("%s", fbc_get_error());
            continue;
        }

        if (val.type == T_NUM)
            printf("%.*f\n", val.perc, val.num);
    }
    return 0;
}
