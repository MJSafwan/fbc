#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <editline.h>
#include <time.h>
#include <stdarg.h>

#include "xassert.h"

#define ARENA_IMPLEMENTATION
#include "arena.h"

#define BUFF_CAP 256
#define ARENA_CAP (1024*100)
#define NODE_CAP 3

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

#define TREE_NUM 0
#define TREE_BOP 1
#define TREE_UOP 2
#define TREE_FUNC 3
#define TREE_VAR 4
#define TREE_ASSIGN 5

#define T_NULL 0
#define T_NUM 1
#define T_LAMBDA 2

#define NULL_LIT (eval_type){0}
#define NUM_LIT(d) (eval_type){T_NUM, d}

typedef struct p_tree p_tree;

typedef struct {
    int kind;
    union {
        double num;
        char *name;
        char op_id;
    };
} token;

typedef struct {
    int type;
    union {
        double num;
        p_tree *lambda;
    };
} eval_type;

typedef struct {
    size_t cursor;
    int state;
    const char *buff;
    size_t len;
} tokenizer;


struct p_tree {
    int kind;
    union {
        token val;
        p_tree *lambda;
    };
    p_tree *nodes[NODE_CAP];     
};


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
    ['+'] = (pair){2, 3},
    ['-'] = (pair){2, 3},
    ['%'] = (pair){3, 4},
    ['*'] = (pair){5, 6},
    ['&'] = (pair){3, 4},
    ['|'] = (pair){2, 3},
    ['/'] = (pair){5, 6},
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
                    s = arena_alloc(&lit_arena, tk->len);
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
            if (!isalnum(c) && c != '_') {
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

void report_serr(tokenizer tz) {
    if (err != 0)
        return;
    printf("%*s <-- Here\n", tz.cursor+strlen(prompt), "^");
    printf("Syntax error!\n");
    err = 1;
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


/* All variables are bound to a lambda type */
eval_type assign(char *name, p_tree *val, var_table *table) {
    int exists = -1;
    get_var(name, &exists, table);
    if (exists >= 0) {
        table->items[exists].val.lambda = val;
        table->items[exists].val.type = T_LAMBDA;
        return eval(val, table);
    }

    if (table->count >= table->capacity) {
        table->capacity = table->capacity == 0 ? 16 : table->capacity * 2;
        table->items = realloc(table->items, table->capacity * sizeof(var_entry));
        xassert(table->items, "Out of memory!\n");
    }

    table->items[table->count].val.lambda = val;
    table->items[table->count].name = name;
    table->count++;
    return eval(val, table);
}


eval_type eval_assign(p_tree *root, var_table *table) {
    return assign(root->nodes[0]->val.name, root->nodes[1], table);    
}

eval_type eval_var(p_tree *root, var_table *table) {
    int exists = -1;
    eval_type val = get_var(root->val.name, &exists, table);    
    if (exists < 0)
        return NULL_LIT;
    return eval(val.lambda, table);
}

eval_type eval_uop(p_tree *root, var_table *table) {
    eval_type num1 = eval(root->nodes[0], table);
    if (!exp_type(num1, T_NUM))
        return NULL_LIT;
    switch (root->val.op_id) {
        case '-':
            return NUM_LIT(-num1.num);
        case '~':
            return NUM_LIT(~((int)num1.num));
       default:
            return NULL_LIT;
    }
}

eval_type eval_bop(p_tree *root, var_table *table) {
    eval_type t_num1 = eval(root->nodes[0], table);
    if (!exp_type(t_num1, T_NUM)) {
        return NULL_LIT;
    }
    eval_type t_num2 = eval(root->nodes[1], table);
    if (!exp_type(t_num2, T_NUM)) {
        return NULL_LIT;
    }

    double num1 = t_num1.num;
    double num2 = t_num2.num;
    switch (root->val.op_id) {
        case '+':
            return NUM_LIT(num1 + num2);
        case '&':
            return NUM_LIT((int)num1 & (int)num2);
        case '|':
            return NUM_LIT((int)num1 | (int)num2);
        case '*':
            return NUM_LIT(num1 * num2);
        case '-':
            return NUM_LIT(num1 - num2);
        case '/': {
            if (num2 == 0) {
                printf("Cannot divide by zero\n");
                return NULL_LIT;
            }
            return NUM_LIT(num1 / num2);
          }
        case '%': {
            if (num2 == 0) {
                printf("Zero cannot be a modulus\n");
                return NULL_LIT;
            }
            return NUM_LIT((int)num1 % (int)num2);
          }
       default:
                  return NULL_LIT;
    }
}

arena lambda_arena = {0};

eval_type eval_func(p_tree *root, var_table *table) {
    arena_set_frame(&lambda_arena);

    var_table *table_cpy = arena_alloc(&lambda_arena, sizeof(var_table));
    memcpy(table_cpy, table, sizeof(var_table));

    table_cpy->items = arena_alloc(&lambda_arena, table->capacity * sizeof(var_entry));
    memcpy(table_cpy->items, table->items, table->capacity * sizeof(var_entry));
    
    p_tree **argv = root->nodes;
    for (int i = 0; argv[i] != NULL; ++i) {
        eval(argv[i], table_cpy);
    }

    eval_type t = eval(root->lambda, table_cpy);
    arena_pop(&lambda_arena);
    return t;
}

eval_type eval(p_tree *root, var_table *table) {
    switch (root->kind) {
        case TREE_NUM:
            return NUM_LIT(root->val.num);
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
        default:
            return NULL_LIT;
    }
    return NULL_LIT;
}

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a);

p_tree *parse_callable(tokenizer *tz, p_tree *lval, arena *a) {
    if (lval == NULL) {
        report_serr(*tz);
        return NULL;
    }

    p_tree *lambda = arena_alloc(&p_arena, sizeof(p_tree));
    lambda->kind = TREE_FUNC;
    lambda->lambda = lval;

    p_tree *arg1 = parse_expr(tz, 0, a);
    if (arg1 == NULL) {
        report_serr(*tz);
        return NULL;
    }

    lambda->nodes[0] = arg1;

    /* We expect one and only one argument */
    if (expect(*tz, TOK_RP)) {
        skip(tz);
        return lambda;
    }

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
            report_serr(*tz);
            return NULL;
        }
        if (!expect(*tz, TOK_RP)) {
            report_serr(*tz);
            return NULL;
        }
        skip(tz);
        return t;
    } else if (expect(*tz, TOK_OP)) {
            token tok_op = {0};
            next_token(tz, &tok_op);
            pair binding = lop_bind[tok_op.op_id];
            if (binding.r == 0) {
                report_serr(*tz);
                return NULL;
            }
            p_tree *t = parse_pexpr(tz, min_b, a);
            if (t == NULL) {
                report_serr(*tz);
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
        id.name = id.name;
        p_tree *lval = arena_alloc(a, sizeof(p_tree));
        memset(lval, 0, sizeof(p_tree));
        lval->val = id;
        lval->kind = TREE_VAR;

        if (expect(*tz, TOK_EQ)) {
            token tok_eq = {0};
            next_token(tz, &tok_eq);
            p_tree *eq = arena_alloc(a, sizeof(p_tree));
            memset(eq, 0, sizeof(p_tree));
            p_tree *rval = NULL;
            if ((rval = (parse_expr(tz, 0, &lit_arena))) == NULL) {
                report_serr(*tz);
                return NULL;
            }
            eq->nodes[0] = lval;
            eq->nodes[1] = rval;
            eq->kind = TREE_ASSIGN;
            return eq;
        }
        lval->kind = TREE_VAR;
        return lval;
    }

    report_serr(*tz);
    return NULL;
}

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a) {
    p_tree *num1 = NULL;
    token pk = peek(*tz);
    if (pk.kind == TOK_END)
        return NULL;
    if ((num1 = parse_pexpr(tz, min_b, a)) == NULL) {
        report_serr(*tz);
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
    p_tree *ct = root;
    for (;;) {
        if (!expect(*tz, TOK_OP))
            break;
        token op = peek(*tz);
        pair binding = op_bind[op.op_id];
        if (binding.l < min_b)
            break;
        skip(tz);
        ct->val = op;
        ct->kind = TREE_BOP;
        p_tree *num2 = parse_expr(tz, binding.r, a);
        if (num2 == NULL) {
            report_serr(*tz);
            return NULL;
        }
        ct->nodes[0] = num1;
        ct->nodes[1] = num2;
        num1 = arena_alloc(&p_arena, sizeof(p_tree));
        memcpy(num1, num2, sizeof(p_tree));
        ct = num2;
    }
    return root;
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);   // Parse arena. Wiped each iteration.
    lit_arena = arena_init(ARENA_CAP); // Arena for string literals, like the names of variables.
    lambda_arena = arena_init(ARENA_CAP);

    srand(time(NULL));
    for (;;) {
        memset(p_arena.ptr, 0, p_arena.capacity);
        err = 0;

        char *buff = readline(prompt);
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
        p_tree *t = parse_expr(&tz, 0, &p_arena);

        if (t == NULL) {
            arena_pop(&p_arena);
            continue;
        }

        eval_type val = eval(t, &gvar_table);
        int type = val.type;
        if (type == T_NUM)
            printf("%f\n", val.num);
        arena_pop(&p_arena);
    }
    return 0;
}
