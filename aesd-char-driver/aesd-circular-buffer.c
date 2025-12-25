#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

struct aesd_buffer_entry *
aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer,
    size_t char_offset,
    size_t *entry_offset_byte_rtn)
{
    size_t cumulative_size = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count;

    if (buffer->full) {
        count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else if (buffer->in_offs >= buffer->out_offs) {
        count = buffer->in_offs - buffer->out_offs;
    } else {
        count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
              - buffer->out_offs + buffer->in_offs;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (char_offset < cumulative_size + buffer->entry[index].size) {
            *entry_offset_byte_rtn = char_offset - cumulative_size;
            return &buffer->entry[index];
        }

        cumulative_size += buffer->entry[index].size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    return NULL;
}

void aesd_circular_buffer_add_entry(
    struct aesd_circular_buffer *buffer,
    const struct aesd_buffer_entry *add_entry)
{
    if (buffer->full) {
        buffer->out_offs =
            (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    buffer->entry[buffer->in_offs] = *add_entry;

    buffer->in_offs =
        (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }
}

void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}

