/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("udith sandeepa");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = &aesd_device;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filpaesd_device, char __user *buf, size_t count,
                loff_t *f_pos)
{

    struct aesd_dev* pdevice = NULL;
    struct aesd_buffer_entry* selected_enty = NULL;
    size_t entry_offset_byte_rtn = 0;
    size_t bytes_available = 0;
    size_t bytes_to_copy = 0;
    int return_code = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if(NULL == filpaesd_device->private_data)
    {
        return -ENODEV;
    }

    if(!access_ok(buf, count))
    {
        return -EFAULT;
    }

    pdevice = (struct aesd_dev*)filpaesd_device->private_data;

    down_read(&pdevice->rwsem);

    selected_enty = aesd_circular_buffer_find_entry_offset_for_fpos(&pdevice->circular_buffer, *f_pos, &entry_offset_byte_rtn); 

    if(selected_enty == NULL)
    {
        return_code = 0;
        goto unlock_device;
    }

    bytes_available = selected_enty->size - entry_offset_byte_rtn;
    bytes_to_copy = bytes_available < count ? bytes_available : count;

    if(copy_to_user(buf, selected_enty->buffptr + entry_offset_byte_rtn, bytes_to_copy))
    {
        return_code = -EFAULT;
        goto unlock_device;
    }    

    *f_pos += bytes_to_copy;
    return_code = bytes_to_copy;

unlock_device:    
    up_read(&pdevice->rwsem);

    return return_code;
}

int add_to_cache(struct aesd_dev* pdevice, const char __user *buf, size_t count, loff_t *f_pos)
{
    int error_code = 0;
    char* new_buff = NULL;

    down_write(&pdevice->rwsem);

    if(pdevice->cache.buffptr == NULL)
    {
        PDEBUG("Create cache using line with size: %zu", count);

        pdevice->cache.buffptr = kmalloc(count, GFP_KERNEL);
        if(pdevice->cache.buffptr == NULL)
        {
            error_code = -ENOMEM;
            goto out_unlock;
        }
        memcpy((char*)pdevice->cache.buffptr, buf, count);
        pdevice->cache.size = count;
        pdevice->cache.allocated = count;
    }
    else
    {
        PDEBUG("Extend existing cache with size: %zu using line with size: %zu", pdevice->cache.size, count);

        new_buff = (char*)krealloc((void*)pdevice->cache.buffptr, pdevice->cache.size + count, GFP_KERNEL);

        if(new_buff == NULL)
        {
            error_code = -ENOMEM;
            goto out_unlock;
        }

        memcpy(new_buff + pdevice->cache.size, buf, count);
        pdevice->cache.buffptr = new_buff;
        pdevice->cache.size += count;
        pdevice->cache.allocated += count;
    }

out_unlock:
    up_write(&pdevice->rwsem);
    *f_pos += count;

    return error_code;
}

