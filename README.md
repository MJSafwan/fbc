# Introduction

This is a functional basic calculator implemented as a single header library.

Note: This was made for educational and recreational purposes. Please do not use this in any serious project.

# Usage

To use this library, include "fbc.h" and define FBC_IMPLEMENTATION. For automatic error reporting, also define FBC_PRINT_SERR.

Then, you must call fbc_init to initialize the library. This function has the following signiture:
```C
void fbc_init(void);
```

After that, you can call the fbc_line function and supply the line to be interpreted. This function has the following signiture:
```C
eval_type fbc_line(char *line);
```

The eval_type return type is defined as follows:

```C
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
```

The 'type' field is roughly this enum:
```C
enum type {
    T_NULL,
    T_NUM,
    T_LAMBDA
};
```

T_NULL is a null return type (though, this does not mean that an error occured. It just means that the evaluated line did not return anything)

T_NUM, which is a number with 'num' and 'perc' fields avalable for the number and its percision respectively.

T_LAMBDA is an expressin with free variables that is yet to be evaluated. More on this type later.

You can chech errors by calling fbc_did_error, which returns non-zero if an error happened. Then, to get the error message, you can call the fbc_get_error, which returns a temporary string owned by the library containing the error message. However, the fbc_get_error will return NULL if the macro FBC_PRINT_SERR is defined, in which case an error will automaticly be printed.

# Build the REPL

Make sure you have a C compiler. Then, run

```
make
```

This will build the REPL.

# Implementation Details

You can use this like any normal calculator. Lines like this:
```
5 + 2 * 4.132 + 10 / 5 + (2+4-10)^2
```
Will be evaluated to 31.264.

You can assign variables like so:
```
x = 10
```
Where 'x' can be any string of characters starting with a letter and followed by any alphanumeric character or an underbar, and '10' can be any expression that will be eagrly evaluated and assigned to 'x'.

You then can use this variable in any expression and it will be evaluated accordingly.

Now we must explore the notion of lambdas. A lambda is an expression with a free variable. That is, a variable that is not defined or assigned to any value.

For example, the expression
```
z + y * 5
``` 
is a lambda, since neither z nor y are defined. You can 'call' these lambdas, whuch will assign temporary, sandboxed bindings to the variables in the expressions.
For example, the expression
```
x + 10
``` 
May be called like so:
```
(x+10)(x=5)
```
This will assign a temporary binding to the variable x, that is 5, and then evaluate 'x+10' as if x were 5, even if x was something different. The original value of x, if it exists, will be saved and replaced with 5, then it will return to its original value once the expression has been evaluated.

However, you can only call expressions with one binding at a time. So, you must chain your calls.

For example, in the case of 
```
z + y * 5
```

You can say:
```
(z + y * 5)(z = 2.5)(y=5)
```

This will bind y to 5 first, then bind z to 2.5, then evaluate the expression.

In fact, any expression is callable!

You can also 'bind' symbols. The notation for this is like so:
```
y : x*10
```

Here, the right hand side is not evaluated eagrly, but is evaluated lazaly. Once you invoke y like so:
```
y
```
Though, you can also call this expression, since this will, in a semantic sense, expand to 'x*10', so you can say
```
y(x=10)
```

# License

This is licensed under MIT.
