#include "trace_format.h"

void append_u32(char **p, size_t *remaining, uint32_t v)
{
    char tmp[12];
    int i = 0;
    if (v == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (v && i < (int)sizeof(tmp))
        {
            tmp[i++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (i-- > 0 && *remaining)
    {
        *(*p)++ = tmp[i];
        (*remaining)--;
    }
}

void append_u16(char **p, size_t *remaining, uint16_t v)
{
    append_u32(p, remaining, (uint32_t)v);
}

void append_str(char **p, size_t *remaining, const char *s)
{
    if (!s)
        return;
    while (*s && *remaining)
    {
        *(*p)++ = *s++;
        (*remaining)--;
    }
}

void append_char(char **p, size_t *remaining, char c)
{
    if (*remaining)
    {
        *(*p)++ = c;
        (*remaining)--;
    }
}

void append_i16(char **p, size_t *remaining, int16_t v)
{
    if (v < 0)
    {
        append_char(p, remaining, '-');
        append_u16(p, remaining, (uint16_t)(-v));
    }
    else
    {
        append_u16(p, remaining, (uint16_t)v);
    }
}

void append_i32(char **p, size_t *remaining, int32_t v)
{
    uint32_t mag;
    if (v < 0)
    {
        append_char(p, remaining, '-');
        mag = (uint32_t)(-(int64_t)v);
    }
    else
    {
        mag = (uint32_t)v;
    }
    append_u32(p, remaining, mag);
}

static void append_hex_nibble(char **p, size_t *remaining, uint8_t v)
{
    static const char hex[] = "0123456789abcdef";
    if (!*remaining)
        return;
    *(*p)++ = hex[v & 0x0Fu];
    (*remaining)--;
}

void append_hex_u8(char **p, size_t *remaining, uint8_t v)
{
    append_hex_nibble(p, remaining, (uint8_t)(v >> 4));
    append_hex_nibble(p, remaining, v);
}

void append_hex_u16(char **p, size_t *remaining, uint16_t v)
{
    append_hex_nibble(p, remaining, (uint8_t)(v >> 12));
    append_hex_nibble(p, remaining, (uint8_t)(v >> 8));
    append_hex_nibble(p, remaining, (uint8_t)(v >> 4));
    append_hex_nibble(p, remaining, (uint8_t)v);
}

void append_hex_u32(char **p, size_t *remaining, uint32_t v)
{
    for (int shift = 28; shift >= 0; shift -= 4)
        append_hex_nibble(p, remaining, (uint8_t)((v >> shift) & 0x0Fu));
}
