#include "utils.h"
#include "include/taguchi.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Fatal: malloc failed\\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (ptr == NULL) {
        fprintf(stderr, "Fatal: calloc failed\\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        fprintf(stderr, "Fatal: realloc failed\\n");
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

void set_error(char *error_buf, const char *fmt, ...) {
    if (error_buf == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_buf, TAGUCHI_ERROR_SIZE, fmt, args);
    va_end(args);
}

