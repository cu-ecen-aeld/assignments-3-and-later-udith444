#include "safefile.h"

#include <pthread.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

const char* g_ErrorStrings[] =
{
    "Invalid error code",
    "File handle is invalid",
    "Pointer to buffer is NULL",
    "Pointer to range is NULL",
    "Read callback pointer is NULL",
    "Safe file pointer is NULL",
    "Reading callback has returned false execuOon status"
};

struct safe_file_struct
{
    int handle;
    pthread_mutex_t lock;
    int err;
    const char* errorstring; 
};

void save_error(safe_file* const file, const int err, const char* const description)
{
    if( err == SF_INVALID_ERR)
    {
        file->err = errno;
        file->errorstring = strerror(errno);
    }
    else
    {
        file->err = err;
        file->errorstring = description;
    }
}

#define SAVE_ERRNO(file) save_error(file, errno, strerror(errno))
#define SAVE_CERRNO(file, error) save_error(file, error, strerror(error))
#define SAVE_CERROR(file, error) save_error(file, error, get_custom_error_string(error))

const char* const get_custom_error_string(int err) 
{
    err = abs(err) - 1;

    if(err > 0 && err < sizeof(g_ErrorStrings))
    {
        return g_ErrorStrings[err];
    }
    return "Unknown";
}

int safe_file_init_empty(safe_file** file)
{
    if(file == NULL)
    {
        return SF_INVALID_POINTER_TO_POINTER;
    }

    safe_file* tmpptr = (safe_file*)calloc(1, sizeof(safe_file));

    if(tmpptr == NULL)
    {
        return errno;
    }

    int result = 0;

    tmpptr->handle = INVALID_FILE_HANDLE;

    if( (result = pthread_mutex_init(&tmpptr->lock, NULL)) != 0)
    {
        free(tmpptr);
        return errno;
    }

    *file = tmpptr;

    return SF_SUCCESS;
}

bool safe_file_open(safe_file* file, const char* path, const int flags, mode_t mode)
{
    if(path == NULL)
    {
        //TODO:Add more error handling
        perror("Pointer to file is NULL");
        return NULL;
    }

    int handle = open(path, flags, mode);

    if(handle < 0)
    {
        SAVE_ERRNO(file);
        return false;
    }

    file->handle = handle;
    
    return true;
}

int safe_file_init(const char* path, int flags, mode_t mode, safe_file ** file)
{
    if(path == NULL)
    {
        return SF_FILE_PATH_IS_NULL;
    }

    int result = safe_file_init_empty(file);

    if(result != SF_SUCCESS)
    {        
        return result;
    }

    if(!safe_file_open(*file, path, flags, mode))
    {
        result = (*file)->err;
        safe_file_destroy(*file);
        return result;
    }

    return SF_SUCCESS;
}

bool safe_file_close(safe_file* file)
{
    if(file == NULL)
    {
        perror("NULL file pointer");
        return false;
    }

    int result = close(file->handle);

    if(result != SF_SUCCESS)
    {
        SAVE_ERRNO(file);
        return false;
    }

    file->handle = INVALID_FILE_HANDLE;

    return true;

}

bool safe_file_destroy(safe_file* file)
{
    if(file == NULL)
    {
        perror("NULL file pointer");
        return false;
    }

    int result = 0;

    if(file->handle != INVALID_FILE_HANDLE)
    {
        result = close(file->handle);

        if(result != SF_SUCCESS)
        {
            SAVE_ERRNO(file);
            return false;
        }        
    }

    //file->handle = INVALID_FILE_HANDLE;

    result = pthread_mutex_destroy(&file->lock); 

    if(result != 0)
    {
        SAVE_CERRNO(file, result);
        return false;
    }

    free(file);

    return true;
}


bool safe_file_lock(safe_file* file)
{
    int result = pthread_mutex_lock(&file->lock);

    if(result != 0)
    {
        SAVE_ERRNO(file);
        //save_error(file, result, strerror(result));
        return false;
    }

    return true;
}

bool safe_file_unlock(safe_file* file)
{
    int result = pthread_mutex_unlock(&file->lock);

    if(result != 0)
    {
        save_error(file, result, strerror(result));
        return false;
    }

    return true;
}

bool safe_file_seek(safe_file* file, const int whence ,const off_t offset, off_t* const position)
{
    if(file == NULL)
    {
        fprintf(stderr, "Safe file pointer is NULL\n");
        return false;
    }

    if(file->handle == INVALID_FILE_HANDLE)
    {
        SAVE_CERROR(file, SF_INVALID_FILE_HANDLE_ERR);
        return false;
    }

    off_t result = 0;

    result = lseek(file->handle, offset, whence);

    if(result < 0)
    {
        SAVE_ERRNO(file); 
        return false;
    }

    if(position != NULL)
    {
        *position = result;
    }

    return true;
}

bool safe_file_write(safe_file* file, const char* buf, size_t buffsize)
{
    if(file == NULL)
    {
        perror("NULL file handle");
        return false;
    }

    if(buf == NULL)
    {
        SAVE_CERROR(file, SF_BUFF_NULL_PTR_ERR);
        return false;
    }

    int result = 0;
    ssize_t writed = 0;

    do
    {
        result = write(file->handle, buf + writed, buffsize - writed);

        if(result == -1)
        {
            SAVE_ERRNO(file);
            return false;
        }

        writed += result;

    }while(writed < buffsize);


    return true;
}


