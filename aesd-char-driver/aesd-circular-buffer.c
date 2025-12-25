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
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

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
    /**
    * TODO: implement per description
    */
    
    
    // start with first entry at out_offs 
    int out = buffer->out_offs;
    
    // return pointer
    struct aesd_buffer_entry* rEntry = NULL;   
        
    // calculate offset   
    size_t poff = 0;
     
    // get the number of entrys in buffer     
    int nEntries = 0;
     
    // if full -> all entries 
    if ( buffer->full )
    {
        // return # of buffer
        nEntries =  AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    else {
        // calculate # of entrys     
        nEntries = (buffer->in_offs - buffer->out_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
          
        // alternative calculation
        // if ( buffer->in_offs > buffer->out_offs )
        //     n = buffer->in_offs - buffer->out_offs;
        // else if (buffer->in_offs < buffer->out_offs )
        //     n = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
     
     }
      
     // get entry according offset     
     for ( int i = 0; i< nEntries; i++ ) {
     
         // is offset > (previous entry sizes + entry size)
         if ( (poff + buffer->entry[out].size) <= char_offset )  {
             // add byte offset
             poff += buffer->entry[out].size;
        }
        else {
            // store relative offset
            *entry_offset_byte_rtn = char_offset - poff;
            
            // get pointer to return
            rEntry = &buffer->entry[out];

            break;
        }

        // step to next entry
        out = (out +1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        
    }

    return rEntry;
    //return NULL;
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
    /**
    * TODO: implement per description
    */
        
    // Copy new entry at in_offs   
    buffer->entry[buffer->in_offs] = *add_entry;
    
    // Move in_offs pointer to next entry
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // if buffer-full move out_offs pointer
    if ( buffer->full ) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    else {
        // Is buffer full
        if ( buffer->in_offs == buffer->out_offs ) {
            buffer->full = true;
        }
    }
    
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
    
    // init in_offs, out_offs pointers
    buffer->in_offs = 0;
    buffer->out_offs = 0;
  
    // buffer empty
    buffer->full = false;
    
}


