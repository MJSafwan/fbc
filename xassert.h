#ifndef xassert
#define xassert(cond, msg, ...) \
    if (!(cond)) {\
        printf("%s:%s:%d Assersion failed: ", __FILE__, __func__, __LINE__);\
        printf(msg,##__VA_ARGS__);\
        __builtin_debugtrap();\
    }
#endif
