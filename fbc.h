#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#ifndef FBC_H_
#define FBC_H_

#ifndef xassert
#define xassert(cond, msg, ...) \
    if (!(cond)) {\
        printf("%s:%s:%d Assersion failed: ", __FILE__, __func__, __LINE__);\
        printf(msg,##__VA_ARGS__);\
        __builtin_debugtrap();\
    }
#endif

#define BUFF_CAP 256
#define ARENA_CAP (1024*100)
#define ASSIGN_ARENA (1024*10)
#define NODE_CAP 3

#define TOKS \
    X(TOK_INV, "INV")\
    X(TOK_NUM, "Number")\
    X(TOK_OP, "Operator")\
    X(TOK_LP, "(")\
    X(TOK_RP, ")")\
    X(TOK_END, "EOF")\
    X(TOK_ID, "Id")\
    X(TOK_DEF, ":")\
    X(TOK_EQ, "=")\
    X(TOK_COMMENT, "#")\
    X(NT_EXPER, "Expression")

#define SERR \
    X(ERR_UNEXP, "Unexpected token") \
    X(ERR_CALL,  "Invalid function call")

#define TZ_SEEK 0
#define TZ_ACC 1

#define ACC_NUM 0
#define ACC_ID 1

#define TREE_NUM 0
#define TREE_BOP 1
#define TREE_UOP 2
#define TREE_FUNC 3
#define TREE_VAR 4
#define TREE_ASSIGN 5
#define TREE_DEFINE 6

#define T_NULL 0
#define T_NUM 1
#define T_LAMBDA 2

#define NULL_LIT (eval_type){0}
#define NUM_LIT(d, p) (eval_type){T_NUM, d, p}


#define FBC_MAX(a, b) ((a) < (b) ? b : a)

#define STACK_ALIGNMENT 16

typedef enum {
#define X(tok, _) tok,
TOKS
#undef X
} token_kind;

const char *tokstr[] = {
#define X(tok, name) [tok] = name,
TOKS
#undef X
};

typedef enum {
#define X(e, _) e,
SERR
#undef X
} err_kind;

const char *serrstr[] = {
#define X(e, name) [e] = name,
SERR
#undef X
};

typedef struct {
    void    *ptr;
    uint64_t offset;
    uint64_t mark;
    uint64_t capacity;
    int free;
} fbc_arena;

typedef struct p_tree p_tree;

typedef struct {
    token_kind kind;
    union {
        struct {
            double num;
            /* todo: calculate percision well */
            int perc;
        };
        char *name;
        char op_id;
    };
} token;

typedef struct {
    size_t cursor;
    int state;
    char *buff;
    size_t len;
} tokenizer;

struct p_tree {
    int kind;
    union {
        token val;
        fbc_arena as_fbc_arena;
        p_tree *lambda;
    };
    p_tree *nodes[NODE_CAP];     
};

typedef struct {
    int type;
    union {
        struct {
            double num;
            int perc;
        };
        struct {
            p_tree *lambda;
            fbc_arena as_fbc_arena;
        };
    };
} eval_type;


typedef struct {
    char *name;
    eval_type val;
} var_entry;

typedef struct {
    var_entry *items;
    size_t count;
    size_t capacity;
} var_table;

typedef struct {
    int l;
    int r;
} pair;

typedef struct {
    char *serr_str;
    int fbc_err;
    fbc_arena p_fbc_arena;
    fbc_arena lit_fbc_arena;
    fbc_arena lambda_fbc_arena;
    var_table gvar_table;
} fbc_ctx;

fbc_ctx fbc_init(void);
eval_type fbc_line(char *line, fbc_ctx* ctx);
int fbc_did_error(fbc_ctx *ctx);
char *fbc_get_error(fbc_ctx *ctx);

#ifdef FBC_IMPLEMENTATION

pair op_bind[128] = {
    ['+'] = (pair){1, 2},
    ['-'] = (pair){1, 2},
    ['*'] = (pair){3, 4},
    ['/'] = (pair){3, 4},
    ['^'] = (pair){6, 7},
};

pair lop_bind[128] = {
  ['-'] = (pair){0, 1},  
  ['~'] = (pair){0, 1},
};

int fbc_did_error(fbc_ctx *ctx) {
    return ctx->fbc_err;
}

char *fbc_get_error(fbc_ctx *ctx) {
    return ctx->serr_str;
}


fbc_arena fbc_arena_init(uint64_t capacity);
void *fbc_arena_alloc(fbc_arena *s, uint64_t size);
void fbc_arena_set_frame(fbc_arena *s);
void fbc_arena_pop(fbc_arena *s);
void fbc_arena_destroy(fbc_arena *s);

fbc_arena fbc_arena_init(uint64_t capacity) {
    fbc_arena s = {
        .ptr = malloc(capacity),
        .capacity = capacity,
        .mark = 0,
        .offset = 0,
    };

    xassert(s.ptr, "Cannot allocate memory of size %llu for fbc_arena!\n", capacity);

    return s;
}

void *fbc_arena_alloc(fbc_arena *s, uint64_t size) {
    xassert(size, "Trying to allocate memory of size zero!\n"); 

    size += size % STACK_ALIGNMENT == 0 ? 0 : (STACK_ALIGNMENT - size % STACK_ALIGNMENT);
    xassert(s->offset+size <= s->capacity, "Allocation of %llu bytes is out of the bounds of fbc_arena %p!\n", size, s);   
    uint64_t offset = s->offset;
    s->offset += size;
    return (char *)(s->ptr) + offset;
}

void fbc_arena_set_frame(fbc_arena *s) {
    uint64_t offset = s->offset;
    uint64_t *old_fp = fbc_arena_alloc(s, sizeof(uint64_t));
    old_fp[0] = s->mark;
    s->mark = offset;
}

void fbc_arena_pop(fbc_arena *s) {
    s->offset = s->mark;
    if (s->offset == 0) return;
    uint64_t old_fp = ((uint64_t *) ((char*)s->ptr + s->offset))[0];
    s->mark = old_fp;
}

void fbc_arena_destroy(fbc_arena *s) {
    xassert(s->free == 0, "Trying to free a free arena!\n");
    if (s->free == 0)
        free(s->ptr);
    s->free = 1;
}

int is_op(char c) {
    return op_bind[c].r > 0 || lop_bind[c].r > 0;
}

typedef struct {
    char *ptr;
    size_t offset;
    size_t len;
} sstr;

char *sstrdup(sstr s, fbc_arena *a) {
    char *cpy = NULL;
    if (a == NULL)
        cpy = malloc(s.len+1);
    else
        cpy = fbc_arena_alloc(a, s.len+1);
    memcpy(cpy, s.ptr+s.offset, s.len);
    cpy[s.len] = 0;
    return cpy;
}

int next_token(tokenizer *tk, token *out, fbc_ctx *ctx) {
    tk->state = TZ_SEEK;
    
    sstr s = {0};
    int perc = -1;
    int acc = 0;
    for (;;) {
        char c = tk->buff[tk->cursor];

        if (tk->cursor >= tk->len) {
            out->kind = TOK_END;
            return TOK_END; 
        }

        if (tk->state == TZ_SEEK) {
            if (isspace(c) == 0) {
                if (isalnum(c) == 1) {
                    s.ptr = tk->buff;
                    s.offset = tk->cursor;
                    tk->state = TZ_ACC;
                    if (isdigit(c) != 0) acc = ACC_NUM; else acc = ACC_ID;
                }

                if (is_op(c) == 1) {
                    out->kind = TOK_OP;
                    out->op_id = c;
                    tk->cursor++;
                    return TOK_OP;
                }

                if (c == '(') {tk->cursor++;return TOK_LP;}
                if (c == ')') {tk->cursor++;return TOK_RP;}
                if (c == ':') {tk->cursor++;return TOK_DEF;}
                if (c == '=') {tk->cursor++;return TOK_EQ;}
                if (c == '#') {tk->cursor++;return TOK_COMMENT;}

            }
        }

        if (tk->state == TZ_ACC) {
            if (acc == ACC_NUM) {
                if (isdigit(c) == 0 && c != '.') {
                    char *tmp = sstrdup(s, NULL);
                    out->num = atof(tmp);
                    out->perc = perc < 0 ? 0 : perc;
                    out->kind = TOK_NUM;
                    free(tmp);
                    return TOK_NUM;
                } else if (perc >= 0) {
                    perc++;
                }

                if (perc < 0 && c == '.')
                    perc = 0;
            }
            if (acc == ACC_ID) {
                if (isalnum(c) == 0 && c != '_') {
                    out->name = sstrdup(s, &ctx->lit_fbc_arena);
                    out->kind = TOK_ID;
                    return TOK_ID;
                }
            }
            s.len++;
        }

        tk->cursor++;
    }
}

int expect(tokenizer tz, int kind, fbc_ctx *ctx) {
    token next = {0};
    return kind == next_token(&tz, &next, ctx);
}

void skip(tokenizer *tz, fbc_ctx *ctx) {
    token next = {0};
    next_token(tz, &next, ctx);
}

int peek(tokenizer tz, fbc_ctx *ctx) {
    token next = {0};
    return next_token(&tz, &next, ctx);
}

token peek_whole_token(tokenizer tz, fbc_ctx *ctx) {
    token next = {0};
    next_token(&tz, &next, ctx);
    return next;
}

void report_serr(fbc_ctx *ctx, tokenizer tz, err_kind kind, ...) {
    if (ctx->fbc_err != 0)
        return;
    ctx->fbc_err = 1;
#ifdef FBC_PRINT_SERR
    puts(tz.buff);
    printf("%*s <-- Here\n", (int)tz.cursor+1, "^");
    puts(serrstr[kind]);
#else
    if (ctx->serr_str == NULL)
        ctx->serr_str = fbc_arena_alloc(&ctx->lit_fbc_arena, 256);
    memset(ctx->serr_str, 0, 256);
    int off = snprintf(ctx->serr_str, 256, "%s\n", tz.buff);
    off += snprintf(ctx->serr_str+off, 256-off, "%*s <-- Here\n", (int)tz.cursor+1, "^");
    off += snprintf(ctx->serr_str+off, 256-off, "%s\n", serrstr[kind]);
#endif
    va_list l;
    va_start(l, kind);
    if (kind == ERR_UNEXP) {
        int target = va_arg(l, int);
        int got = va_arg(l, int);
#ifdef FBC_PRINT_SERR
        printf("Expected '%s', got '%s'\n", tokstr[target], tokstr[got]);
#else
        off += snprintf(ctx->serr_str+off, 256-off, "Expected '%s', got '%s'\n", tokstr[target], tokstr[got]);
#endif
    }
    va_end(l);
}


eval_type eval(p_tree *root, var_table *table, fbc_ctx *ctx);

eval_type get_var(char *name, int *exists, var_table *table) {
    for (int i = 0; i < table->count; ++i) {
        var_entry ent = table->items[i];
        if (strcmp(name, ent.name) == 0) {
            *exists = i;
            return ent.val;
        }
    }
    *exists = -1;
    return NULL_LIT;
}

eval_type assign(p_tree *root, var_table *table, int define, fbc_ctx *ctx) {
    char *name = root->nodes[0]->val.name;
    p_tree *val = root->nodes[1];

    int exists = -1;
    get_var(name, &exists, table);
    if (exists >= 0) {
        if (define == 0) {
            table->items[exists].val = eval(val, table, ctx);
        } else {
            table->items[exists].val.as_fbc_arena = root->as_fbc_arena;
            table->items[exists].val.lambda = val;
            table->items[exists].val.type = T_LAMBDA;
        }
        return table->items[exists].val;
    }

    if (table->count >= table->capacity) {
        table->capacity = table->capacity == 0 ? 16 : table->capacity * 2;
        table->items = realloc(table->items, table->capacity * sizeof(var_entry));
        xassert(table->items, "Out of memory!\n");
    }

    if (define == 0) {
        table->items[table->count].val = eval(val, table, ctx);
    } else {
        table->items[table->count].val.lambda = val;
        table->items[table->count].val.as_fbc_arena = root->as_fbc_arena;
        table->items[table->count].val.type = T_LAMBDA;
    }

    table->items[table->count].name = name;

    return table->items[table->count++].val;
}


eval_type eval_assign(p_tree *root, var_table *table, fbc_ctx *ctx) {
    return assign(root, table, 0, ctx);    
}

eval_type eval_define(p_tree *root, var_table *table, fbc_ctx *ctx) {
    return assign(root, table, 1, ctx);    
}


eval_type eval_var(p_tree *root, var_table *table, fbc_ctx *ctx) {
    int exists = -1;
    eval_type val = get_var(root->val.name, &exists, table);    

    if (exists < 0)
        return NULL_LIT;
    if (val.type == T_NUM) {
        return val;
    } else if (val.type == T_LAMBDA) {
        return eval(val.lambda, table, ctx);
    }

    return NULL_LIT;
}

eval_type eval_uop(p_tree *root, var_table *table, fbc_ctx *ctx) {
    eval_type num1 = eval(root->nodes[0], table, ctx);
    if (num1.type != T_NUM)
        return NULL_LIT;
    switch (root->val.op_id) {
        case '-':
            return NUM_LIT(-num1.num, num1.perc);
        case '~':
            return NUM_LIT(~((int)num1.num), 0);
       default:
            return NULL_LIT;
    }
}

eval_type eval_bop(p_tree *root, var_table *table, fbc_ctx *ctx) {
    eval_type num1 = eval(root->nodes[0], table, ctx);
    if (num1.type != T_NUM)
        return NULL_LIT;
    eval_type num2 = eval(root->nodes[1], table, ctx);
    if (num2.type != T_NUM)
        return NULL_LIT;

    switch (root->val.op_id) {
        case '+':
            return NUM_LIT(num1.num + num2.num, FBC_MAX(num1.perc, num2.perc));
        case '&':
            return NUM_LIT((int)num1.num & (int)num2.num, 0);
        case '|':
            return NUM_LIT((int)num1.num | (int)num2.num, 0);
        case '*':
            return NUM_LIT(num1.num * num2.num, FBC_MAX(num1.perc, num2.perc));
        case '-':
            return NUM_LIT(num1.num - num2.num, FBC_MAX(num1.perc, num2.perc));
        case '/': {
            if (num2.num == 0)
                return NULL_LIT;
            return NUM_LIT(num1.num / num2.num, FBC_MAX(num1.perc, num2.perc));
          }
        case '^': 
            return NUM_LIT(pow(num1.num, num2.num), FBC_MAX(num1.perc, num2.perc));
       default:
                  return NULL_LIT;
    }
}


int eval_builtin(char *name, var_table *table, eval_type *ret, fbc_ctx *ctx) {

    if (name == NULL)
        return 0;

    if (strcmp(name, "clear") == 0) {
        system("clear");
        return 1;
    }

    if (strcmp(name, "exit") == 0) {
        int exists = -1;
        eval_type t_code = get_var("code", &exists, table);
        int code = 0;
        if (exists >= 0)
            code = eval(t_code.lambda, table, ctx).num;
        exit(code);
        // return 1;
    }

    if (strcmp(name, "if") == 0) {
        int exists = -1;
        eval_type t_cond = get_var("cond", &exists, table);
        int cond = 0;
        if (exists >= 0) {
            if (t_cond.type == T_NUM) {
                cond = (int)t_cond.num;
            } else {
                return 1;
            }
        }

        eval_type t_then = get_var("then", &exists, table);
        if (t_then.type != T_LAMBDA)
            return 1;
        if (cond != 0 && exists >= 0) {
            *ret = eval(t_then.lambda, table, ctx);
            return 1;
        }

        eval_type t_else = get_var("else", &exists, table);
        if (t_else.type != T_LAMBDA)
            return 1;
        if (cond == 0 && exists >= 0) {
            *ret = eval(t_else.lambda, table, ctx);
        }
        return 1;
    }
    
    return 0;
}

eval_type eval_func(p_tree *root, var_table *table, fbc_ctx *ctx) {
    var_table table_cpy = {0};
    memcpy(&table_cpy, table, sizeof(var_table));
    table_cpy.items = malloc(table->capacity * sizeof(var_entry));
    memcpy(table_cpy.items, table->items, table->capacity * sizeof(var_entry));

    char *name = NULL;
    if (root->lambda->val.kind == TOK_ID)
        name = root->lambda->val.name;

    p_tree *argv = root->nodes[0];

    if (argv != NULL)
        eval(argv, &table_cpy, ctx);

    eval_type t = NULL_LIT;

    if (!eval_builtin(name, &table_cpy, &t, ctx))
        t = eval(root->lambda, &table_cpy, ctx);

    free(table_cpy.items);
    /* Todo: memory management */
    //fbc_arena_destroy(&argv->as_fbc_arena);
    return t;
}

eval_type eval(p_tree *root, var_table *table, fbc_ctx *ctx) {
    switch (root->kind) {
        case TREE_NUM:
            return NUM_LIT(root->val.num, root->val.perc);
        case TREE_BOP:
            return eval_bop(root, table, ctx);
        case TREE_UOP:
            return eval_uop(root, table, ctx);
        case TREE_FUNC:
            return eval_func(root, table, ctx);
        case TREE_VAR:
            return eval_var(root, table, ctx);
        case TREE_ASSIGN:
            return eval_assign(root, table, ctx);
        case TREE_DEFINE:
            return eval_define(root, table, ctx);
        default:
            return NULL_LIT;
    }
    return NULL_LIT;
}

p_tree *parse_expr(tokenizer *tz, int min_b, fbc_arena *a, fbc_ctx *ctx);

p_tree *parse_callable(tokenizer *tz, p_tree *lval, fbc_arena *a, fbc_ctx *ctx) {
    p_tree *lambda = fbc_arena_alloc(a, sizeof(p_tree));
    lambda->kind = TREE_FUNC;
    lambda->lambda = lval;

    int ntok = peek(*tz, ctx);
    if (ntok == TOK_RP) {
        skip(tz, ctx);
        return lambda;
    }

    p_tree *arg1 = parse_expr(tz, 0, a, ctx);
    if (arg1 == NULL) {
        report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
        return NULL;
    }

    lambda->nodes[0] = arg1;

    /* We expect only one (or no) argument */
    if (expect(*tz, TOK_RP, ctx)) {
        skip(tz, ctx);
        return lambda;
    }

    tz->cursor++;
    report_serr(ctx, *tz, ERR_CALL);
    return NULL;
}

p_tree *parse_pexpr(tokenizer *tz, int min_b, fbc_arena *a, fbc_ctx *ctx) {
    int tok = peek(*tz, ctx);
    if (tok == TOK_NUM) {
        token num = {0};
        next_token(tz, &num, ctx);
        p_tree *t = fbc_arena_alloc(a, sizeof(p_tree));
        memset(t, 0, sizeof(p_tree));
        t->val = num;
        t->kind = TREE_NUM;
        return t;
    } else if(tok == TOK_LP) {
        skip(tz, ctx);
        p_tree *t = parse_expr(tz, 0, a, ctx);
        if (t == NULL) {
            report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
            return NULL;
        }
        if (!expect(*tz, TOK_RP, ctx)) {
            report_serr(ctx, *tz, ERR_UNEXP, TOK_RP, peek(*tz, ctx));
            return NULL;
        }
        skip(tz, ctx);
        return t;
    } else if (tok == TOK_OP) {
            token tok_op = {0};
            next_token(tz, &tok_op, ctx);
            pair binding = lop_bind[tok_op.op_id];
            p_tree *t = parse_pexpr(tz, 0, a, ctx);
            if (t == NULL) {
                report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
                return NULL;
            }
            p_tree *op = fbc_arena_alloc(a, sizeof(p_tree));
            op->kind = TREE_UOP;
            op->val = tok_op;
            op->nodes[0] = t;
            return op;
    } else if (tok == TOK_ID) {
        token id = {0};
        next_token(tz, &id, ctx);     
        p_tree *lval = fbc_arena_alloc(a, sizeof(p_tree));
        memset(lval, 0, sizeof(p_tree));
        lval->val = id;
        lval->kind = TREE_VAR;
        int id_kind = peek(*tz, ctx);

        if (id_kind == TOK_EQ || id_kind == TOK_DEF) {
            token tok_eq = {0};
            int tok_val = next_token(tz, &tok_eq, ctx);

            p_tree *eq = fbc_arena_alloc(&ctx->lit_fbc_arena, sizeof(p_tree));
            memset(eq, 0, sizeof(p_tree));

            fbc_arena as_fbc_arena = fbc_arena_init(ASSIGN_ARENA);
            p_tree *rval = NULL;
            if ((rval = (parse_expr(tz, 0, &as_fbc_arena, ctx))) == NULL) {
                report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
                return NULL;
            }
            eq->as_fbc_arena = as_fbc_arena;
            eq->nodes[0] = lval;
            eq->nodes[1] = rval;
            if (tok_val == TOK_EQ) {
                eq->kind = TREE_ASSIGN;
            } else {
                eq->kind = TREE_DEFINE;
            }

            return eq;
        }
        lval->kind = TREE_VAR;
        return lval;
    }

    return NULL;
}

p_tree *parse_expr(tokenizer *tz, int min_b, fbc_arena *a, fbc_ctx *ctx) {
    p_tree *num1 = NULL;
    int pk = peek(*tz, ctx);
    if (pk == TOK_END)
        return NULL;
    if (pk == TOK_COMMENT)
        return NULL;
    if ((num1 = parse_pexpr(tz, 0, a, ctx)) == NULL) {
        report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
        return NULL;
    }

    if (expect(*tz, TOK_LP, ctx)) {
        skip(tz, ctx);
        num1 = parse_callable(tz, num1, a, ctx);
        for (;;) {
            if (!expect(*tz, TOK_LP, ctx))
                break;
            skip(tz, ctx);
            num1 = parse_callable(tz, num1, a, ctx);
        }
        return num1;
    }

    p_tree *root = fbc_arena_alloc(a, sizeof(p_tree));

    memcpy(root, num1, sizeof(p_tree));

    for (;;) {
        if (!expect(*tz, TOK_OP, ctx))
            break;
        token op = peek_whole_token(*tz, ctx);
        pair binding = op_bind[op.op_id];
        if (binding.l < min_b)
            break;
        skip(tz, ctx);
        root->val = op;
        root->kind = TREE_BOP;
        p_tree *num2 = parse_expr(tz, binding.r, a, ctx);

        if (num2 == NULL) {
            report_serr(ctx, *tz, ERR_UNEXP, NT_EXPER, peek(*tz, ctx));
            return NULL;
        }

        root->nodes[0] = num1;
        root->nodes[1] = num2;
        num1 = fbc_arena_alloc(a, sizeof(p_tree));
        memcpy(num1, root, sizeof(p_tree));
    }
    return root;
}

char *strip_string(char *buff, int *out_len) { 
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
    *out_len = size;
    return res;
}

eval_type fbc_line(char *line, fbc_ctx *ctx) {
        fbc_arena_pop(&ctx->p_fbc_arena);
        memset(ctx->p_fbc_arena.ptr, 0, ctx->p_fbc_arena.capacity);
        ctx->fbc_err = 0;

        int bytes_read = 0;
        line = strip_string(line, &bytes_read);
        bytes_read++;
        tokenizer tz = {0};
        tz.buff = line;
        tz.len = bytes_read;


        p_tree *t = parse_expr(&tz, 0, &ctx->p_fbc_arena, ctx);
        free(line);

        if (t == NULL) {
            fbc_arena_pop(&ctx->p_fbc_arena);
            return NULL_LIT;
        }
        if (!expect(tz, TOK_END, ctx)) {
            fbc_arena_pop(&ctx->p_fbc_arena);
            report_serr(ctx, tz, ERR_UNEXP, TOK_END, peek(tz, ctx));
            return NULL_LIT;
        }

        eval_type val = eval(t, &ctx->gvar_table, ctx);
        fbc_arena_pop(&ctx->p_fbc_arena);
        return val;
}

fbc_ctx fbc_init(void) {
    fbc_ctx ctx = {
        .p_fbc_arena = fbc_arena_init(ARENA_CAP),
        .lit_fbc_arena = fbc_arena_init(ARENA_CAP),
        .lambda_fbc_arena = fbc_arena_init(ARENA_CAP),
    };
    return ctx;
}

void fbc_uninit(fbc_ctx *ctx) {
    fbc_arena_destroy(&ctx->p_fbc_arena);
    fbc_arena_destroy(&ctx->lit_fbc_arena);
    fbc_arena_destroy(&ctx->lambda_fbc_arena);

    for (size_t i = 0; i < ctx->gvar_table.count; ++i) {
        eval_type entry = ctx->gvar_table.items[i].val;
        if (entry.type == T_LAMBDA) {
            fbc_arena_destroy(&entry.as_fbc_arena);
        }
    }

    if (ctx->gvar_table.count > 0)
        free(ctx->gvar_table.items);
}

#   endif // FBC_IMPLEMENTATION
#endif // FBC_H_

