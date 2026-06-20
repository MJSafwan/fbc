#include <stdio.h>
#define FBC_IMPLEMENTATION
#include "../fbc.h"

int main(void) {
   fbc_ctx ctx = fbc_init();

   eval_type t = fbc_line("1.5 * 3.141 - 8 / 5 * (10 - 80.2 / 0.222)", &ctx);
   if (t.type == T_NUM)
       printf("%.6f\n", t.num);
   fbc_uninit(&ctx); 
}
