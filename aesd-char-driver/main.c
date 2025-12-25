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
#include <linux/slab.h>
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Carlos Almeida"); /** DONE: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * DONE: handle open
     */

    struct aesd_dev *dev; /*Device information*/
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; 

    return 0;
}

static size_t aesd_get_total_size(struct aesd_circular_buffer *buffer)
{
    size_t total = 0;
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
        if (entry->buffptr)
            total += entry->size;
    }
    return total;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    size_t total_size;

    PDEBUG("llseek offset %lld whence %d", offset, whence);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    total_size = aesd_get_total_size(&dev->buffer);

    switch (whence) {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = filp->f_pos + offset;
        break;
    case SEEK_END:
        newpos = total_size + offset;
        break;
    default:
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    if (newpos < 0) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = newpos;
    mutex_unlock(&dev->lock);
    return newpos;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * DONE: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    size_t bytes_to_read;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * DONE: handle read
     */
    if(mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if(entry == NULL)
    {
        retval = 0;
        goto exit;
    }

    bytes_to_read = entry->size - entry_offset;
    if (bytes_to_read > count)
        bytes_to_read = count;
    
    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read)) {
        retval = -EFAULT;
        goto exit;
    }

    *f_pos += bytes_to_read;
    retval = bytes_to_read;
exit:
    mutex_unlock(&dev->lock);
    return retval;
}



ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *new_buffer;
    const char *newline_ptr;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * DONE: handle write
     */

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    new_buffer = krealloc(dev->current_entry, dev->current_entry_size + count, GFP_KERNEL);
    
    if (!new_buffer) {
        retval = -ENOMEM;
        goto exit;
    }
    dev->current_entry = new_buffer;

    if (copy_from_user(dev->current_entry + dev->current_entry_size, buf, count)) {
        retval = -EFAULT;
        goto exit;
    }
    
    dev->current_entry_size += count;

    newline_ptr = memchr(dev->current_entry, '\n', dev->current_entry_size);
    
    while ((newline_ptr = memchr(dev->current_entry, '\n', dev->current_entry_size))) {
    
        size_t entry_size = newline_ptr - dev->current_entry + 1;
    
        struct aesd_buffer_entry new_entry = {
            .buffptr = kmalloc(entry_size, GFP_KERNEL),
            .size = entry_size,
        };
    
        memcpy(new_entry.buffptr, dev->current_entry, entry_size);
    
        if (dev->buffer.full)
            kfree(dev->buffer.entry[dev->buffer.in_offs].buffptr);
    
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
    
        /* Move remaining data to front */
        memmove(dev->current_entry,
                dev->current_entry + entry_size,
                dev->current_entry_size - entry_size);
        
        dev->current_entry_size -= entry_size;
        
        dev->current_entry = krealloc(dev->current_entry,
                                      dev->current_entry_size,
                                      GFP_KERNEL);
    }


    retval = count;
    
exit:
    mutex_unlock(&dev->lock);
    return retval;
}

static long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = &dev->buffer;
    loff_t newpos = 0;
    uint32_t num_entries;
    uint8_t entry_index;
    uint32_t i;

    /* Calculate number of valid entries */
    if (buffer->full) {
        num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else if (buffer->in_offs >= buffer->out_offs) {
        num_entries = buffer->in_offs - buffer->out_offs;
    } else {
        num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
    }

    /* Validate write_cmd is in range */
    if (write_cmd >= num_entries)
        return -EINVAL;

    /* Get the entry index in the circular buffer */
    entry_index = (buffer->out_offs + write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    /* Validate write_cmd_offset is in range */
    if (write_cmd_offset >= buffer->entry[entry_index].size)
        return -EINVAL;

    /* Sum up sizes of all entries before the target entry */
    for (i = 0; i < write_cmd; i++) {
        uint8_t idx = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        newpos += buffer->entry[idx].size;
    }
    newpos += write_cmd_offset;

    filp->f_pos = newpos;
    return 0;
}

/**
 * ioctl handler for AESDCHAR_IOCSEEKTO command
 */
long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    long retval = 0;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
            return -EFAULT;

        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;

        retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
        mutex_unlock(&dev->lock);
        break;

    default:
        return -ENOTTY;
    }

    return retval;
}



struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =         aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
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

    /**
     * DONE: initialize the AESD specific portion of the device
     */

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;
    struct aesd_buffer_entry *entry;
    cdev_del(&aesd_device.cdev);

    /**
     * DONE: cleanup AESD specific poritions here as necessary
     */
    
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr)
            kfree(entry->buffptr);
    }
    kfree(aesd_device.current_entry);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

