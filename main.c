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
                return TOK_NUM;
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

int leval(char lop, int num) {
    switch (lop) {
        case '-':
            return -num;
        case '~':
            return ~num;
        default:
            return 0;
    }
}

int eval(char op, int num1, int num2) {
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
            return 0;
        case '^':
            return pow(num1, num2);
        case '%':
            if (num2 > 0)
                return num1 % num2;
            return 0;
        default:
            return 0;
    }
}

int parse_expr(tokenizer *tz, int min_b, int *ret);

int parse_pexpr(tokenizer *tz, int min_b, int *ret) {
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
    }
    return -1;
}

int parse_expr(tokenizer *tz, int min_b, int *ret) {
    int num1;
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
        int num2;
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
        int res;
        int err = parse_expr(&tz, 0, &res);
        if (err < 0) {
            printf("Syntax error!\n");
            arena_pop(&p_arena);
            continue;
        }
        printf("%d\n", res);
        arena_pop(&p_arena);
    }
    return 0;
}
