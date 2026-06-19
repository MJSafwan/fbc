#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "xassert.h"

#define STACK_ALIGNMENT 16


#ifndef ARENA_H_
#define ARENA_H_

typedef struct {
    void    *ptr;
    uint64_t offset;
    uint64_t mark;
    uint64_t capacity;
    int ref;
} arena;

arena arena_init(uint64_t capacity);
int arena_ref(arena *s);
int arena_unref(arena *s);
void *arena_alloc(arena *s, uint64_t size);
void arena_set_frame(arena *s);
void arena_pop(arena *s);
void arena_destroy(arena *s);

#ifdef ARENA_IMPLEMENTATION

arena arena_init(uint64_t capacity) {
    arena s = {
        .ptr = malloc(capacity),
        .capacity = capacity,
        .mark = 0,
        .offset = 0,
    };

    xassert(s.ptr, "Cannot allocate memory of size %llu for arena!\n", capacity);

    return s;
}

void *arena_alloc(arena *s, uint64_t size) {
    xassert(size, "Trying to allocate memory of size zero!\n"); 

    size += size % STACK_ALIGNMENT == 0 ? 0 : (STACK_ALIGNMENT - size % STACK_ALIGNMENT);
    xassert(s->offset+size <= s->capacity, "Allocation of %llu bytes is out of the bounds of arena %p!\n", size, s);   
    uint64_t offset = s->offset;
    s->offset += size;
    return (char *)(s->ptr) + offset;
}

void arena_set_frame(arena *s) {
    uint64_t offset = s->offset;
    uint64_t *old_fp = arena_alloc(s, sizeof(uint64_t));
    old_fp[0] = s->mark;
    s->mark = offset;
}

void arena_pop(arena *s) {
    s->offset = s->mark;
    if (s->offset == 0) return;
    uint64_t old_fp = ((uint64_t *) ((char*)s->ptr + s->offset))[0];
    s->mark = old_fp;
}

int arena_ref(arena *s) {
    return s->ref++;
}
int arena_unref(arena *s) {
    if (--s->ref <= 0) {
        arena_destroy(s);
    }
    return s->ref;
}

void arena_destroy(arena *s) {
    free(s->ptr);
}

#endif // ARENA_IMPLEMENTATION
#endif // ARENA_H_