bool safe_file_read(safe_file* file, char* buffer, size_t buffsize, size_t* readed)
{
    if(file == NULL)
    {
        perror("Safe file pointer is NULL");
        return false;
    }

    if(buffer == NULL)
    {
        SAVE_CERROR(file, SF_BUFF_NULL_PTR_ERR);
        return false;
    }

    ssize_t result = 0, readed_total = 0;

    do
    {
        result = read(file->handle, buffer + readed_total, buffsize);

        if(result < 0)
        {
            SAVE_ERRNO(file);
            return false;
        }

        readed_total += result;

        if(result == 0)
        {
            if(readed != NULL)
            {
                *readed = readed_total;
            }
            return true;
        }
        
    }while(readed_total < buffsize);

    return true;
}

int safe_file_read_range(safe_file* file, 
    const read_range_t* const range,
    char* buff, 
    const size_t buffsize, 
    const read_params_t* const params, 
    read_callback_t callback)
{

    if(file == NULL)
    {
        perror("Safe file pointer is NULL");    
        return SF_SAFE_FILE_NULL_ERR;
    }

    if(file->handle == INVALID_FILE_HANDLE)
    {
        SAVE_CERROR(file, SF_INVALID_FILE_HANDLE_ERR);
        return SF_INVALID_FILE_HANDLE_ERR;
    }

    if(range == NULL)
    {
        SAVE_CERROR(file, SF_RANGE_POINTER_NULL_ERR);
        return SF_RANGE_POINTER_NULL_ERR;
    }

    if(callback == NULL)
    {
        SAVE_CERROR(file, SF_CALLBACK_POINTER_NULL_ERR);
        return SF_CALLBACK_POINTER_NULL_ERR;
    }

    if(!safe_file_lock(file))
    {
        return file->err;
    }
    
    off_t start_position = 0, end_position = 0;
    int error = 0;
    
    if(false ==safe_file_seek(file, range->start.whence, range->start.offset, &start_position))
    {
        error = file->err;
        goto out_unlock;
    }

    if(false == safe_file_seek(file, range->end.whence, range->end.offset, &end_position))
    {
        error = file->err;
        goto out_unlock;    
    }

    if( false == safe_file_seek(file, SEEK_SET, start_position, NULL))
    {        
        error = file->err;
        goto out_unlock;
    }

    ssize_t bytes_to_read_total = end_position - start_position;    
    ssize_t readed_bytes = 0, result = 0;

    do
    {
        size_t bytes_to_read = buffsize; 

        if( (bytes_to_read_total - readed_bytes) < buffsize)
        {
            bytes_to_read = bytes_to_read_total - readed_bytes;
        }
        
        result = read(file->handle, buff, bytes_to_read);
        if(result < 0)
        {
            SAVE_ERRNO(file);
            error = file->err;
            goto out_unlock;
        }

        readed_bytes += result;

        if(!callback(result, buff, buffsize, params))
        {
            error = SF_CALLBACK_HAS_RETURNED_FALSE_STATUS;
            goto out_unlock;
        }
        
    } 
    while(readed_bytes < bytes_to_read_total);    

out_unlock:
    safe_file_unlock(file);

    return error;
}


int safe_file_seek_and_write(safe_file* file, const int whence, const off_t offset, const char* const buff, const size_t buffsize)
{
    int error = 0;

    if(file == NULL)
    {
        perror("Safe file is NULL");
        return SF_SAFE_FILE_NULL_ERR;
    }

    if(file->handle == INVALID_FILE_HANDLE)
    {
        SAVE_CERROR(file, SF_INVALID_FILE_HANDLE_ERR);
        return SF_INVALID_FILE_HANDLE_ERR;
    }

    if(!safe_file_lock(file))
    {
        return file->err;
    }

    if(!safe_file_seek(file, whence, offset, NULL) || 
       !safe_file_write(file, buff, buffsize))
    {
        error = file->err;
        safe_file_unlock(file);
        return error;
    }

    if(!safe_file_unlock(file))
    {
        return file->err;
    }

    return SF_SUCCESS;
}

int safe_file_get_error(safe_file* file,const char** errstring)
{
    if(file == NULL)
    {
        perror("File ptr is NULL");
        *errstring = "Safe file pointer is NULL";
        return SF_INVALID_ERR;
    }

    if(errstring != NULL)
    {
        *errstring = file->errorstring;        
    }

    return file->err;    
}

const char* safe_file_get_error_string(int error)
{
    if(error > 0)
    {
        return strerror(error);
    }
    else if( error < 0 && error < SF_INVALID_ERR && error > SF_COUNT_ERR)
    {
        return get_custom_error_string(error);
    }

    return "Success";
}

int safe_file_sync(safe_file* file)
{
    int error = 0;

    if(file == NULL)
    {
        perror("Safe file is NULL");
        return SF_SAFE_FILE_NULL_ERR;
    }

    if(!safe_file_lock(file))
    {
        return file->err;
    }

    if(file->handle == INVALID_FILE_HANDLE)
    {
        SAVE_CERROR(file, SF_INVALID_FILE_HANDLE_ERR);
        safe_file_unlock(file);
        return SF_INVALID_FILE_HANDLE_ERR;
    }

    error = fdatasync(file->handle);
    
    if(error != 0)
    {   
       SAVE_ERRNO(file);
       error = errno;
       safe_file_unlock(file);
       return error;
    }

    if(!safe_file_unlock(file))
    {
        return file->err;
    }

    return SF_SUCCESS;
}

int safe_file_get_fd(safe_file* file)
{
    if(file == NULL)
    {
        perror("Safe file is NULL");
        return SF_SAFE_FILE_NULL_ERR;
    }

    if(file->handle == INVALID_FILE_HANDLE)
    {
        SAVE_CERROR(file, SF_INVALID_FILE_HANDLE_ERR);
        return SF_INVALID_FILE_HANDLE_ERR;
    }

    return file->handle;
}