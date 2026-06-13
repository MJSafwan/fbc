#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <editline.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"

#define BUFF_CAP 256
#define ARENA_CAP (1024*100)

#define TOK_INV 0
#define TOK_NUM 1
#define TOK_OP 2
#define TOK_LP 3
#define TOK_RP 4
#define TOK_END 5
#define TOK_ID 6
#define TOK_COM 7
#define TOK_EQ 8

#define SEEK_CHAR 0
#define READ_NUM 1
#define READ_ID 2

typedef struct {
    int kind;
    union {
        double num;
        char *name;
        char op_id;
    };
} token;

typedef struct {
    size_t cursor;
    int state;
    const char *buff;
    size_t len;
} tokenizer;

arena p_arena = {0};

typedef struct {
    int l;
    int r;
} pair;

pair op_bind[128] = {
    ['+'] = (pair){1, 2},
    ['-'] = (pair){1, 2},
    ['*'] = (pair){3, 4},
    ['/'] = (pair){3, 4},
    ['%'] = (pair){3, 4},
    ['^'] = (pair){3, 5},
    ['|'] = (pair){1, 2},
    ['&'] = (pair){3, 4},
};

pair lop_bind[128] = {
  ['-'] = (pair){0, 1},  
  ['~'] = (pair){0, 1},
};

int is_op(char c) {
    return op_bind[c].r > 0 || lop_bind[c].r > 0;
}

int is_num(char c) {
    return c >= '0' && c <= '9';
}

int next_token(tokenizer *tk, token *out) {
    tk->state = SEEK_CHAR;
    
    char *s = NULL;
    int offset = 0;
    int floating = 0;
    for (;;) {
        if (tk->cursor >= tk->len) {
            out->kind = TOK_END;
            return TOK_END; 
        }

        if (tk->state == SEEK_CHAR) {
            char c = tk->buff[tk->cursor];
            if (isspace(c) == 0) {
                if (is_num(c) == 1) {
                    s = arena_alloc(&p_arena, tk->len);
                    strncpy(s, tk->buff, tk->len);
                    tk->state = READ_NUM;
                    offset = tk->cursor;
                }


                if (is_op(c) == 1) {
                    out->kind = TOK_OP;
                    out->op_id = c;
                    tk->cursor++;
                    return TOK_OP;
                }

                if (c == '(') {
                    out->kind = TOK_LP;
                    tk->cursor++;
                    return TOK_LP;
                }

                if (c == ')') {
                    out->kind = TOK_RP;
                    tk->cursor++;
                    return TOK_RP;
                }

                if (c == ',') {
                    out->kind = TOK_COM;
                    tk->cursor++;
                    return TOK_COM;
                }

                if (c == '=') {
                    out->kind = TOK_EQ;
                    tk->cursor++;
                    return TOK_EQ;
                }

                if (isalpha(c) == 1) {
                    s = arena_alloc(&p_arena, tk->len);
                    strncpy(s, tk->buff, tk->len);
                    tk->state = READ_ID;
                    offset = tk->cursor;
                }
            }
        }

        if (tk->state == READ_NUM) {
            out->kind = TOK_NUM;
            char c = tk->buff[tk->cursor];


            if (!is_num(c)) {
                if (c == '.') {
                    floating = 1;
                    s[tk->cursor] = 0;
                    out->num = atoi(&s[offset]);
                    out->kind = TOK_NUM;
                    tk->cursor++;
                    continue;
                }
                if (floating >= 1) {
                    return TOK_NUM;
                }
                s[tk->cursor] = 0;
                out->num = atoi(&s[offset]);
                out->kind = TOK_NUM;
                return TOK_NUM;
            }

            if (floating >= 1) {
                out->num += (c - '0') / pow(10, floating);
                floating++;
                tk->cursor++;
                continue;
            }
        }

        if (tk->state == READ_ID) {
            out->kind = TOK_ID;
            char c = tk->buff[tk->cursor];
            if (!isalpha(c)) {
                s[tk->cursor] = 0;
                out->name = &s[offset];
                out->kind = TOK_ID;
                return TOK_ID;
            }
        }

        tk->cursor++;
    }
}

