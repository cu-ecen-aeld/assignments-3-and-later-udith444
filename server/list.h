#pragma once

#include <stddef.h>
#include <stdbool.h>

struct list_struct;
struct list_item_struct;
struct safelist_struct;

typedef struct list_item_struct list_item_t;
typedef struct list_struct list_t;
typedef struct safelist_struct safelist_t;

typedef enum 
{
    LIST_SUCCESS = 0,
    LIST_FAILED = -1,
    LIST_CANNOT_ALLOCATE_MEMORY = -2,
    LIST_INVALID_POINTER_TO_POINTER = -3,
    LIST_INVALID_POINTER = -4,
    LIST_WRONG_DATA_TYPE = -5,
    LIST_INVALID_STRING_LEN = -6,
    LIST_UNKNOWN_DATA_TYPE = -7,
    LIST_INVALID_ARGUMENT = -8
}list_errors_t;

typedef enum 
{
    LIST_DATA_TYPE_UNKNOWN = 0,
    LIST_DATA_TYPE_BYTE = 1,
    LIST_DATA_TYPE_INTEGER = 1 << 1,
    LIST_DATA_TYPE_POINTER = 1 << 2,
    LIST_DATA_TYPE_STRING = 1 << 3,
    LIST_DATA_TYPE_EMPLACED_CUSTOM_STRUCT = 1 << 4

}data_type_t;

typedef int (*construct_func)(void* mem, size_t size, void* arg);
typedef int(*each_func)(list_item_t* pitem, void* data);
typedef int(*find_func)(list_item_t* pitem, void* data, bool* result);

int list_create(list_t** plist);
int list_delete(list_t** plist);

int safelist_create(safelist_t** list);
int safelist_delete(safelist_t** list);

int safelist_lock(safelist_t* plist);
int safelist_unlock(safelist_t* plist);

int list_item_create(list_item_t** item);
int list_item_delete(list_item_t** item);

int list_item_set_byte(list_item_t* pitem, char val);
int list_item_set_digit(list_item_t* pitem, int val);
int list_item_set_pointer(list_item_t* pitem, void* val);
int list_item_set_string(list_item_t* pitem, const char* string, int len);

int list_item_set_struct(list_item_t* plist, void* pstruct, int size);
int list_item_emplace_struct(list_item_t* plist, int size, construct_func func, void* arg);

int list_insert(list_t* plist, list_item_t* position, list_item_t* item);

//int list_push_back(list_t* plist, data_type_t type, ...);

int list_push_back_byte(list_t* plist, char val);
int list_push_back_digit(list_t* plist, int val);
int list_push_back_pointer(list_t* plist, void* val);
int list_push_back_string(list_t* plist, const char* string, int len);


int list_push_back(list_t* plist, void* pstruct, int size);
int list_emplace_back(list_t* plist, int size, construct_func func, void* arg);

//TODO:add push_fron and emplace_front methods
//TODO:Add find_if method 
//TODO:Add for_each_range
//TODO:Add pop_front pop_back
//TODO:Add remove_if

int list_for_each(list_t* plist, each_func func,  void* data);
int list_find_if(list_t* plist, find_func func, void* data, list_item_t** item);

int list_item_get_byte(list_item_t* pitem, char* val);
int list_item_get_digit(list_item_t* pitem, int* val);
int list_item_get_pointer(list_item_t* pitem, void** val);
int list_item_get_string(list_item_t* pitem, char** val);
int list_item_get_custom_struct(list_item_t* pitem, void** val);

data_type_t list_get_item_data_type(list_item_t* pitem);

int list_remove_item(list_t* plist, list_item_t* pitem);

int list_clear(list_t* plist);

list_item_t* list_begin(list_t* plist);
list_item_t* list_end(list_t* plist);

list_item_t* list_get_next(list_item_t* pitem);
list_item_t* list_get_previous(list_item_t* pitem);

int list_size(list_t* plist);

const char* const list_get_error_string(const int error);

