#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <editline.h>
#include <time.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"

#define BUFF_CAP 256
#define ARENA_CAP (1024*100)
#define NODE_CAP 16

#define TOK_INV 0
#define TOK_NUM 1
#define TOK_OP 2
#define TOK_LP 3
#define TOK_RP 4
#define TOK_END 5
#define TOK_ID 6
#define TOK_COM 7
#define TOK_EQ 8
#define TOK_LAMBDA 9

#define SEEK_CHAR 0
#define READ_NUM 1
#define READ_ID 2

#define TREE_NUM 0
#define TREE_BOP 1
#define TREE_UOP 2
#define TREE_FUNC 3
#define TREE_VAR 4
#define TREE_ASSIGN 5
#define TREE_LAMBDA 6

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

typedef struct p_tree p_tree;

struct p_tree {
    int kind;
    token val;
    p_tree *nodes[NODE_CAP];     
};


typedef struct {
    char *name;
    double val;
} var_entry;

typedef struct {
    var_entry *items;
    size_t count;
    size_t capacity;
} var_table;


typedef struct {
    p_tree *lam;
    p_tree *val;
} lambda_entry;

typedef struct {
    lambda_entry *items;
    size_t count;
    size_t capacity;
} lambda_table;

typedef struct {
    int l;
    int r;
} pair;

arena p_arena = {0};
arena l_arena = {0};

var_table gvar_table = {0};
lambda_table glam_table = {0};

