#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <editline.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#include "xassert.h"

#define ARENA_IMPLEMENTATION
#include "arena.h"

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

#define MAX(a, b) ((a) < (b) ? b : a)
#define MIN(a, b) ((a) > (b) ? b : a)

typedef struct p_tree p_tree;

typedef struct {
    token_kind kind;
    union {
        struct {
            double num;
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
        arena as_arena;
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
            arena as_arena;
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


arena p_arena = {0};
arena lit_arena = {0};
int err = 0;

var_table gvar_table = {0};

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

const char *prompt = "> ";

const char *tstr[] = {
    [T_NUM] = "Number",
    [T_NULL] = "Null",
};

int is_op(char c) {
    return op_bind[c].r > 0 || lop_bind[c].r > 0;
}

typedef struct {
    char *ptr;
    size_t offset;
    size_t len;
} sstr;

char *sstrdup(sstr s, arena *a) {
    char *cpy = NULL;
    if (a == NULL)
        cpy = malloc(s.len+1);
    else
        cpy = arena_alloc(a, s.len+1);
    memcpy(cpy, s.ptr+s.offset, s.len);
    cpy[s.len] = 0;
    return cpy;
}

int sstrcmp(sstr s1, sstr s2) {
    if (s1.len != s2.len)
        return -1;
    return strncmp(s1.ptr+s1.offset, s2.ptr+s2.offset, s1.len);
}

int next_token(tokenizer *tk, token *out) {
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
                    out->name = sstrdup(s, &lit_arena);
                    out->kind = TOK_ID;
                    return TOK_ID;
                }
            }
            s.len++;
        }

        tk->cursor++;
    }
}

int expect(tokenizer tz, int kind) {
    token next = {0};
    return kind == next_token(&tz, &next);
}

void skip(tokenizer *tz) {
    token next = {0};
    next_token(tz, &next);
}

int peek(tokenizer tz) {
    token next = {0};
    return next_token(&tz, &next);
}

token peek_whole_token(tokenizer tz) {
    token next = {0};
    next_token(&tz, &next);
    return next;
}

int exp_type(eval_type t, int target) {
    if (t.type != target) {
        //printf("Expected %s, got %s\n", tstr[target], tstr[t.type]);
        return 0;
    }
    return 1;
}

int exp_argc(int argc, int target) {
    if (argc != target) {
        printf("Expected %d argument(s), got %d\n", target, argc);
        return 0;
    }
    return 1;
}

void report_serr(tokenizer tz, err_kind kind, ...) {
    if (err != 0)
        return;
    err = 1;
    puts(tz.buff);
    printf("%*s <-- Here\n", (int)tz.cursor+1, "^");
    puts(serrstr[kind]);
    va_list l;
    va_start(l, kind);
    if (kind == ERR_UNEXP) {
        int target = va_arg(l, int);
        int got = va_arg(l, int);
        printf("Expected '%s', got '%s'\n", tokstr[target], tokstr[got]);
    }
    va_end(l);
}


eval_type eval(p_tree *root, var_table *table);

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


eval_type assign(p_tree *root, var_table *table, int define) {
    char *name = root->nodes[0]->val.name;
    p_tree *val = root->nodes[1];

    int exists = -1;
    get_var(name, &exists, table);
    if (exists >= 0) {
        if (define == 0) {
            table->items[exists].val = eval(val, table);
        } else {
            /* Auto ref counting for bound symbols */
            arena_unref(&table->items[exists].val.as_arena);
            arena_ref(&root->as_arena);
            table->items[exists].val.as_arena = root->as_arena;
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
        table->items[table->count].val = eval(val, table);
    } else {
        arena_ref(&root->as_arena);
        table->items[table->count].val.lambda = val;
        table->items[table->count].val.as_arena = root->as_arena;
        table->items[table->count].val.type = T_LAMBDA;
    }

    table->items[table->count].name = name;

    return table->items[table->count++].val;
}


eval_type eval_assign(p_tree *root, var_table *table) {
    return assign(root, table, 0);    
}

eval_type eval_define(p_tree *root, var_table *table) {
    return assign(root, table, 1);    
}


eval_type eval_var(p_tree *root, var_table *table) {
    int exists = -1;
    eval_type val = get_var(root->val.name, &exists, table);    

    if (exists < 0)
        return NULL_LIT;
    if (val.type == T_NUM) {
        return val;
    } else if (val.type == T_LAMBDA) {
        return eval(val.lambda, table);
    }

    return NULL_LIT;
}

eval_type eval_uop(p_tree *root, var_table *table) {
    eval_type num1 = eval(root->nodes[0], table);
    if (!exp_type(num1, T_NUM))
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

eval_type eval_bop(p_tree *root, var_table *table) {
    eval_type num1 = eval(root->nodes[0], table);
    if (!exp_type(num1, T_NUM)) {
        return NULL_LIT;
    }
    eval_type num2 = eval(root->nodes[1], table);
    if (!exp_type(num2, T_NUM)) {
        return NULL_LIT;
    }

    switch (root->val.op_id) {
        case '+':
            return NUM_LIT(num1.num + num2.num, MAX(num1.perc, num2.perc));
        case '&':
            return NUM_LIT((int)num1.num & (int)num2.num, 0);
        case '|':
            return NUM_LIT((int)num1.num | (int)num2.num, 0);
        case '*':
            return NUM_LIT(num1.num * num2.num, MAX(num1.perc, num2.perc));
        case '-':
            return NUM_LIT(num1.num - num2.num, MAX(num1.perc, num2.perc));
        case '/': {
            if (num2.num == 0) {
                printf("Cannot divide by zero\n");
                return NULL_LIT;
            }
            return NUM_LIT(num1.num / num2.num, MAX(num1.perc, num2.perc));
          }
        case '^': 
            return NUM_LIT(pow(num1.num, num2.num), MAX(num1.perc, num2.perc));
       default:
                  return NULL_LIT;
    }
}

arena lambda_arena = {0};

int eval_builtin(char *name, var_table *table, eval_type *ret) {

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
            code = eval(t_code.lambda, table).num;
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
            *ret = eval(t_then.lambda, table);
            return 1;
        }

        eval_type t_else = get_var("else", &exists, table);
        if (t_else.type != T_LAMBDA)
            return 1;
        if (cond == 0 && exists >= 0) {
            *ret = eval(t_else.lambda, table);
        }
        return 1;
    }
    
    return 0;
}

