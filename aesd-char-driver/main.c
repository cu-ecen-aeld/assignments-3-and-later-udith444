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

MODULE_AUTHOR("woernoe"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("AESD driver sample");
MODULE_INFO(intree, "Y");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;


    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *cb = &dev->data;

    size_t b_fpos = *f_pos;
    size_t b_start = 0;
    size_t totcpy = 0;

    struct aesd_buffer_entry *pEntry = NULL;

    if (mutex_lock_interruptible(&dev->lock))
         return -ERESTARTSYS;

    // walk buffer
    while ( ( pEntry = aesd_circular_buffer_find_entry_offset_for_fpos( cb, b_fpos, &b_start)) != NULL ) 
    {
        // Nr of bytes to copy
        size_t cpy_bytes =  pEntry->size - b_start;

        // any limitations on amount of receive buffer
        if ( (totcpy + cpy_bytes) > count ) 
        {
            // only the remaining nrs
            cpy_bytes = count - totcpy;
        }

        // copy to userspace
        if (copy_to_user(buf + totcpy, pEntry->buffptr + b_start, cpy_bytes)) {
	    retval = -EFAULT;
	    goto out;
	}

        // pointer to next bufferentry
        b_fpos += cpy_bytes;

        // amount of bytes copied
        totcpy += cpy_bytes;
        // exceeds the limit of userbuffer
        if ( totcpy >= count) {
             break;
        }
    }

    *f_pos += totcpy;

    retval = totcpy;

out:
    mutex_unlock(&dev->lock);


    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    retval = 0; // <<
    
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *cb = &dev->data;
    char *kmem = NULL;

    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

    if (count > 0 )
    {   
        ssize_t rcount = count;      // # Bytes to request

        if (dev->tmpDataSize != 0)   // previous non terminated string
        {
            rcount += dev->tmpDataSize;

            kmem = kmalloc(rcount + 1, GFP_KERNEL);   // alloc additional mem
            
            if (kmem == NULL)
                goto out;
                 
            memcpy(kmem, dev->tmpData, dev->tmpDataSize);
 
            copy_from_user(kmem + dev->tmpDataSize, buf, count);


            kfree(dev->tmpData);  // free 
             
            dev->tmpData = kmem;
            dev->tmpDataSize = rcount;
        }
        else {
            kmem = kmalloc(count + 1, GFP_KERNEL);
  
            if (kmem == NULL)
                goto out;

            copy_from_user(kmem, buf, count);

            dev->tmpData = kmem;
            dev->tmpDataSize = count;
            
        } 

        if (*(dev->tmpData + dev->tmpDataSize -1) == '\n') 
        {

             // temp block on stack           
             //struct aesd_buffer_entry *pEntry;
             struct aesd_buffer_entry tentry;
             
             tentry.buffptr = dev->tmpData;
             tentry.size = dev->tmpDataSize;
                        
             dev->tmpData = NULL;
             dev->tmpDataSize = 0;

             // free entry at in_offs as it will be overwritten
             if (cb->full) {
                 // free 
                 
                 if (cb->entry[cb->in_offs].buffptr) {
                    
                     kfree(cb->entry[cb->in_offs].buffptr);
                 }
             }
             
             aesd_circular_buffer_add_entry( cb, &tentry); 
             
        }
        
        retval = count;
    }
    else {
       // stopped
  
       if (dev->tmpData != NULL ) {
          
           kfree( dev->tmpData);
           dev->tmpData = NULL;
           dev->tmpDataSize = 0;
           goto out;
   
       }
    }
   
out:
    mutex_unlock(&dev->lock);

    return retval;
}

/*
 * The "extended" operations
 */

loff_t aesd_llseek (struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
    case 0: /* SEEK_SET */
 	newpos = off;
        break;

    case 1: /* SEEK_CUR */
	newpos = filp->f_pos + off;
	break;

    case 2: /* SEEK_END */
        // get size of cmds
        int nEntries = (dev->data.full ) ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : 
                       ((dev->data.in_offs - dev->data.out_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
        
        newpos = 0;
        int out_ind = dev->data.out_offs;
        for (int i=0; i < nEntries; i++ ) {
            newpos += dev->data.entry[out_ind].size;
            out_ind = (out_ind + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
	newpos += off;
	break;

    default: /* can't happen */
	return -EINVAL;
    }
    
    if (newpos<0) 
        return -EINVAL;
	
    filp->f_pos = newpos;

    return newpos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto req;
    loff_t newpos;
    int retval = -ENOTTY;
   
   PDEBUG("ioctl cmd %d (%d) arg %ld ", cmd, AESDCHAR_IOCSEEKTO, arg);
   
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) 
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) 
        return -ENOTTY;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;    
   
    switch(cmd) {
   
        case AESDCHAR_IOCSEEKTO:
       
           if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
               retval =  -EFAULT;
               goto unlock;
           }
               
           
           int nEntries = (dev->data.full ) ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : 
                       ((dev->data.in_offs - dev->data.out_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
           
           if ( req.write_cmd >= nEntries ) {
                retval -EINVAL;
                goto unlock;
           }
           
           newpos = 0;
           int outPtr = dev->data.out_offs;
           
           for (int i=0 ; i < req.write_cmd; i++ ) {
               newpos += dev->data.entry[outPtr].size;
               outPtr = (outPtr + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
           }
           newpos += req.write_cmd_offset;
           
           if (req.write_cmd_offset >= dev->data.entry[outPtr].size) {
                PDEBUG("ioctl: Size=%d Newpos=%d outPtr=%d ", dev->data.entry[outPtr].size, outPtr);
                retval = -EINVAL;
                goto unlock;
           }
           
           filp->f_pos = newpos;
             
           retval = 0;
           
           break;
       default:
           retval = -ENOTTY;
   
   }   // switch (cmd)

unlock:    
    mutex_unlock(&dev->lock);
    
    return retval; 
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
    .release =  aesd_release,
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
    
    //PDEBUG("aesd_init_module");
    
    result = alloc_chrdev_region(&dev, aesd_minor, 1,  "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */


    //aesd_device.data = &aesd_device;
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init( &aesd_device.data );       // we

    aesd_device.tmpData = NULL;                            // we
    aesd_device.tmpDataSize = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    if (mutex_lock_interruptible(&aesd_device.lock) )      
	return; // -ERESTARTSYS;
	
    int nEntries = (aesd_device.data.full ) ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : 
                       ((aesd_device.data.in_offs - aesd_device.data.out_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    int ind = aesd_device.data.out_offs;	

    for (int i=0 ; i < nEntries; i++ ) {
        // free memory
        
        if (  aesd_device.data.entry[ind].buffptr != NULL) 
            kfree (aesd_device.data.entry[ind].buffptr);
        aesd_device.data.entry[ind].buffptr = NULL;
        
        ind = (ind  + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } 	

    if (aesd_device.tmpData != NULL) {
        // free temp data
        kfree(aesd_device.tmpData);
        aesd_device.tmpData = NULL;
        aesd_device.tmpDataSize = 0;
    }
    mutex_unlock(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);


