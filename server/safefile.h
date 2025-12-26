#ifndef HH_SAFE_FILE
#define HH_SAFE_FILE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdbool.h>

#ifdef INVALID_FILE_HANDLE
#error "Invalid file handle has already defined"
#else
#define INVALID_FILE_HANDLE -1
#endif

typedef union read_params_union 
{
    void* data;
    int num;

}read_params_t;

typedef struct read_point_struct
{
    int whence;
    off_t offset;

}read_point_t;

typedef struct read_range_struct
{
    read_point_t start;
    read_point_t end;
}read_range_t;

enum SAFE_FILE_ERRORS
{
    SF_SUCCESS = 0,
    SF_INVALID_ERR = -1,
    SF_INVALID_FILE_HANDLE_ERR = -2,
    SF_BUFF_NULL_PTR_ERR = -3,
    SF_RANGE_POINTER_NULL_ERR = -4,
    SF_CALLBACK_POINTER_NULL_ERR = -5,
    SF_SAFE_FILE_NULL_ERR = -6,
    SF_CALLBACK_HAS_RETURNED_FALSE_STATUS = -7,
    SF_FILE_PATH_IS_NULL = -8,
    SF_INVALID_POINTER_TO_POINTER = -9,
    SF_COUNT_ERR = -10
};

typedef struct safe_file_struct safe_file;

// TODO:Change return type of the callback from bool to int for storing error codes
typedef bool (*read_callback_t)(const ssize_t readed, const char* const buff, const size_t count, read_params_t const* params);

int safe_file_init_empty(safe_file** file);
int safe_file_init(const char* path, const int flags, mode_t mode, safe_file** file);
bool safe_file_destroy(safe_file* file);

bool safe_file_open(safe_file* file, const char* path, const int flags, mode_t mode);
bool safe_file_close(safe_file* file);

bool safe_file_lock(safe_file* file);
bool safe_file_unlock(safe_file* file);

//Methods that need lock before performing
bool safe_file_seek(safe_file* file, const int whence, const off_t offset, off_t * const position);

bool safe_file_write(safe_file* file, const char* buf, const size_t count);
bool safe_file_read(safe_file* file, char* buffer, const size_t buffsize, size_t* readed);

int safe_file_get_error(safe_file* file, const char** errstring);

int safe_file_sync(safe_file* file);

//Atomic methods that don't need lock before performing
int safe_file_seek_and_write(safe_file* file,
    const int whence, const off_t offset,
    const char* const buff,
    const size_t count);

int safe_file_read_range(safe_file* file,
    const read_range_t* const range,
    char* buff,
    const size_t count,
    const read_params_t* const params,
    read_callback_t callback);

const char* const safe_file_get_error_string(int error);

int safe_file_get_fd(safe_file* file);

#endif