int expect(tokenizer tkzer, int kind) {
    token next = {0};
    next_token(&tkzer, &next);
    return kind == next.kind;
}

void skip(tokenizer *tkzer) {
    token next = {0};
    next_token(tkzer, &next);
}

token peek(tokenizer tkzer) {
    token next = {0};
    next_token(&tkzer, &next);
    return next;
}

double leval(char lop, double num) {
    switch (lop) {
        case '-':
            return -num;
        case '~':
            return ~((int)num);
        default:
            return 0;
    }
}

double eval(char op, double num1, double num2) {
    switch (op) {
        case '+':
            return num1 + num2;
        case '*':
            return num1 * num2;
        case '-':
            return num1 - num2;
        case '/':
            if (num2 != 0)
                return num1/num2;
            return NAN;
        case '&':
            return (int)num1 & (int)num2;
        case '|':
            return (int)num1 | (int)num2;
        case '^':
            return (int)num1 ^ (int)num2;
        case '%':
            if (num2 > 0)
                return fmod(num1, num2);
            return NAN;
        default:
            return 0;
    }
}

double parse_expr(tokenizer *tz, int min_b, double *ret);

typedef struct {
    char *name;
    double val;
} var_entry;

struct {
    var_entry *items;
    size_t count;
    size_t capacity;
} var_table = {0};

double eval_var(char *name, int *found) {
    for (int i = 0; i < var_table.count; ++i) {
        var_entry val = var_table.items[i];
        if (strcmp(name, val.name) == 0) {
            *found = i;
            return val.val;
        }
    }

    *found = -1;
    return 0;
}

double eval_as(char *name, double rval) {
    int found = 0;
    eval_var(name, &found);
    if (found >= 0) {
        var_table.items[found].val = rval;
        return rval;
    }

    if (var_table.count >= var_table.capacity) {
        var_table.capacity = var_table.capacity == 0 ? 64 : var_table.capacity * 2;
        var_table.items = realloc(var_table.items, var_table.capacity * sizeof(var_entry));
        assert(var_table.items);
    }

    var_table.items[var_table.count].val = rval;
    var_table.items[var_table.count].name = strdup(name);
    var_table.count++;
    return rval;
}

double eval_func(char *name, int argc, double *argv, int *err) {
    /* 'Meta' functions */

    if (strcmp(name, "debug") == 0) {
        printf("I am the debugging function!\n");
        printf("You gave me %d arguments.\n", argc);
        printf("[");

        for (int i = 0; i < argc; ++i) {
            printf("%f", argv[i]);
            if (i < argc-1)
                printf(", ");
        }

        printf("]\n");
        *err = 0;
        return argc;    
    } 

    if (strcmp(name, "clear") == 0) {
        system("clear");
        return 0;
    }

    if (strcmp(name, "exit") == 0) {
        int code = 0;
        if (argc >= 1)
            code = (int)argv[0];
        exit(code);
        // return code;
    }

    /* Math functions */
    if (strcmp(name, "sin") == 0) {
        if (argc != 1) {
            *err = -1;
            return 0;
        }
        return sinf(argv[0]);
    }

    if (strcmp(name, "cos") == 0) {
        if (argc != 1) {
            *err = -1;
            return 0;
        }
        return cosf(argv[0]);
    }

    if (strcmp(name, "pow") == 0) {
        if (argc != 2) {
            *err = -1;
            return 0;
        }

        return pow(argv[0], argv[1]);
    }

    if (strcmp(name, "exp") == 0) {
        if (argc == 2) {
            return exp(argv[0]*log(argv[1]));
        }

        if (argc != 1) {
            *err = -1;
            return 0;    
        }
        return exp(argv[0]);
    }

    if (strcmp(name, "ln") == 0) {
        if (argc != 1) {
            *err = -1;
            return 0;
        }
        if (argv[0] <= 0)
            return NAN;
        return log(argv[0]);
    }

    if (strcmp(name, "log") == 0) {
        if (argc != 2) {
            *err = -1;
            return 0;
        }
        if (argv[0] <= 0)
            return NAN;
        if (argv[1] <= 0)
            return NAN;
        return log(argv[0])/log(argv[1]);
    }
    *err = -1;
    return 0;
}

