/**
 * @file string.h
 * @brief Minimal string.h for freestanding bare-metal builds
 *
 * Declares common memory/string functions that the compiler provides
 * as builtins when building with -ffreestanding.
 */

#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include <stddef.h>

/* Memory functions - compiler provides builtins */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* String functions */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

#endif /* _LIBC_STRING_H */
