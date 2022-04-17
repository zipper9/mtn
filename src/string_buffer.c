#include "string_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define INITIAL_SIZE 400

int sb_add_string_len(struct string_buffer *sb, const char *s, int len)
{
    int size = sb->len + len + 1;
    if (size > sb->capacity)
    {
        int new_capacity = sb->capacity << 1;
        if (new_capacity < size)
            new_capacity = size;
        if (new_capacity < INITIAL_SIZE)
            new_capacity = INITIAL_SIZE;
        char *new_buf = (char *) realloc(sb->s, new_capacity);
        if (!new_buf)
            return -1;
        sb->s = new_buf;
        sb->capacity = new_capacity;
    }
    memcpy(sb->s + sb->len, s, len);
    sb->len += len;
    sb->s[sb->len] = 0;
    return 0;
}

int sb_add_string(struct string_buffer *sb, const char *s)
{
    return s ? sb_add_string_len(sb, s, strlen(s)) : 0;
}

int sb_add_char(struct string_buffer *sb, char c)
{
    return sb_add_string_len(sb, &c, 1);
}

int sb_add_buffer(struct string_buffer *sb, const struct string_buffer *sb2)
{
    return sb_add_string_len(sb, sb2->s, sb2->len);
}

void sb_clear(struct string_buffer *sb)
{
    if (sb->len)
    {
        assert(sb->s);
        sb->s[0] = 0;
        sb->len = 0;
    }
}

void sb_shrink(struct string_buffer *sb, int size)
{
    assert(size >= 0);
    if (size < sb->len)
    {
        assert(sb->s);
        sb->s[size] = 0;
        sb->len = size;
    }
}

void sb_init(struct string_buffer *sb)
{
    memset(sb, 0, sizeof(*sb));
}

void sb_destroy(struct string_buffer *sb)
{
    free(sb->s);
    memset(sb, 0, sizeof(*sb));
}