int write_with_cache(struct aesd_dev* pdevice, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_buffer_entry buf_entry;
    size_t total_size = 0;
    char* complete_buff = NULL;
    int return_code = 0;

    down_write(&pdevice->rwsem);

    if(pdevice->cache.buffptr == NULL)
    {
        PDEBUG("Add circullar buffer entry with size: %zu", count);
        buf_entry.buffptr = buf;
        buf_entry.size = count;

        aesd_circular_buffer_add_entry(&pdevice->circular_buffer, &buf_entry);
        goto out_unlock;
    }

    total_size = pdevice->cache.size + count;

    complete_buff = (char*)krealloc((void*)pdevice->cache.buffptr, total_size, GFP_KERNEL);

    if(complete_buff == NULL)
    {
        return_code = -ENOMEM;
        goto out_unlock;
    }

    memcpy(complete_buff + pdevice->cache.size, buf, count);
                
    PDEBUG("Add circullar buffer entry using cache with size: %zu and line with size: %zu", pdevice->cache.size, count);

    buf_entry.buffptr = complete_buff;
    buf_entry.size = total_size;

    aesd_circular_buffer_add_entry(&pdevice->circular_buffer, &buf_entry);

    kfree((void*)pdevice->cache.buffptr);

    pdevice->cache.buffptr = NULL;
    pdevice->cache.allocated = 0;
    pdevice->cache.size = 0;
    
out_unlock:
    up_write(&pdevice->rwsem);

    *f_pos += buf_entry.size;
    return return_code;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{ 
    struct aesd_dev* pdevice = NULL;

    char* user_data_buff = NULL;
    
    char* begin_position = NULL;
    char* end_position = NULL;
    char* current_position = NULL;

    int error_code = 0;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if(count == 0)
    {
        return 0;
    }

    if(NULL == filp->private_data)
    {
        return -ENODEV;
    }

    if(!access_ok(buf, count))
    {
        return -EFAULT;
    }

    user_data_buff = (char*)kmalloc(count, GFP_KERNEL);
    
    if(user_data_buff == NULL)
    {
        return -ENOMEM;
    }

    if(copy_from_user((char*)user_data_buff, buf, count))
    {
        error_code = -EFAULT;
        goto free_user_buff;
    }

    pdevice = (struct aesd_dev*)filp->private_data;

    begin_position = user_data_buff;
    end_position = user_data_buff + count;
    current_position = user_data_buff;

    while(current_position != end_position)
    {
        if('\n' == *current_position )
        {
            error_code = write_with_cache(pdevice, begin_position, (current_position + 1) - begin_position, f_pos);
            if(0 != error_code)
            {
                goto free_user_buff;
            }
            begin_position = current_position + 1;
        }

        ++current_position;
    }

    if(begin_position != end_position)
    {
        error_code = add_to_cache(pdevice, begin_position, end_position - begin_position, f_pos);
    }

free_user_buff:
    kfree((void*)user_data_buff);

    return error_code == 0 ? count : error_code;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t newpos = 0;
    struct aesd_dev* pdevice = NULL;
    size_t total_size = 0;
    int index = 0;
    struct aesd_buffer_entry* entry = NULL;

    pdevice = (struct aesd_dev*)filp->private_data;

    down_write(&pdevice->rwsem);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &pdevice->circular_buffer, index) {
        total_size += entry->size;
    }

    switch (whence) {
    case SEEK_SET:
        newpos = offset > total_size ? -EINVAL : offset;
        break;
    case SEEK_CUR:
        newpos = (filp->f_pos + offset) > total_size ? -EINVAL : (filp->f_pos + offset);
        break;
    case SEEK_END:
        newpos = (total_size + offset) > total_size ? -EINVAL : (total_size + offset);
        break;
    default:
        newpos = -EINVAL;
        goto out_unlock;
    }

    if (newpos < 0)
    {
        newpos = -EINVAL;
        goto out_unlock;
    }

    filp->f_pos = newpos;

    PDEBUG("llseek to offset: %lld, whence: %d result in offset: %ldd", offset, whence, newpos);

out_unlock:
    up_write(&pdevice->rwsem);

    return newpos;
}

int aesd_adjust_file_offset (struct file *filp, const uint32_t write_cmd, const uint32_t write_cmd_offset)
{
    loff_t fpos = 0;
    size_t index = 0;
    int error = 0;
    struct aesd_buffer_entry* entry = NULL;
    struct aesd_dev* pdevice = NULL;    

    if(write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        return -EINVAL;
    }

    pdevice = (struct aesd_dev*)filp->private_data;

    down_write(&pdevice->rwsem);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &pdevice->circular_buffer, index) {
        if(write_cmd == index)
        {
            if(write_cmd_offset >= entry->size)
            {
                error = -EINVAL;
                goto out_unlock;
            }

            fpos += write_cmd_offset;
            break;
        }

        fpos += entry->size;
    }

    PDEBUG("Adjust file offset to write_cmd: %u, write_cmd_offset: %u resulting in fpos: %lld", write_cmd, write_cmd_offset, fpos);
    filp->f_pos = fpos;

out_unlock:
    up_write(&pdevice->rwsem);

    return error;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;

    struct aesd_seekto seekto;

    // if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) {
    //     return -ENOTTY;
    // }

    // if( _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR ) {
    //     return -ENOTTY;
    // }

    switch (cmd) {
    case AESDCHAR_IOCSEEKTO: 
    {
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
            retval = -EFAULT;
            break;
        }

        retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
        break;
    }
    default:
        retval = -ENOTTY;
        break;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .compat_ioctl = aesd_ioctl,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&aesd_device.circular_buffer);

    init_rwsem(&aesd_device.rwsem);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    size_t index = 0;
    struct aesd_buffer_entry* entry = NULL;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        kfree(entry->buffptr);
    }

    if(aesd_device.cache.buffptr != NULL)
    {
        kfree((void*)aesd_device.cache.buffptr);
        aesd_device.cache.buffptr = NULL;
    }

    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
