#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

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

#define SEEK_CHAR 0
#define READ_ID 1

typedef struct {
    int kind;
    union {
        int num;
        char op_id;
    };
} token;

typedef struct {
    size_t cursor;
    int state;
    char *buff;
    size_t len;
} tokenizer;

arena p_arena = {0};

int is_op(char c) {
    return c == '+' || c == '*';
}

int is_num(char c) {
    return c >= '0' && c <= '9';
}

int next_token(tokenizer *tk, token *out) {
    tk->state = SEEK_CHAR;
    
    char *s = NULL;
    int offset = 0;
    for (;;) {
        if (tk->cursor >= tk->len) {
            out->kind = TOK_END;
            return TOK_END; 
        }

        if (tk->state == SEEK_CHAR) {
            char c = tk->buff[tk->cursor];
            if (isspace(c) == 0) {
                if (is_num(c) == 1) {
                    arena_set_frame(&p_arena);
                    s = arena_alloc(&p_arena, tk->len);
                    strncpy(s, tk->buff, tk->len);
                    tk->state = READ_ID;
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
            }
        }

        if (tk->state == READ_ID) {
            out->kind = TOK_NUM;
            char c = tk->buff[tk->cursor];
            if (!is_num(c)) {
                s[tk->cursor] = 0;
                out->num = atoi(&s[offset]);
                out->kind = TOK_NUM;
                arena_pop(&p_arena);
                return TOK_NUM;
            }
        }

        tk->cursor++;
    }
}

void get_tokens(token *ts) {
    char *buff = arena_alloc(&p_arena, BUFF_CAP);
    memset(buff, 0, BUFF_CAP);
    int bytes_read = read(0, buff, BUFF_CAP-1);
    buff[bytes_read] = 0;
    bytes_read++;
    tokenizer tz = {0};
    tz.buff = buff;
    tz.len = bytes_read;
    token t = {0};
    size_t ts_count = 0;

    while (next_token(&tz, &t) != TOK_END) {
        if (ts_count >= 256)
           break;
       ts[ts_count++] = t; 
    }

    ts[ts_count] = t; 
}

typedef struct p_tree p_tree;

struct p_tree {
    union {
        int val;
        char op;
    };
    p_tree *l;
    p_tree *r;
    p_tree *parent;
};

int expect(token t, int tok) {
    return t.kind == tok;
}

int make_tree(p_tree *root, token *ts) {
    int scope_max = 32;
    p_tree **stack = arena_alloc(&p_arena, sizeof(p_tree*) * scope_max);
    for (size_t i = 1; i < scope_max; ++i) {
        stack[i] = arena_alloc(&p_arena, sizeof(p_tree));
        assert(stack[i]);
    }
    stack[0] = root;
    p_tree *ct = root;
    int scope = 0;
    for (size_t i = 0; ts[i].kind != TOK_END; ++i) {
        token t = ts[i];
        if (t.kind == TOK_NUM) {
            p_tree *num = arena_alloc(&p_arena, sizeof(p_tree));
            num->val = t.num;
            num->parent = ct;
            ct->l = num;
        }
        if (!expect(t, TOK_NUM) && !expect(t, TOK_LP) && i == 0) {
            fprintf(stderr, "Invalid syntax: expected a number or expression at the beginning of the line\n");
            return -1;
        }   
        if (t.kind == TOK_OP) {
            token next = ts[i+1];
            if (!expect(next, TOK_NUM) && !expect(next, TOK_LP)) {
                fprintf(stderr, "Invalid syntax: Expected a number or expression after operator\n");
                return -1;
            }
            if (i > 0) {
                if (!expect(ts[i-1], TOK_NUM) && !expect(ts[i-1], TOK_RP)) {
                    fprintf(stderr, "Invalid syntax: Expected a number or expression before the operator\n");
                    return -1;
                } 
            }
            p_tree *tr = arena_alloc(&p_arena, sizeof(p_tree));
            memset(tr, 0, sizeof(p_tree));
            assert(tr);
            tr->op = t.op_id;
            ct->r = tr;
            tr->parent = ct;
            ct = tr;
        }

        if (t.kind == TOK_LP) {
            if (scope >= scope_max) {
                fprintf(stderr, "Maximum nesting reached.\n");
                return -1;
            }
            stack[scope] = ct;
            scope++;
            memset(stack[scope], 0, sizeof(p_tree));
            ct = stack[scope];
        }
        if (t.kind == TOK_RP) {
            if (scope == 0) {
                fprintf(stderr, "Invalid syntax: unballanced parentheses\n");
                return -1;
            }
            scope--;
            p_tree *st = arena_alloc(&p_arena, sizeof(p_tree));
            memcpy(st, stack[scope+1], sizeof(p_tree));
            if (st->l != NULL)
                st->l->parent = st;
            if (st->r != NULL)
                st->r->parent = st;
            stack[scope]->l = st;
            ct = stack[scope];
        }
    }
    if (scope != 0) {
        fprintf(stderr, "Invalid syntax: unballanced parentheses\n");
        return -1;
    }
    return 0;
}

int eval(p_tree *root);

void eval_mult(p_tree *tr) {
    if (tr->parent == NULL) {
        return;
    }
    p_tree *parent = tr->parent;
    if (tr->op == '*') {
        tr->parent->l->val = eval(tr->parent->l) * eval(tr->l); 
        tr->parent->r = tr->r;
        tr->parent->l->l = NULL;
        tr->parent->l->r = NULL;
        if (tr->r != NULL) {
            tr->parent->r->parent = parent;
        }
    }
    eval_mult(parent);
}

void eval_add(p_tree *tr) {
    if (tr->parent == NULL) {
        return;
    }
    p_tree *parent = tr->parent;
    if (tr->op == '+') {
        tr->parent->l->val = eval(tr->parent->l) + eval(tr->l); 
        tr->parent->r = tr->r;
        tr->parent->l->l = NULL;
        tr->parent->l->r = NULL;
        if (tr->r != NULL) {
            tr->parent->r->parent = parent;
        }
    }

    eval_add(parent);
}

int eval(p_tree *root) {
    if (root->l == NULL)
        return root->val;

    p_tree *t = root;
    while (t->r != NULL) {
        t = t->r;
    }

    eval_mult(t);
    t = root;
    while (t->r != NULL) {
        t = t->r;
    }
    eval_add(t);
    assert(root->r == NULL);
    return root->l->val;
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);
    for (;;) {
        memset(p_arena.ptr, 0, p_arena.capacity);
        token *ts = arena_alloc(&p_arena, 64 * sizeof(token));
        get_tokens(ts);
        
        p_tree *root = arena_alloc(&p_arena, sizeof(p_tree));
        memset(root, 0, sizeof(p_tree));
        
        if (make_tree(root, ts) < 0)
            continue;
        int val = eval(root);
        printf("%d\n", val);
        arena_pop(&p_arena);
    }
    return 0;
}