eval_type eval_func(p_tree *root, var_table *table) {

    arena_set_frame(&lambda_arena);

    var_table *table_cpy = arena_alloc(&lambda_arena, sizeof(var_table));
    memcpy(table_cpy, table, sizeof(var_table));
    table_cpy->items = arena_alloc(&lambda_arena, table->capacity * sizeof(var_entry));
    memcpy(table_cpy->items, table->items, table->capacity * sizeof(var_entry));

    char *name = NULL;
    if (root->lambda->val.kind == TOK_ID)
        name = root->lambda->val.name;

    p_tree *argv = root->nodes[0];
    arena *as_arena = &argv->as_arena;
    if (argv != NULL) {
        if (argv->kind == TREE_DEFINE) {
            arena_ref(as_arena);
        }
        eval(argv, table_cpy);
    }

    eval_type t = NULL_LIT;

    if (!eval_builtin(name, table_cpy, &t))
        t = eval(root->lambda, table_cpy);

    if (argv != NULL) {
        if (argv->kind == TREE_DEFINE) {
            arena_unref(as_arena);
        }
        eval(argv, table_cpy);
    }

    arena_pop(&lambda_arena);
    return t;
}

eval_type eval(p_tree *root, var_table *table) {
    switch (root->kind) {
        case TREE_NUM:
            return NUM_LIT(root->val.num, root->val.perc);
        case TREE_BOP:
            return eval_bop(root, table);
        case TREE_UOP:
            return eval_uop(root, table);
        case TREE_FUNC:
            return eval_func(root, table);
        case TREE_VAR:
            return eval_var(root, table);
        case TREE_ASSIGN:
            return eval_assign(root, table);
        case TREE_DEFINE:
            return eval_define(root, table);
        default:
            return NULL_LIT;
    }
    return NULL_LIT;
}

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a);

p_tree *parse_callable(tokenizer *tz, p_tree *lval, arena *a) {
    p_tree *lambda = arena_alloc(a, sizeof(p_tree));
    lambda->kind = TREE_FUNC;
    lambda->lambda = lval;

    if (expect(*tz, TOK_RP)) {
        skip(tz);
        return lambda;
    }

    p_tree *arg1 = parse_expr(tz, 0, a);
    if (arg1 == NULL) {
        report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
        return NULL;
    }

    lambda->nodes[0] = arg1;

    /* We expect only one (or no) argument */
    if (expect(*tz, TOK_RP)) {
        skip(tz);
        return lambda;
    }

    tz->cursor++;
    report_serr(*tz, ERR_CALL);
    return NULL;
}

