#include <stdio.h>
#define FBC_IMPLEMENTATION
#include "../fbc.h"

int main(void) {
   fbc_ctx ctx = fbc_init();

   eval_type fact = fbc_line("fact : if(cond=x)(then : x*fact(x=x-1))(else : 1)", &ctx);
   if (fbc_did_error(&ctx) != 0)
       return 1;
   char buff[128] = {0};
   for (int i = 0; i < 14; ++i) {
       memset(buff, 0, 128);
       snprintf(buff, 127, "fact(x=%d)", i);
       eval_type t = fbc_line(buff, &ctx);
       if (t.type == T_NUM)
           printf("%.0f\n", t.num);
   }
   fbc_uninit(&ctx); 
}
