/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#include <stdlib.h>
#endif

#include "aesd-circular-buffer.h"

#define buff_begin_index(buff) buff->out_offs
#define buff_end_index(buff) buff->in_offs

#define buff_entry_at(buff, index) buff->entry[index]
#define buff_begin_entry(buff) buff_entry_at(buff, buff_begin_index(buff))
#define buff_end_entry(buff) buff_entry_at(buff, buff_end_index(buff))

#define buff_index_cycle_increase(index) \
    index++; \
    if(index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) \
    { \
        index = 0; \
    }

#define buff_index_cycle_decrease(index) \
    index--; \
    if(index < 0 ) \
    { \
        index = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1; \
    }

#define increase_begin_buff_index(buff) \
    buff_index_cycle_increase(buff_begin_index(buff)); \

#define increase_end_buff_index(buff) \
    buff_end_index(buff)++; \
    if(buff_end_index(buff) >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) \
    { \
        buff_end_index(buff) = 0; \
        buff->full = true; \
    }

//# cell
//$ end
//@ begin
//# # # #

//@ # # $ 3 - 0 = 3 increase
//$ @ # # 0 - 1 = -1 increase
//# # $ @ 2 - 3 = -1 increase

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    int iterator = buff_begin_index(buffer);

    int accumulated_offset = 0;

    int i = 0;

    while(i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        if(buff_entry_at(buffer, iterator).buffptr == NULL)
        {
            return NULL;
        }

        if(char_offset >= accumulated_offset && char_offset < (accumulated_offset + buff_entry_at(buffer, iterator).size))
        {
            *entry_offset_byte_rtn = char_offset - accumulated_offset;
            return &(buff_entry_at(buffer, iterator));
        }

        accumulated_offset += buff_entry_at(buffer, iterator).size;

        buff_index_cycle_increase(iterator);

        i++;
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    if(buffer == NULL || add_entry == NULL)
    {
        return;
    }

    if(buffer->empty)
    {
        buffer->empty = false;
    }
    else
    {
        increase_end_buff_index(buffer);

        if(buffer->full)
        {
            increase_begin_buff_index(buffer);
        }
    }

    if(buff_end_entry(buffer).buffptr == NULL)
    {
        buff_end_entry(buffer).buffptr = 
#ifdef __KERNEL__
        (const char*)kmalloc(add_entry->size, GFP_KERNEL);
#else
        (const char*)malloc(add_entry->size);
#endif        
        buff_end_entry(buffer).allocated = add_entry->size;
    }
    else if(buff_end_entry(buffer).allocated < add_entry->size)
    {
        buff_end_entry(buffer).buffptr = 
#ifdef __KERNEL__
        (const char*)krealloc((char*)buff_end_entry(buffer).buffptr, add_entry->size, GFP_KERNEL);
#else        
        (const char*)realloc((char*)buff_end_entry(buffer).buffptr, add_entry->size);
#endif        
        buff_end_entry(buffer).allocated = add_entry->size;
    }

    memcpy((char*)buff_end_entry(buffer).buffptr, add_entry->buffptr, add_entry->size);
    buff_end_entry(buffer).size = add_entry->size;

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
    buffer->empty = true;
}