p_tree *parse_pexpr(tokenizer *tz, int min_b, arena *a) {
    if (expect(*tz, TOK_NUM)) {
        token num = {0};
        next_token(tz, &num);
        p_tree *t = arena_alloc(a, sizeof(p_tree));
        memset(t, 0, sizeof(p_tree));
        t->val = num;
        t->kind = TREE_NUM;
        return t;
    } else if(expect(*tz, TOK_LP)) {
        skip(tz);
        p_tree *t = parse_expr(tz, 0, a);
        if (t == NULL) {
            report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
            return NULL;
        }
        if (!expect(*tz, TOK_RP)) {
            report_serr(*tz, ERR_UNEXP, TOK_RP, peek(*tz));
            return NULL;
        }
        skip(tz);
        return t;
    } else if (expect(*tz, TOK_OP)) {
            token tok_op = {0};
            next_token(tz, &tok_op);
            pair binding = lop_bind[tok_op.op_id];
            p_tree *t = parse_pexpr(tz, 0, a);
            if (t == NULL) {
                report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
                return NULL;
            }
            p_tree *op = arena_alloc(a, sizeof(p_tree));
            op->kind = TREE_UOP;
            op->val = tok_op;
            op->nodes[0] = t;
            return op;
    } else if (expect(*tz, TOK_ID)) {
        token id = {0};
        next_token(tz, &id);     
        p_tree *lval = arena_alloc(a, sizeof(p_tree));
        memset(lval, 0, sizeof(p_tree));
        lval->val = id;
        lval->kind = TREE_VAR;

        if (expect(*tz, TOK_EQ) || expect(*tz, TOK_DEF)) {
            token tok_eq = {0};
            int tok_val = next_token(tz, &tok_eq);

            p_tree *eq = arena_alloc(a, sizeof(p_tree));
            memset(eq, 0, sizeof(p_tree));

            arena as_arena = arena_init(ASSIGN_ARENA);
            p_tree *rval = NULL;
            if ((rval = (parse_expr(tz, 0, &as_arena))) == NULL) {
                report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
                return NULL;
            }
            eq->as_arena = as_arena;
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

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a) {
    p_tree *num1 = NULL;
    int pk = peek(*tz);
    if (pk == TOK_END)
        return NULL;
    if (pk == TOK_COMMENT)
        return NULL;
    if ((num1 = parse_pexpr(tz, 0, a)) == NULL) {
        report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
        return NULL;
    }

    if (expect(*tz, TOK_LP)) {
        skip(tz);
        num1 = parse_callable(tz, num1, a);
        for (;;) {
            if (!expect(*tz, TOK_LP))
                break;
            skip(tz);
            num1 = parse_callable(tz, num1, a);
        }
        return num1;
    }

    p_tree *root = arena_alloc(a, sizeof(p_tree));

    memcpy(root, num1, sizeof(p_tree));

    for (;;) {
        if (!expect(*tz, TOK_OP))
            break;
        token op = peek_whole_token(*tz);
        pair binding = op_bind[op.op_id];
        if (binding.l < min_b)
            break;
        skip(tz);
        root->val = op;
        root->kind = TREE_BOP;
        p_tree *num2 = parse_expr(tz, binding.r, a);

        if (num2 == NULL) {
            report_serr(*tz, ERR_UNEXP, NT_EXPER, peek(*tz));
            return NULL;
        }

        root->nodes[0] = num1;
        root->nodes[1] = num2;
        num1 = arena_alloc(a, sizeof(p_tree));
        memcpy(num1, root, sizeof(p_tree));
    }
    return root;
}

eval_type pe_line(char *line) {
        memset(p_arena.ptr, 0, p_arena.capacity);
        err = 0;

        size_t bytes_read = strlen(line)+1;
        tokenizer tz = {0};
        tz.buff = line;
        tz.len = bytes_read;


        p_tree *t = parse_expr(&tz, 0, &p_arena);

        if (t == NULL) {
            arena_pop(&p_arena);
            return NULL_LIT;
        }
        if (!expect(tz, TOK_END)) {
            arena_pop(&p_arena);
            report_serr(tz, ERR_UNEXP, TOK_END, peek(tz));
            return NULL_LIT;
        }

        eval_type val = eval(t, &gvar_table);
        arena_pop(&p_arena);
        return val;
}

void eval_file(char *fn) {
    FILE *f = fopen(fn, "r");
    xassert(f, "Cannot open file!\n%s: %s\n", fn, strerror(errno));
    size_t bcap = 1028;
    char *buff = malloc(bcap);
    xassert(buff, "Cannot allocate buffer for reading file\n");
    memset(buff, 0, bcap);
    while (fgets(buff, bcap, f)) {
        pe_line(buff);
        memset(buff, 0, bcap);
    }
    fclose(f);
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);   // Parse arena. Wiped each iteration.
    lit_arena = arena_init(ARENA_CAP); // Arena for string literals, like the names of variables.
    lambda_arena = arena_init(ARENA_CAP);

    srand(time(NULL));
    eval_file("std.fbc");

    for (;;) {
        char *buff = readline(prompt);
        if (buff == NULL) /* C-d EOF */
            exit(0);
        
        eval_type val = pe_line(buff);
        if (val.type == T_NUM)
            printf("%.*f\n", val.perc, val.num);
    }
    return 0;
}