double parse_pexpr(tokenizer *tz, int min_b, double *ret) {
    if (expect(*tz, TOK_NUM)) {
        token num = {0};
        next_token(tz, &num);
        *ret = num.num;
        return 0;
    } else if(expect(*tz, TOK_LP)) {
        skip(tz);
        int err = parse_expr(tz, 0, ret);
        if (err < 0)
            return -1;
        if (!expect(*tz, TOK_RP))
            return -1;
        skip(tz);
        return 0;
    } else if (expect(*tz, TOK_OP)) {
            token op = {0};
            next_token(tz, &op);
            pair binding = lop_bind[op.op_id];
            if (binding.r == 0)
                return -1;
            int err = parse_pexpr(tz, min_b, ret);
            if (err < 0)
                return -1;
            *ret = leval(op.op_id, *ret);
            return 0;
    } else if (expect(*tz, TOK_ID)) {
        token id = {0};
        next_token(tz, &id);     

        if (!expect(*tz, TOK_LP)) {
            if (expect(*tz, TOK_EQ)) {
                skip(tz);
                double rval = 0;
                if (parse_expr(tz, 0, &rval) < 0)
                    return -1;
                *ret = eval_as(id.name, rval);
                return 0;
            }
            int found = 0;
            *ret = eval_var(id.name, &found);
            return 0;
        }

        skip(tz);
        int arg_cap = 256;
        double *argv = malloc(arg_cap * sizeof(int));
        int argc = 0;
        if (expect(*tz, TOK_RP)) {
            skip(tz);
            int err = 0;
            *ret = eval_func(id.name, argc, argv, &err);
            return err;
        }

        if (parse_expr(tz, 0, &argv[argc++]) < 0)
            return -1;

        if (expect(*tz, TOK_RP)) {
            skip(tz);
            int err = 0;
            *ret = eval_func(id.name, argc, argv, &err);
            return err;
        }

        for (;;) {
            if (argc >= arg_cap)
                return -1;
            if (!expect(*tz, TOK_COM))
                return -1;
            skip(tz);
            if (parse_expr(tz, 0, &argv[argc++]) < 0)
                return -1;
            if (expect(*tz, TOK_RP))
                break;
        }
        int err = 0;
        *ret = eval_func(id.name, argc, argv, &err);
        return err;
    }
    return -1;
}

double parse_expr(tokenizer *tz, int min_b, double *ret) {
    double num1;
    token pk = peek(*tz);
    if (pk.kind == TOK_END)
        return 0;
    if (parse_pexpr(tz, min_b, &num1) < 0)
        return -1;

    for (;;) {
        if (!expect(*tz, TOK_OP))
            break;
        token op = peek(*tz);
        pair binding = op_bind[op.op_id];
        if (binding.l < min_b)
            break;
        skip(tz);
        double num2;
        int err = parse_expr(tz, binding.r, &num2);
        if (err < 0)
            return -1;
        num1 = eval(op.op_id, num1, num2);
    }
    *ret = num1;
    return 0;
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);
    assert(p_arena.ptr);

    eval_as("PI", M_PI);
    eval_as("E", M_E);

    for (;;) {
        memset(p_arena.ptr, 0, p_arena.capacity);
        token *ts = arena_alloc(&p_arena, 64 * sizeof(token));

        char *buff = readline("> ");
        if (buff == NULL) /* C-d EOF */
            exit(0);

        size_t bytes_read = strlen(buff)+1;
        tokenizer tz = {0};
        tz.buff = buff;
        tz.len = bytes_read;

        token p = peek(tz);
        if (p.kind == TOK_END) {
            arena_pop(&p_arena);
            continue;
        }

        double res = 0.0f;
        int err = parse_expr(&tz, 0, &res);
        if (err < 0) {
            printf("Syntax error!\n");
            arena_pop(&p_arena);
            continue;
        }
        printf("%f\n", res);
        arena_pop(&p_arena);
    }
    return 0;
}
