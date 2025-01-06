#ifndef TRACE_FORMAT_H
#define TRACE_FORMAT_H

#include <stddef.h>
#include <stdint.h>

void append_u32(char **p, size_t *remaining, uint32_t v);
void append_u16(char **p, size_t *remaining, uint16_t v);
void append_str(char **p, size_t *remaining, const char *s);
void append_char(char **p, size_t *remaining, char c);
void append_i16(char **p, size_t *remaining, int16_t v);
void append_i32(char **p, size_t *remaining, int32_t v);
void append_hex_u8(char **p, size_t *remaining, uint8_t v);
void append_hex_u16(char **p, size_t *remaining, uint16_t v);
void append_hex_u32(char **p, size_t *remaining, uint32_t v);

#endif /* TRACE_FORMAT_H */
