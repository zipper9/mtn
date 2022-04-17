#ifndef STRING_BUFFER_H_
#define STRING_BUFFER_H_

struct string_buffer
{
    char *s;
    int len;
    int capacity;
};

void sb_init(struct string_buffer *sb);
int sb_add_string_len(struct string_buffer *sb, const char *s, int len);
int sb_add_string(struct string_buffer *sb, const char *s);
int sb_add_char(struct string_buffer *sb, char c);
int sb_add_buffer(struct string_buffer *sb, const struct string_buffer *sb2);
void sb_clear(struct string_buffer *sb);
void sb_destroy(struct string_buffer *sb);

#endif // STRING_BUFFER_H_
