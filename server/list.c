
#include "list.h"

#include <pthread.h>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

typedef enum 
{
    LIST_DATA_TYPE_UNKNOWN_INTERNAL = 0,
    LIST_DATA_TYPE_BYTE_INTERNAL = 1,
    LIST_DATA_TYPE_INTEGER_INTERNAL = 1 << 1,
    LIST_DATA_TYPE_POINTER_INTERNAL = 1 << 2,
    LIST_DATA_TYPE_SHORT_STRING_INTERNAL = 1 << 3,
    LIST_DATA_TYPE_ALLOCATED_STRING_INTERNAL = 1 << 4,
    LIST_DATA_TYPE_EMPLACED_OPTIMIZED_CUSTOM_STRUCT_INTERAL = 1 << 5,
    LIST_DATA_TYPE_EMPLACED_ALLOCATED_CUSTOM_STRUCT_INTERNAL = 1 << 6,
    LIST_DATA_TYPE_COPIED_OPTIMIZED_CUSTOM_STRUCT_INTERNAL = 1 << 7,
    LIST_DATA_TYPE_COPIED_ALLOCATED_CUSTOM_STRRUCT_INTERNAL = 1 << 8,

}item_internal_data_type_t;


typedef union
{
    char string[sizeof(void*)];
    void* pointer;
    int digit;
    char byte;

}list_item_data;


struct list_item_struct
{
    list_item_t* next;
    list_item_t* prev;
    item_internal_data_type_t type;
    list_item_data data;
};

struct list_struct
{
    list_item_t* first;
    list_item_t* last;
    int size;
};

struct safelist_struct
{
    list_t list;
    pthread_mutex_t lock;
};

