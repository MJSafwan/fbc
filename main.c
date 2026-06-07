#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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
    const char *buff;
    size_t len;
} tokenizer;

arena p_arena = {0};


int is_weak_op(char c) {
    return c == '+' || c == '-';
}

int is_strong_op(char c) {
    return c == '*' || c == '/' || c == '^';
}

int is_op(char c) {
    return is_strong_op(c) || is_weak_op(c);
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

typedef struct p_tree p_tree;

struct p_tree {
    token val;
    p_tree *l;
    p_tree *r;
};

const char *tok_names[] = {
    [TOK_INV] = "INV",
    [TOK_NUM] = "NUM",
    [TOK_OP]  = "OP",
    [TOK_LP]  = "LP",
    [TOK_RP]  = "RP",
    [TOK_END] = "EOF"
};

const char *tok_str(int kind) {
    if (kind >= sizeof(tok_names)/sizeof(char*))
        return NULL;
    return tok_names[kind];
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

token peek_token(tokenizer tkzer) {
    token t = {0};
    next_token(&tkzer, &t);
    return t;
}

p_tree *parse_expr(tokenizer *tz);

// T -> N | -N | (E) 
p_tree *parse_term(tokenizer *tz) {
    if (!expect(*tz, TOK_NUM)) {
        if (!expect(*tz, TOK_OP)) {
            if (!expect(*tz, TOK_LP))
                return NULL;
            skip(tz);
            p_tree *expr = parse_expr(tz);
            if (expr == NULL)
                return NULL;
            if (!expect(*tz, TOK_RP))
                return NULL;
            skip(tz);
            return expr;    
        }
        token op = peek_token(*tz);
        if (op.op_id != '-')
            return NULL;
        skip(tz);
        if (!expect(*tz, TOK_NUM))
            return NULL;

        token num = {0};
        next_token(tz, &num);

        p_tree *node = arena_alloc(&p_arena, sizeof(p_tree));
        p_tree *num_node = arena_alloc(&p_arena, sizeof(p_tree));
        memset(node, 0, sizeof(p_tree));
        memset(num_node, 0, sizeof(p_tree));
        num_node->val = num;
        node->val = op;
        node->l = num_node;
        return node;
    }
    token num = {0};
    next_token(tz, &num);
    p_tree *node = arena_alloc(&p_arena, sizeof(p_tree));
    memset(node, 0, sizeof(p_tree));
    node->val = num;
    return node;
}

// F -> T | T.*{[*/^]T}
p_tree *parse_factor(tokenizer *tz) {
    p_tree *num1 = parse_term(tz);
    if (num1 == NULL)
        return NULL;
    if (!expect(*tz, TOK_OP))
        return num1;
    p_tree *root = arena_alloc(&p_arena, sizeof(p_tree));
    p_tree *ct = root;
    memcpy(root, num1, sizeof(p_tree));
    for (;;) {
        if (!expect(*tz, TOK_OP)) {
            return root;
        }
        token tok_op = peek_token(*tz);
        if (is_strong_op(tok_op.op_id)) {
            skip(tz);
            p_tree *num2 = parse_term(tz);
            if (num2 == NULL)
                return NULL;

            ct->val = tok_op;
            ct->l = num1;
            ct->r = num2;
            ct = num2;

            p_tree *tmp = arena_alloc(&p_arena, sizeof(p_tree));
            memcpy(tmp, num2, sizeof(p_tree));
            num1 = tmp;
        } else {
            return root;
        }
    }
}

// E -> F|F.*{[+-]F}
p_tree *parse_expr(tokenizer *tz) {
    p_tree *num1 = parse_factor(tz);
    if (num1 == NULL)
        return NULL;
    if (!expect(*tz, TOK_OP)) {
        return num1;
    }
    p_tree *root = arena_alloc(&p_arena, sizeof(p_tree));
    p_tree *ct = root;
    memcpy(root, num1, sizeof(p_tree));
    for (;;) {
        if (!expect(*tz, TOK_OP))
            return root;
        token tok_op = peek_token(*tz);
        if (is_weak_op(tok_op.op_id)) {
            skip(tz);
            p_tree *num2 = parse_factor(tz);
            if (num2 == NULL) {
                return NULL;
            }

            ct->val = tok_op;
            ct->l = num1;
            ct->r = num2;
            ct = num2;

            p_tree *tmp = arena_alloc(&p_arena, sizeof(p_tree));
            memcpy(tmp, num2, sizeof(p_tree));
            num1 = tmp;
        } else {
            return root;
        }
    }
}

int eval(p_tree *root) {
    assert(root);
    if (root->l == NULL && root->r == NULL)
        return root->val.num;
    if (root->r == NULL)
       return -eval(root->l); 
    char op = root->val.op_id;
    switch (op) {
        case '+':
            return eval(root->l) + eval(root->r);
        case '-':
            return eval(root->l) - eval(root->r);
        case '*':
            return eval(root->l) * eval(root->r);
        case '/':
            int r = eval(root->r);
            if (r != 0)
                return eval(root->l) / r;
            return 0;      
        case '^':
            return pow(eval(root->l), eval(root->r));
        default:
            assert(1);
    }
    return 0;
}

int main(void) {
    p_arena = arena_init(ARENA_CAP);

    for (;;) {
        memset(p_arena.ptr, 0, p_arena.capacity);
        token *ts = arena_alloc(&p_arena, 64 * sizeof(token));

        char *buff = arena_alloc(&p_arena, BUFF_CAP);
        memset(buff, 0, BUFF_CAP);
        int bytes_read = read(0, buff, BUFF_CAP-1);
        buff[bytes_read] = 0;
        bytes_read++;
        tokenizer tz = {0};
        tz.buff = buff;
        tz.len = bytes_read;
        p_tree *expr = parse_expr(&tz);
        if (expr == NULL)
            printf("Syntax error!\n");
        printf("%d\n", eval(expr));
        arena_pop(&p_arena);
    }
    return 0;
}