pair op_bind[128] = {
    ['+'] = (pair){1, 2},
    ['-'] = (pair){1, 2},
    ['%'] = (pair){3, 4},
    ['*'] = (pair){5, 6},
    ['/'] = (pair){5, 6},
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

                if (c == '\\') {
                    out->kind = TOK_LAMBDA;
                    tk->cursor++;
                    return TOK_LAMBDA;
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


double eval(p_tree *root, var_table *table);

double get_var(char *name, int *exists, var_table *table) {
    for (int i = 0; i < table->count; ++i) {
        var_entry ent = table->items[i];
        if (strcmp(name, ent.name) == 0) {
            *exists = i;
            return ent.val;
        }
    }
    *exists = -1;
    return 0;
}

p_tree *get_lambda(char *name, int *exists) {
    for (int i = 0; i < glam_table.count; ++i) {
        lambda_entry ent = glam_table.items[i];
        if (strcmp(name, ent.lam->val.name) == 0) {
            *exists = i;
            return ent.val;
        }
    }
    *exists = -1;
    return NULL;
}

double assign(char *name, double val, var_table *table) {
    int exists = -1;
    get_var(name, &exists, table);
    if (exists >= 0) {
        table->items[exists].val = val;
        return val;
    }

    if (table->count >= table->capacity) {
        table->capacity = table->capacity == 0 ? 16 : table->capacity * 2;
        table->items = realloc(table->items, table->capacity * sizeof(var_entry));
        assert(table->items);
    }

    table->items[table->count].val = val;
    table->items[table->count].name = strdup(name);
    table->count++;
    return val;
}

double assign_lamb(p_tree *lambd, p_tree *expr) {
    int exists = -1;
    char *name = lambd->val.name;
    get_lambda(name, &exists);
    if (exists >= 0) {
        glam_table.items[exists].val = expr;
        return 0;
    }

    if (glam_table.count >= glam_table.capacity) {
        glam_table.capacity = glam_table.capacity == 0 ? 16 : glam_table.capacity * 2;
        glam_table.items = realloc(glam_table.items, glam_table.capacity * sizeof(lambda_entry));
        assert(glam_table.items);
    }

    glam_table.items[glam_table.count].lam = lambd;
    glam_table.items[glam_table.count].val = expr;
    glam_table.count++;
    return 0;
}

double eval_assign(p_tree *root, var_table *table) {
    int kind = root->nodes[0]->kind;
    if (kind == TREE_VAR) {
        return assign(root->nodes[0]->val.name, root->nodes[1]->val.num, &gvar_table);    
    } else {
        return assign_lamb(root->nodes[0], root->nodes[1]);
    }
}

double eval_var(p_tree *root, var_table *table) {
    int exists = -1;
    double val = get_var(root->val.name, &exists, table);    
    if (exists < 0) {
        if (table != &gvar_table) {
            val = get_var(root->val.name, &exists, &gvar_table);
            if (exists < 0)
                return 0;
        }
    }
    return val;
}

double eval_uop(p_tree *root, var_table *table) {
    switch (root->val.op_id) {
        case '-':
            return -eval(root->nodes[0], table);
        case '~':
            return ~(int)eval(root->nodes[0], table);
       default:
            return 0;
    }
}

double eval_bop(p_tree *root, var_table *table) {
    switch (root->val.op_id) {
        case '+':
            return eval(root->nodes[0], table) + eval(root->nodes[1], table);
        case '*':
            return eval(root->nodes[0], table) * eval(root->nodes[1], table);
        case '-':
            return eval(root->nodes[0], table) - eval(root->nodes[1], table);
        case '/': {
            double num2 = eval(root->nodes[1], table);
            if (num2 == 0)
                return NAN;
            return eval(root->nodes[0], table) / num2;
          }
        case '%': {
            double num2 = eval(root->nodes[1], table);
            if (num2 == 0)
                return NAN;
            return (int)eval(root->nodes[0], table) % (int)num2;
          }
       default:
                  return 0;
    }
}


double eval_func(p_tree *root, var_table *table) {
    int argc = 0;
    p_tree **argv = root->nodes;
    for (argc = 0; argv[argc] != NULL; ++argc);
    char *name = root->val.name;

    int exists = -1;
    p_tree *ltree = get_lambda(name, &exists);

    if (exists >= 0) {
        p_tree *vars = glam_table.items[exists].lam;
        int i = 0;
        for (i = 0; vars->nodes[i] != NULL; ++i);

        if (i != argc)
            return NAN;

        var_table ltable = {0};

        for (int i = 0; i < argc; ++i) {
            assign(vars->nodes[i]->val.name, eval(argv[i], table), &ltable);
        }

        double val = eval(ltree, &ltable);
        free(ltable.items);
        return val;
    }

    /* 'Meta' functions */

    if (strcmp(name, "debug") == 0) {
        printf("I am the debugging function!\n");
        printf("You gave me %d arguments.\n", argc);
        printf("[");

        for (int i = 0; i < argc; ++i) {
            printf("%f", eval(argv[i], table));
            if (i < argc-1)
                printf(", ");
        }

        printf("]\n");
        return argc;    
    } 

    if (strcmp(name, "clear") == 0) {
        system("clear");
        return 0;
    }

    if (strcmp(name, "exit") == 0) {
        int code = 0;
        if (argc >= 1)
            code = (int)eval(argv[0], table);
        exit(code);
        // return code;
    }

    /* Math functions */
    if (strcmp(name, "sin") == 0) {
        if (argc != 1) {
            return NAN;
        }
        return sinf(eval(argv[0], table));
    }

    if (strcmp(name, "cos") == 0) {
        if (argc != 1) {
            return NAN;
        }
        return cosf(eval(argv[0], table));
    }

    if (strcmp(name, "pow") == 0) {
        if (argc != 2) {
            return NAN;
        }

        return pow(eval(argv[0], table), eval(argv[1], table));
    }

    if (strcmp(name, "exp") == 0) {
        if (argc != 1) {
            return NAN; 
        }
        return exp(eval(argv[0], table));
    }

    if (strcmp(name, "ln") == 0) {
        if (argc != 1) {
            return NAN;
        }
        return log(eval(argv[0], table));
    }

    if (strcmp(name, "choice") == 0) {
        if (argc == 0)
            return 0;
        if (argc == 1)
            return eval(argv[0], table);
        int c = rand() % argc;
        /* Lazy eval */
        return eval(argv[c], table);
    }

    if (strcmp(name, "rand") == 0) {
        size_t max = RAND_MAX;
        if (argc >= 1)
           max = eval(argv[0], table)+1; 
        return rand() % max;
    }

    if (strcmp(name, "log") == 0) {
        if (argc != 2) {
            return NAN;
        }
        return log(eval(argv[0], table))/log(eval(argv[1], table));
    }

    return 0;
}

double eval(p_tree *root, var_table *table) {
    switch (root->kind) {
        case TREE_NUM:
            return root->val.num;
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
            return 0;
    }
    return 0;
}

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a);

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
        if (t == NULL)
            return NULL;
        if (!expect(*tz, TOK_RP))
            return NULL;
        skip(tz);
        return t;
    } else if (expect(*tz, TOK_OP)) {
            token tok_op = {0};
            next_token(tz, &tok_op);
            pair binding = lop_bind[tok_op.op_id];
            if (binding.r == 0)
                return NULL;
            p_tree *t = parse_pexpr(tz, min_b, a);
            if (t == NULL)
                return NULL;
            p_tree *op = arena_alloc(a, sizeof(p_tree));
            op->kind = TREE_UOP;
            op->val = tok_op;
            op->nodes[0] = t;
            return op;
    } else if (expect(*tz, TOK_ID)) {
        token id = {0};
        next_token(tz, &id);     
        id.name = strdup(id.name);
        p_tree *lval = arena_alloc(a, sizeof(p_tree));
        memset(lval, 0, sizeof(p_tree));
        lval->val = id;
        lval->kind = TREE_VAR;

        if (!expect(*tz, TOK_LP)) {
            if (expect(*tz, TOK_EQ)) {
                token tok_eq = {0};
                next_token(tz, &tok_eq);
                p_tree *eq = arena_alloc(a, sizeof(p_tree));
                memset(eq, 0, sizeof(p_tree));
                p_tree *rval = NULL;
                if ((rval = (parse_expr(tz, 0, a))) == NULL)
                    return NULL;
                eq->nodes[0] = lval;
                eq->nodes[1] = rval;
                eq->kind = TREE_ASSIGN;
                return eq;
            }
            lval->kind = TREE_VAR;
            return lval;
        }
        skip(tz);
        lval->kind = TREE_FUNC;

        if (expect(*tz, TOK_RP)) {
            skip(tz);
            return lval;
        }

        p_tree *arg1 = parse_expr(tz, 0, a);
        int argc = 0;
        if (arg1 == NULL)
            return NULL;

        lval->nodes[argc++] = arg1;

        if (expect(*tz, TOK_RP)) {
            skip(tz);
            return lval;
        }

        for (;;) {
            if (argc >= NODE_CAP-1)
                return NULL;
            if (!expect(*tz, TOK_COM))
                return NULL;
            skip(tz);
            if ((lval->nodes[argc++] = parse_expr(tz, 0, a)) == NULL)
                return NULL;
            if (expect(*tz, TOK_RP))
                break;
        }
        lval->nodes[argc] = NULL;
        return lval;
    } else if (expect(*tz, TOK_LAMBDA)) {
        skip(tz);
        if (!expect(*tz, TOK_ID))
            return NULL;
        token tok_id = {0};
        next_token(tz, &tok_id);
        tok_id.name = strdup(tok_id.name);
        if (!expect(*tz, TOK_LP))
            return NULL;
        skip(tz);

        p_tree *id = arena_alloc(&l_arena, sizeof(p_tree)); 
        id->kind = TREE_LAMBDA;
        id->val = tok_id;

        int argc = 0;
        token id1 = {0};
        if (expect(*tz, TOK_ID)) {
            next_token(tz, &id1);
            id1.name = strdup(id1.name);
            p_tree *t = arena_alloc(&l_arena, sizeof(p_tree));
            t->kind = TREE_VAR;
            t->val = id1;
            id->nodes[argc++] = t;
        }
        
        for (;;) {
            if (expect(*tz, TOK_RP)) {
                skip(tz);
                break;
            }
            if (!expect(*tz, TOK_COM))
                return NULL;
            if (!expect(*tz, TOK_ID))
                return NULL;
            if (argc >= NODE_CAP-1)
                return NULL;
            next_token(tz, &id1);
            p_tree *t = arena_alloc(&l_arena, sizeof(p_tree));
            t->kind = TREE_VAR;
            t->val = id1;
            id->nodes[argc++] = t;
        }

        id->nodes[argc] = NULL;

        if (!expect(*tz, TOK_EQ))
            return NULL;
        token eq = {0};
        next_token(tz, &eq);
        p_tree *rexpr = parse_expr(tz, 0, &l_arena);
        if (rexpr == NULL)
            return NULL;
        p_tree *as = arena_alloc(&l_arena, sizeof(p_tree));
        memset(as, 0, sizeof(p_tree));
        as->kind = TREE_ASSIGN;
        as->val = eq;
        as->nodes[0] = id;
        as->nodes[1] = rexpr;
        return as;
    }
    return NULL;
}

p_tree *parse_expr(tokenizer *tz, int min_b, arena *a) {
    p_tree *num1 = NULL;
    token pk = peek(*tz);
    if (pk.kind == TOK_END)
        return NULL;
    if ((num1 = parse_pexpr(tz, min_b, a)) == NULL)
        return NULL;

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
        if (num2 == NULL)
            return NULL;
        ct->nodes[0] = num1;
        ct->nodes[1] = num2;
        num1 = num2;
        ct = num2;
    }
    return root;
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);
    l_arena = arena_init(ARENA_CAP);
    assert(p_arena.ptr);

    assign("PI", M_PI, &gvar_table);
    assign("E", M_E, &gvar_table);

    srand(time(NULL));
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
        p_tree *t = parse_expr(&tz, 0, &p_arena);

        if (t == NULL) {
            printf("Syntax error!\n");
            arena_pop(&p_arena);
            continue;
        }

        printf("%f\n", eval(t, &gvar_table));
        arena_pop(&p_arena);
    }
    return 0;
}
