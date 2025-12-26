/**
 * @file aesdchar.c
 * @brief AESD character driver - fixed version (correct circular buffer overwrite handling)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> /* file_operations */
#include "aesdchar.h"
#include "aesd_ioctl.h" 
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kernel.h>

#ifndef PDEBUG
#ifdef DEBUG
#define PDEBUG(fmt, args...) printk(KERN_DEBUG "%s: " fmt "\n", __func__, ##args)
#else
#define PDEBUG(fmt, args...) do { } while (0)
#endif
#endif

#ifndef AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE
#define AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE 10
#endif

#ifndef AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
#define AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE
#endif



int aesd_major = 0;
int aesd_minor = 0;

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Helper: free a single circular buffer entry's memory (if owned) */
static void aesd_free_circbuf_entry(struct aesd_buffer_entry *entry)
{
  if (entry && entry->buffptr) {
    kfree((void *)entry->buffptr);
    entry->buffptr = NULL;
    entry->size = 0;
  }
}

/* Helper: free all entries currently stored in the circular buffer */
static void aesd_free_all_entries(struct aesd_circular_buffer *cb)
{
  unsigned int idx = cb->out_offs;
  unsigned int count = 0;

  /* iterate up to maximum buffer entries */
  while (count < AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE) {
    struct aesd_buffer_entry *e = &cb->entry[idx];
    aesd_free_circbuf_entry(e);

    count++;
    if (!cb->full && idx == cb->in_offs)
      break;
    idx = (idx + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;
    /* if we've looped back to out_offs and buffer is full, we've freed all */
    if (idx == cb->out_offs && cb->full)
      break;
  }
}

/* Add entry into circular buffer, freeing overwritten entry if necessary */
static void aesd_circbuf_add_and_manage(struct aesd_circular_buffer *cb,
                                        struct aesd_buffer_entry *new_entry)
{
  bool was_full = cb->full;

  pr_info("CIRC_MANAGE: before add in=%u out=%u full=%d adding_ptr=%p size=%zu\n",
          cb->in_offs, cb->out_offs, cb->full,
          new_entry ? new_entry->buffptr : NULL,
          new_entry ? new_entry->size : 0);

  /* If buffer is full, the slot that will be overwritten is the one at in_offs.
     Free it here (caller is responsible for freeing memory owned by entries). */
  if (was_full) {
    pr_info("CIRC_MANAGE: buffer full -> freeing slot at in_offs=%u (about to be overwritten)\n",
            cb->in_offs);
    aesd_free_circbuf_entry(&cb->entry[cb->in_offs]);
  }

  /* Add entry using provided circular buffer helper (this updates in_offs and sets full appropriately) */
  aesd_circular_buffer_add_entry(cb, new_entry);

  /* If buffer was full before add, advance out_offs so it points to the new oldest entry.
     Reason: before add, in_offs == out_offs (full). We overwrote the slot at in_offs, and
     aesd_circular_buffer_add_entry advanced in_offs. So oldest moved forward by one. */
  if (was_full) {
    cb->out_offs = (cb->out_offs + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;
    pr_info("CIRC_MANAGE: buffer was full -> advanced out_offs to %u\n", cb->out_offs);
    /* cb->full remains true (add_entry will set it if wrapped) */
    cb->full = true;
  }

  pr_info("CIRC_MANAGE_DONE: after add in=%u out=%u full=%d\n",
          cb->in_offs, cb->out_offs, cb->full);
}

/*
 * Local implementation to find entry and offset for a given file position.
 * Returns pointer to entry and sets *entry_offset and *entry_index.
 * Returns NULL if position is beyond available data.
 */
static struct aesd_buffer_entry *
aesd_local_find_entry_offset_for_fpos(struct aesd_circular_buffer *cb,
                                      size_t fpos, size_t *entry_offset,
                                      unsigned int *entry_index)
{
    size_t accumulated = 0;
    unsigned int idx = cb->out_offs;
    unsigned int traversed = 0;

    pr_info("L_FIND_START: fpos=%zu in=%u out=%u full=%d\n",
            fpos, cb->in_offs, cb->out_offs, cb->full);

    // Traverse entries in order
    while (traversed < AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE) {
        struct aesd_buffer_entry *e = &cb->entry[idx];
        pr_info("L_FIND_LOOP: idx=%u accumulated=%zu size=%zu ptr=%p\n",
                idx, accumulated, e->size, e->buffptr);

        if (!e->buffptr || e->size == 0) {
            // No more data stored
            pr_info("L_FIND_LOOP_EMPTY: idx=%u accumulated=%zu\n", idx, accumulated);
            return NULL;
        }

        if (accumulated + e->size > fpos) {
            // Target is inside this entry
            *entry_offset = fpos - accumulated;
            *entry_index = idx;

            pr_info("L_FIND_OK: idx=%u entry_offset=%zu\n",
                    idx, *entry_offset);

            return e;
        }

        accumulated += e->size;

        // Stop if we've reached the in_offs and buffer not full
        if (!cb->full && idx == cb->in_offs)
            break;

        idx = (idx + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;
        traversed++;

        // Stop if we've wrapped fully
        if (idx == cb->out_offs && cb->full)
            break;
    }

    pr_err("L_FIND_FAIL: fpos=%zu accumulated=%zu idx=%u\n",
           fpos, accumulated, idx);

    return NULL;
}

int aesd_open(struct inode *inode, struct file *filp)
{
  PDEBUG("open");
  /* store device pointer for this open file */
  filp->private_data = &aesd_device;
  return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
  PDEBUG("release");
  return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    size_t bytes_to_copy = count;
    size_t copied = 0;

    pr_info("READ_LOOP_START: f_pos=%lld count=%zu circ_in=%u circ_out=%u full=%d\n",
            *f_pos, count, dev->circ_buf.in_offs, dev->circ_buf.out_offs, dev->circ_buf.full);

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (!dev)
        return -EINVAL;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    {
        size_t total_size = 0;
        int i;
        for (i = 0; i < AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE; i++) {
            if (dev->circ_buf.entry[i].buffptr)
                total_size += dev->circ_buf.entry[i].size;
        }
        if (*f_pos >= total_size) {
            mutex_unlock(&dev->lock);
            return 0;
        }
        if (count > total_size - *f_pos)
            count = total_size - *f_pos;
        bytes_to_copy = count;
    }

    // Find starting entry and offset for f_pos
    {
        size_t entry_off = 0;
        unsigned int entry_idx = 0;
        struct aesd_buffer_entry *entry;

        entry = aesd_local_find_entry_offset_for_fpos(&dev->circ_buf,
                                                      *f_pos,
                                                      &entry_off,
                                                      &entry_idx);
        pr_info("DRV_READ: find returned entry=%p entry_off=%zu entry_idx=%u\n", entry, entry_off, entry_idx);

        if (!entry) {
            // Nothing available at this offset
            retval = 0;
            goto read_out;
        }

        // Iterate entries copying up to count bytes
        while (bytes_to_copy > 0 && entry && entry->buffptr && entry->size > entry_off) {
            size_t avail = entry->size - entry_off;
            size_t to_copy = (avail < bytes_to_copy) ? avail : bytes_to_copy;

            pr_info("DRV_READ_COPY: copying %zu bytes from entry idx=%u offset=%zu\n",
                    to_copy, entry_idx, entry_off);

            if (copy_to_user(buf + copied, entry->buffptr + entry_off, to_copy)) {
                retval = -EFAULT;
                goto read_out;
            }

            copied += to_copy;
            bytes_to_copy -= to_copy;
            *f_pos += to_copy;
            retval += to_copy;

            // Move to next entry
            entry_off = 0;
            entry_idx = (entry_idx + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;

            entry = &dev->circ_buf.entry[entry_idx];
        }
    }

    pr_info("READ_DONE: returned=%zd new_fpos=%lld\n", retval, *f_pos);

read_out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
  ssize_t retval = 0;
  struct aesd_dev *dev = filp->private_data;

  char *newbuf = NULL;

  PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

  if (!dev)
    return -EINVAL;

  /* prevent concurrent writers/readers while we perform write operation */
  if (mutex_lock_interruptible(&dev->lock))
    return -ERESTARTSYS;

  /* append incoming user data to device's write_buf (grow as necessary) */
  if (count) {
    newbuf = kmalloc(dev->write_buf_size + count, GFP_KERNEL);
    if (!newbuf) {
      retval = -ENOMEM;
      goto write_out;
    }

    if (dev->write_buf && dev->write_buf_size) {
      memcpy(newbuf, dev->write_buf, dev->write_buf_size);
      kfree(dev->write_buf);
    }
    /* copy from user into tail of newbuf */
    if (copy_from_user(newbuf + dev->write_buf_size, buf, count)) {
      kfree(newbuf);
      retval = -EFAULT;
      goto write_out;
    }
    dev->write_buf = newbuf;
    dev->write_buf_size += count;
  }

  /* process any complete commands (terminated by '\n') in write_buf */
  {
    size_t processed = 0;
    char *scan_ptr = dev->write_buf;
    size_t remaining = dev->write_buf_size;

    while (remaining > 0) {
      void *nl = memchr(scan_ptr, '\n', remaining);
      if (!nl)
        break; /* no complete command yet */

      /* length includes the newline */
      size_t cmd_len = (char *)nl - scan_ptr + 1;

      /* allocate exact-sized buffer for this command */
      char *cmd_buf = kmalloc(cmd_len, GFP_KERNEL);
      if (!cmd_buf) {
        retval = -ENOMEM;
        goto write_out;
      }
      memcpy(cmd_buf, scan_ptr, cmd_len);

      /* prepare entry */
      struct aesd_buffer_entry new_entry = {
        .buffptr = cmd_buf,
        .size = cmd_len
      };

      pr_info("DRV_WRITE_CMD: size=%zu ptr=%p\n", cmd_len, cmd_buf);

      /* if adding will overwrite an existing entry, free it first and adjust out_offs */
      aesd_circbuf_add_and_manage(&dev->circ_buf, &new_entry);

      /* advance processed pointer */
      scan_ptr += cmd_len;
      processed += cmd_len;
      remaining -= cmd_len;

      /* continue to find further complete commands in current buffer */
    }

    /* if we've processed any complete commands, shrink write_buf to retain remaining partial */
    if (processed > 0) {
      if (remaining > 0) {
        char *leftover = kmalloc(remaining, GFP_KERNEL);
        if (!leftover) {
          retval = -ENOMEM;
          goto write_out;
        }
        memcpy(leftover, dev->write_buf + processed, remaining);
        kfree(dev->write_buf);
        dev->write_buf = leftover;
        dev->write_buf_size = remaining;
      } else {
        kfree(dev->write_buf);
        dev->write_buf = NULL;
        dev->write_buf_size = 0;
      }
    }
  }

  /* on success, report that we've accepted 'count' bytes */
  retval = (ssize_t)count;

write_out:
  mutex_unlock(&dev->lock);
  return retval;
}

struct file_operations aesd_fops = {
  .owner = THIS_MODULE,
  .read = aesd_read,
  .write = aesd_write,
  .open = aesd_open,
  .release = aesd_release,
  .llseek = aesd_llseek, // Add llseek support
  .unlocked_ioctl = aesd_ioctl, // Add ioctl support
};

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    size_t total_size = 0;
    unsigned int idx;
    unsigned int traversed = 0;
    
    if (!dev)
        return -EINVAL;
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    // Calculate total size properly by traversing the circular buffer
    idx = dev->circ_buf.out_offs;
    while (traversed < AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE) {
        struct aesd_buffer_entry *e = &dev->circ_buf.entry[idx];
        if (e->buffptr) {
            total_size += e->size;
        }
        
        // Check if we should stop
        if (!dev->circ_buf.full && idx == dev->circ_buf.in_ops)
            break;
            
        idx = (idx + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;
        traversed++;
        
        if (idx == dev->circ_buf.out_offs && dev->circ_buf.full)
            break;
    }
    
    mutex_unlock(&dev->lock);
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = total_size + offset;
            break;
        default:
            return -EINVAL;
    }
    
    if (new_pos < 0 || new_pos > total_size) {
        return -EINVAL;
    }
    
    filp->f_pos = new_pos;
    return new_pos;
}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    size_t total_offset = 0;
    unsigned int idx;
    unsigned int traversed = 0;
    
    if (!dev)
        return -EINVAL;
    
    if (cmd != AESDCHAR_IOCSEEKTO) {
        return -EINVAL;
    }
    
    if (copy_from_user(&seekto, (void __user *)arg, sizeof(seekto))) {
        return -EFAULT;
    }
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    // Validate seekto.write_cmd is within valid range
    if (seekto.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }
    
    // Traverse the circular buffer to find the target entry
    idx = dev->circ_buf.out_offs;
    while (traversed < AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE) {
        struct aesd_buffer_entry *e = &dev->circ_buf.entry[idx];
        
        if (!e->buffptr) {
            mutex_unlock(&dev->lock);
            return -EINVAL;  // Entry doesn't exist
        }
        
        // If we've reached the target write_cmd index
        if (traversed == seekto.write_cmd) {
            if (seekto.write_cmd_offset >= e->size) {
                mutex_unlock(&dev->lock);
                return -EINVAL;
            }
            
            // Found the target entry, calculate total offset
            total_offset += seekto.write_cmd_offset;
            mutex_unlock(&dev->lock);
            
            pr_info("AESDCHAR_IOCSEEKTO:%u,%u -> f_pos=%zu\n", 
                    seekto.write_cmd, seekto.write_cmd_offset, total_offset);
            
            filp->f_pos = total_offset;
            return 0;
        }
        
        // Not the target yet, add its size to the offset
        total_offset += e->size;
        
        // Move to next entry
        if (!dev->circ_buf.full && idx == dev->circ_buf.in_ops)
            break;
            
        idx = (idx + 1) % AESDCHAR_MAX_CIRCULAR_BUFFER_SIZE;
        traversed++;
        
        if (idx == dev->circ_buf.out_offs && dev->circ_buf.full)
            break;
    }
    
    mutex_unlock(&dev->lock);
    return -EINVAL;  // Requested write_cmd index not found
}

static int aesd_setup_cdev(struct aesd_dev *dev)
{
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev", err);
  }
  return err;
}

int aesd_init_module(void)
{
  dev_t dev = 0;
  int result;
  result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
  aesd_major = MAJOR(dev);
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  memset(&aesd_device, 0, sizeof(struct aesd_dev));

  /* Minimal init: initialize mutex and circular buffer */
  mutex_init(&aesd_device.lock);
  aesd_circular_buffer_init(&aesd_device.circ_buf);

  aesd_device.write_buf = NULL;
  aesd_device.write_buf_size = 0;

  result = aesd_setup_cdev(&aesd_device);

  if (result) {
    unregister_chrdev_region(dev, 1);
  }
  return result;
}

void aesd_cleanup_module(void)
{
  dev_t devno = MKDEV(aesd_major, aesd_minor);

  cdev_del(&aesd_device.cdev);

  /* destroy mutex and free buffer contents */
  mutex_lock(&aesd_device.lock);
  if (aesd_device.write_buf) {
    kfree(aesd_device.write_buf);
    aesd_device.write_buf = NULL;
    aesd_device.write_buf_size = 0;
  }
  aesd_free_all_entries(&aesd_device.circ_buf);
  mutex_unlock(&aesd_device.lock);

  mutex_destroy(&aesd_device.lock);

  unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
