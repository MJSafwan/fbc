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


char *strip_string(char *buff) { 
    int len = strlen(buff);
    char *res = malloc(len+1);
    int state = 0;

    int offset = 0;
    size_t size = 0;

    for (size_t i = 0; i < len; i++) {
        if (isspace(buff[i]) == 0) {
            offset = i;
            break;
        }
    }

    for (size_t i = 0; i < len; i++) {
        if (isspace(buff[len-i-1]) == 0) {
            size = len-i-offset;
            break;
        }
    }

    for (size_t i = 0; i < size; ++i) {
        res[i] = buff[offset+i];
    }
    res[size] = 0;
    return res;
}

int main(void) {
    fbc_init();
    eval_file("std.fbc");
    char *buff = NULL;
    int cap = 1028;
    for (;;) {
        buff = realloc(buff, cap);
        memset(buff, 0, cap);
        fgets(buff, cap-1, stdin);
        if (buff == NULL) /* C-d EOF */
            exit(0);
        
        char *sbuff = strip_string(buff);
        eval_type val = fbc_line(sbuff);
        free(sbuff);
        if (fbc_did_error() > 0) {
            printf("%s", fbc_get_error());
            continue;
        }

        if (val.type == T_NUM)
            printf("%.*f\n", val.perc, val.num);
    }
    return 0;
}