int list_create(list_t** list)
{
    if(*list != NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    (*list)= (list_t*) calloc(1, sizeof(list_t));

    if((*list) == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    return LIST_SUCCESS;
}

int add_list_item(list_t* list, list_item_t** item)
{
    if (list == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(item == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    list_item_t* tmpitem = (list_item_t*)calloc(1, sizeof(list_item_t));

    if( tmpitem == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    tmpitem->type = LIST_DATA_TYPE_UNKNOWN_INTERNAL;

    if(list->first == NULL)
    {
        list->first = tmpitem;
        list->last = tmpitem;
        list->size = 1;
    }
    else
    {
        tmpitem->prev = list->last;
        list->last->next = tmpitem;
        list->last = tmpitem;
        list->size += 1;
    }

    *item = tmpitem;

    return LIST_SUCCESS;
}

/*
int list_push_back(list_t* plist, data_type_t type, ...)
{
    int result = UNKNOWN_DATA_TYPE;

    va_list args;
    va_start(args, 1);

    switch (type)
    {
    case LIST_DATA_TYPE_BYTE:
        //WTF. What is wrong with this line?
        result = list_push_back_byte(plist, va_arg(args, char));
        break;
    case INTEGER:
        result = list_push_back_digit(plist, va_arg(args, int));
        break;
    case POINTER:    
        result = list_push_back_pointer(plist, va_arg(args, void*));
        break;
    case STRING:
        result = list_push_back_string(plist, va_arg(args, char*), va_arg(args, int));
        break;
    case LIST_DATA_TYPE_UNKNOWN:
    default:
        break;    
    }

    va_end(args);

    return result;
}

*/

int list_push_back_byte(list_t* plist, char val)
{
    list_item_t* item = NULL;
    int status = add_list_item(plist, &item);

    if(status != LIST_SUCCESS)
    {
        return status;
    }

    item->type = LIST_DATA_TYPE_BYTE_INTERNAL;
    item->data.byte = val;

    return LIST_SUCCESS;
}

int list_push_back_digit(list_t* plist, int val)
{
    list_item_t* item = NULL;
    int status = add_list_item(plist, &item);

    if(status != LIST_SUCCESS)
    {
        return status;
    }

    item->type = LIST_DATA_TYPE_INTEGER_INTERNAL;
    item->data.digit = val;

    return LIST_SUCCESS;
}

int list_push_back_pointer(list_t* plist, void* val)
{
    list_item_t* item = NULL;
    int status = add_list_item(plist, &item);

    if(status != LIST_SUCCESS)
    {
        return status;
    }

    item->type = LIST_DATA_TYPE_POINTER_INTERNAL;
    item->data.pointer = val;

    return LIST_SUCCESS;
}

int list_push_back_string(list_t* plist,const char* string, int len)
{
    list_item_t* pitem = NULL;
    int status = LIST_SUCCESS;

    if((status = add_list_item(plist, &pitem)) != LIST_SUCCESS || 
       (status = list_item_set_string(pitem, string, len) != LIST_SUCCESS))
    {
        return status;
    }
    
    return LIST_SUCCESS;
}


int list_item_get_byte(list_item_t* pitem, char* val)
{
    if(pitem == NULL || val == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(pitem->type != LIST_DATA_TYPE_BYTE_INTERNAL)
    {
        return LIST_WRONG_DATA_TYPE;
    }

    *val = pitem->data.byte;

    return LIST_SUCCESS;
}


int list_item_get_digit(list_item_t* pitem, int* val)
{
    if(pitem == NULL || val == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(pitem->type != LIST_DATA_TYPE_INTEGER_INTERNAL)
    {
        return LIST_WRONG_DATA_TYPE;
    }

    *val = pitem->data.byte;

    return LIST_SUCCESS;
}

int list_item_get_pointer(list_item_t* pitem, void** val)
{
    if(pitem == NULL || val == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(pitem->type != LIST_DATA_TYPE_POINTER_INTERNAL)
    {
        return LIST_WRONG_DATA_TYPE;
    }

    *val = pitem->data.pointer;

    return LIST_SUCCESS;
}

int list_item_get_string(list_item_t* pitem, char** val)
{
    if(pitem == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(val == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    if(pitem->type == LIST_DATA_TYPE_SHORT_STRING_INTERNAL)
    {
        *val = pitem->data.string;
    }
    else if(pitem->type == LIST_DATA_TYPE_ALLOCATED_STRING_INTERNAL)
    {
        *val = pitem->data.pointer;
    }
    else
    {
        return LIST_WRONG_DATA_TYPE;
    }

    return LIST_SUCCESS;
}


data_type_t list_get_item_data_type(list_item_t* pitem)
{
    if( pitem == NULL)
    {
        return LIST_DATA_TYPE_UNKNOWN_INTERNAL;
    }

    switch (pitem->type)
    {
    case LIST_DATA_TYPE_BYTE_INTERNAL:
        return LIST_DATA_TYPE_BYTE;
    case LIST_DATA_TYPE_INTEGER_INTERNAL:
        return LIST_DATA_TYPE_INTEGER;
    case LIST_DATA_TYPE_POINTER_INTERNAL:
        return LIST_DATA_TYPE_POINTER;        
    case LIST_DATA_TYPE_SHORT_STRING_INTERNAL:
    case LIST_DATA_TYPE_ALLOCATED_STRING_INTERNAL:
        return LIST_DATA_TYPE_STRING;    
    case LIST_DATA_TYPE_EMPLACED_ALLOCATED_CUSTOM_STRUCT_INTERNAL:
    case LIST_DATA_TYPE_EMPLACED_OPTIMIZED_CUSTOM_STRUCT_INTERAL:
    case LIST_DATA_TYPE_COPIED_ALLOCATED_CUSTOM_STRRUCT_INTERNAL:
    case LIST_DATA_TYPE_COPIED_OPTIMIZED_CUSTOM_STRUCT_INTERNAL:
        return LIST_DATA_TYPE_EMPLACED_CUSTOM_STRUCT;
    case LIST_DATA_TYPE_UNKNOWN_INTERNAL:
    default:
        return LIST_DATA_TYPE_UNKNOWN;
    }
}

int list_remove_item(list_t* list, list_item_t* item)
{
    if(list == NULL || item == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(item->next != NULL)
    {
        item->next->prev = item->prev;
    }
    
    if(item->prev != NULL)
    {
        item->prev->next = item->next;
    }

    if(item->type == LIST_DATA_TYPE_ALLOCATED_STRING_INTERNAL ||
       item->type == LIST_DATA_TYPE_EMPLACED_ALLOCATED_CUSTOM_STRUCT_INTERNAL ||
       item->type == LIST_DATA_TYPE_COPIED_ALLOCATED_CUSTOM_STRRUCT_INTERNAL)
    {
        free(item->data.pointer);
    }

    list->size -= 1;

    if(item == list->first)
    {
        list->first = item->next;

        if(list->size < 2)
        {
            list->last = list->first;
        }
    }
    else if(item == list->last)
    {
        list->last = item->prev;

        if(list->size < 2)
        {
            list->first = list->last;
        }
    }

    free(item);

    return LIST_SUCCESS;
}

list_item_t* list_begin(list_t* list)
{
    if(list == NULL)
        return NULL;

    return list->first;
}

list_item_t* list_end(list_t* list)
{
    if(list == NULL)
        return NULL;

    return list->last;
}

list_item_t* list_get_next(list_item_t* item)
{
    if(item == NULL)
        return NULL;

    return item->next;
}

list_item_t* list_get_previous(list_item_t* item)
{
    if(item == NULL)
        return NULL;

    return item->prev;
}

int list_size(list_t* list)
{
    if(list == NULL)
        return LIST_INVALID_POINTER;

    return list->size;
}


int list_clear(list_t* list)
{
    if(list == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(list->size == 0)
    {
        return LIST_SUCCESS;
    }

    list_item_t* iterator = list_end(list);

    int status = LIST_SUCCESS;

    while(iterator != NULL)
    {
        list_item_t* previous = list_get_previous(iterator);
        status = list_remove_item(list, iterator);

        if(status != LIST_SUCCESS)
        {
            return status;
        }

        iterator = previous;
    }

    return LIST_SUCCESS;
}

int list_delete(list_t** list)
{
    if(list == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    if(*list == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    int status = list_clear(*list);

    if(status != LIST_SUCCESS)
    {
        return status;
    }

    free(*list);

    *list = NULL;

    return LIST_SUCCESS;
}

int list_emplace_back(list_t* plist, int size, construct_func func, void* arg)
{
    list_item_t* pitem = NULL;

    int result = LIST_SUCCESS;
    if( (result = add_list_item(plist, &pitem)) != LIST_SUCCESS)
    {
        return result;
    }

    if( (result = list_item_emplace_struct(pitem, size, func, arg)) != LIST_SUCCESS)
    {
        list_remove_item(plist, pitem);
        return result;
    }

    return LIST_SUCCESS;
}

int list_push_back(list_t* plist, void* pstruct, int size)
{
    list_item_t* pitem  = NULL;
    int status = LIST_SUCCESS;

    if((status = add_list_item(plist, &pitem)) != LIST_SUCCESS)
    {
        return status;
    }

    if( (status = list_item_set_struct(pitem, pstruct, size)) != LIST_SUCCESS)
    {
        list_remove_item(plist, pitem);
        return status;
    }

    return LIST_SUCCESS;
}

int list_item_get_custom_struct(list_item_t* pitem, void** val)
{
    if(val == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    if( pitem == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    switch (pitem->type)
    {
    case LIST_DATA_TYPE_EMPLACED_ALLOCATED_CUSTOM_STRUCT_INTERNAL:
    case LIST_DATA_TYPE_COPIED_ALLOCATED_CUSTOM_STRRUCT_INTERNAL:
        *val = pitem->data.pointer;
        break;
    case LIST_DATA_TYPE_EMPLACED_OPTIMIZED_CUSTOM_STRUCT_INTERAL:
    case LIST_DATA_TYPE_COPIED_OPTIMIZED_CUSTOM_STRUCT_INTERNAL:
        *val = &pitem->data;
        break;    
    default:
        return LIST_WRONG_DATA_TYPE;
    }

    return LIST_SUCCESS;
}

int list_for_each(list_t *plist, each_func func, void* data)
{
    if(plist == NULL || func == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    int result = LIST_SUCCESS;

    for(list_item_t* iterator = list_begin(plist); iterator != NULL; iterator = list_get_next(iterator))
    {
        result = func(iterator, data);
        if(result != LIST_SUCCESS)
        {
            return result;
        }
    }

    return LIST_SUCCESS;
}

int list_item_create(list_item_t** item)
{
    if(item == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    list_item_t* pitem = (list_item_t*)calloc(1, sizeof(list_item_t));

    if(pitem == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    *item = pitem;

    return LIST_SUCCESS;
}

int list_item_delete(list_item_t** item)
{
    if(item == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    if(*item == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    free(*item);

    *item = NULL;

    return LIST_SUCCESS;
}

int list_item_set_byte(list_item_t* pitem, char val)
{
    if(pitem == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    pitem->data.byte = val;
    pitem->type = LIST_DATA_TYPE_BYTE_INTERNAL;

    return LIST_SUCCESS;
}

int list_item_set_digit(list_item_t* pitem, int val)
{
    if(pitem == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    pitem->data.digit = val;
    pitem->type = LIST_DATA_TYPE_INTEGER_INTERNAL;

    return LIST_SUCCESS;
}

int list_item_set_pointer(list_item_t* pitem, void* val)
{
    if(pitem == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    pitem->data.pointer = val;
    pitem->type = LIST_DATA_TYPE_POINTER_INTERNAL;

    return LIST_SUCCESS;
}

int list_item_set_string(list_item_t* pitem, const char* string, int len)
{
    if( string == NULL )
    {
        return LIST_INVALID_POINTER;
    }

    if( len < 0)
    {
        return LIST_INVALID_STRING_LEN;
    }

    len += 1;

    if( len < sizeof(void*))    
    {
        memcpy(pitem->data.string, string, len);
        pitem->type = LIST_DATA_TYPE_SHORT_STRING_INTERNAL;
    }
    else
    {
        char* allocated_string = (char*)calloc(1, len);

        if(allocated_string == NULL)
        {
            return LIST_CANNOT_ALLOCATE_MEMORY;
        }

        memcpy(allocated_string, string, len);
        pitem->data.pointer = (void*)allocated_string;
        pitem->type = LIST_DATA_TYPE_ALLOCATED_STRING_INTERNAL;
    }

    return LIST_SUCCESS;

}

int list_insert(list_t* plist, list_item_t* position, list_item_t* item)
{
    if(plist == NULL || item == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(plist->size != 0 && position == NULL)
    {
        return LIST_INVALID_ARGUMENT;
    }

    if(position != NULL)
    {
        item->prev = position->prev;
        if(item->prev != NULL)
        {
            item->prev->next = item;
        }

        item->next = position;
        position->prev = item;
    }

    if(position == plist->first)
    {
        plist->first = item;

        if(plist->size == 0)
        {
            plist->last = item;
        }
    }

    plist->size += 1;

    return LIST_SUCCESS;
}


int list_item_set_struct(list_item_t* pitem, void* pstruct, int size)
{
    if(pitem == NULL || pstruct == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if(size < 0)
    {
        return LIST_INVALID_ARGUMENT;
    }

    if(size < sizeof(list_item_data))
    {
        memcpy(&pitem->data, pstruct, size);
        pitem->type = LIST_DATA_TYPE_COPIED_OPTIMIZED_CUSTOM_STRUCT_INTERNAL;
        return LIST_SUCCESS;
    }

    void* memmory = calloc(1, size);
    if(memmory == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    pitem->data.pointer = memmory;
    pitem->type = LIST_DATA_TYPE_COPIED_ALLOCATED_CUSTOM_STRRUCT_INTERNAL;
    memcpy(memmory, pstruct, size);

    return LIST_SUCCESS;
}

int list_item_emplace_struct(list_item_t* pitem, int size, construct_func func, void* arg)
{
    if(pitem == NULL || func == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if( size <= 0)
    {
        return LIST_INVALID_ARGUMENT;
    }

    if(size < sizeof(list_item_data))
    {
        pitem->type = LIST_DATA_TYPE_EMPLACED_OPTIMIZED_CUSTOM_STRUCT_INTERAL;
        return func((void*)&pitem->data, sizeof(list_item_data), arg);
    }

    void* memmory = calloc(1, size);

    if(memmory == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    pitem->data.pointer = memmory;
    pitem->type = LIST_DATA_TYPE_EMPLACED_ALLOCATED_CUSTOM_STRUCT_INTERNAL;

    return func(memmory, size, arg);
}


int list_find_if(list_t* plist, find_func func, void* data, list_item_t** item)
{
    if(plist == NULL || func == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    if( item == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    bool result = false;

    for(list_item_t* iterator = list_begin(plist); iterator != NULL; iterator = list_get_next(iterator))
    {
        int status = func(iterator, data, &result);

        if(status != LIST_SUCCESS)
        {
            *item = NULL;
            return status;
        }

        if(result)
        {
            *item = iterator;
            return LIST_SUCCESS;
        }
    }

    *item = NULL;
    return LIST_SUCCESS;
}


int safelist_create(safelist_t** list)
{
    if(list == NULL) 
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    safelist_t* plist = (safelist_t*)calloc(1, sizeof(safelist_t));

    if(plist == NULL)
    {
        return LIST_CANNOT_ALLOCATE_MEMORY;
    }

    int result = pthread_mutex_init(&plist->lock, NULL);

    if(result != 0)
    {
        free(plist);
        return result;
    }

    *list = plist;

    return LIST_SUCCESS;
}

int safelist_delete(safelist_t** list)
{
    if(list == NULL)
    {
        return LIST_INVALID_POINTER_TO_POINTER;
    }

    if(*list == NULL)   
    {
        return LIST_INVALID_POINTER;
    }

    int result = pthread_mutex_destroy(&((*list)->lock));

    if(result != 0)
    {
        return result;
    }

    free(*list);
    *list = NULL;

    return LIST_SUCCESS;
}


int safelist_lock(safelist_t* plist)
{
    if(plist == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    return pthread_mutex_lock(&plist->lock);
}

int safelist_unlock(safelist_t* plist)
{
    if(plist == NULL)
    {
        return LIST_INVALID_POINTER;
    }

    return pthread_mutex_unlock(&plist->lock);
}


const char* const list_get_error_string(const int error)
{
    if(error > 0)
    {
        return strerror(error);
    }

    switch (error)
    {
    case LIST_SUCCESS:
        return "Success";
    case LIST_FAILED:
        return "Failed";
    case LIST_CANNOT_ALLOCATE_MEMORY:
        return "Can not allocate memory";
    case LIST_INVALID_POINTER_TO_POINTER:
        return "Invalid pointer to pointer";
    case LIST_INVALID_POINTER:
        return "Invalid pointer";
    case LIST_WRONG_DATA_TYPE:
        return "Stored data type and expected data type are different";
    case LIST_INVALID_STRING_LEN:
        return "Length of string is less of zero";
    case LIST_UNKNOWN_DATA_TYPE:
        return "Type of data stored in item is unknown";
    case LIST_INVALID_ARGUMENT:
        return "Provided wrong argument";    
    default:
        return "Unknown";
    }

    return "Unknown";
}