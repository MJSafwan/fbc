#include <stdio.h>
#define FBC_IMPLEMENTATION
#include "fbc.h"

void eval_file(char *fn, fbc_ctx *ctx) {
    FILE *f = fopen(fn, "r");
    xassert(f, "Cannot open file!\n%s: %s\n", fn, strerror(errno));
    size_t bcap = 1028;
    char *buff = malloc(bcap);
    xassert(buff, "Cannot allocate buffer for reading file\n");
    memset(buff, 0, bcap);
    while (fgets(buff, bcap, f)) {
        fbc_line(buff, ctx);
        memset(buff, 0, bcap);
    }
    fclose(f);
}

int main(void) {
    fbc_ctx ctx = fbc_init();
    eval_file("std.fbc", &ctx);
    int cap = 1028;
    char *buff = malloc(cap);
    for (;;) {
        memset(buff, 0, cap);
        fgets(buff, cap, stdin);
        if (buff == NULL) /* C-d EOF */
            exit(0);
        
        eval_type val = fbc_line(buff, &ctx);
        if (fbc_did_error(&ctx) > 0) {
            printf("%s", fbc_get_error(&ctx));
            continue;
        }

        if (val.type == T_NUM)
            printf("%.*f\n", val.perc, val.num);
    }
    // fbc_uninit(&ctx);
    return 0;
}
