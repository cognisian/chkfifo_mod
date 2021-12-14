#include "chkfifo.h"

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pipe_fs_i.h>

#include <linux/proc_fs.h>

#include <linux/slab.h>
#include <linux/mutex.h>

/* Structures to get FIFO info */
struct block_device *bd;
struct super_block *sb;
struct inode *inode;
struct pipe_inode_info *pipe;

struct mutex fifo_lock;

/*
 * Retrieve the pipe information from the status data
 */
void
get_pipe_info(u64 inode_num, dev_t dev_num) {

	mutex_init(&fifo_lock);

	mutex_lock(&fifo_lock);

	/* Get the inode: get block device -> get device super block -> get inode from inode num */
	bd = bdget(dev_num);
	sb = get_super(bd);
	inode = iget_locked(sb, inode_num);

	/* Get the pipe info structure */
	mutex_lock(&inode->i_mutex);
	pipe = inode->i_pipe;
	if (!pipe) {

		pipe = (struct pipe_inode_info *)kzalloc(sizeof(struct pipe_inode_info), GFP_KERNEL);
		if (pipe) {
			printk(KERN_INFO "I am creating pipe_inode_info struct\n");
			init_waitqueue_head(&pipe->wait);
			pipe->r_counter = pipe->w_counter = 1;
			pipe->inode = inode;
		}
		else {
			pipe = NULL;
			printk(KERN_INFO "Unable to create pipe_inode_info struct\n");
		}
	}
	else {
		printk(KERN_INFO "I have existing pipe_inode_info struct\n");
	}
	mutex_unlock(&inode->i_mutex);
}

/*
 * Free pipe info structure
 * Code taken from free_pipe_info in fs/pipe.c
 */
void
let_go_pipe_info(void) {

	int i;

	for (i = 0; i < PIPE_DEF_BUFFERS; i++) {
		struct pipe_buffer *buf = pipe->bufs + i;
		if (buf->ops)
			buf->ops->release(pipe, buf);
	}

	if (pipe->tmp_page)
		__free_page(pipe->tmp_page);

	kfree(pipe);
	inode->i_pipe=NULL;

	iput(inode);
	drop_super(sb);
	bdput(bd);

	mutex_unlock(&fifo_lock);
}

/*
 */
void
extract_pipe_data(void *data) {

	struct proc_dir_entry *parent = NULL;

	struct proc_entry_data *pdata = NULL;

	/* Get the parent proc dir for this proc entry which contains the stat data */
	if (data == NULL) {
		printk (KERN_ALERT "Error: No data was passed via the proc entry\n");
		return;
	}

	parent = (struct proc_dir_entry *)data;
	if (parent == NULL) {
		printk (KERN_ALERT "Error: Data pointer was not present in the data field of proc entry\n");
		return;
	}

	pdata = (struct proc_entry_data *)parent->data;
	if (pdata != NULL) {
		get_pipe_info(pdata->ino, pdata->dev);
	}
	else {
		printk (KERN_ALERT "Error: No inode or device number data was present in proc entry\n");
		return;
	}
}

/*
 * Converts a num to a string and puts into buffer.  It will insert a new line character
 * and the null terminating character.  Returns the number of characters not
 * including the null terminator.
 */
unsigned int
insert_output(char *buffer, char * format, unsigned int num) {

	int temp = num;
	int digits = 0;

	size_t size = 0;

	int written;

	/* Convert num to string as no itoa func in kernel (add one for new line char)*/
	if (temp >= 10) {
		do {
			++digits;
		} while (temp /= 10);
	}
	else {
		digits = 1;
	}

	size = (digits + 2);

	written = snprintf(buffer, size, format, num);
	buffer[size] = '\0';

	return written;
}

/*
 * When user space reads the /proc/fifo entry then one of the following functions
 * are called depending on which entry was read by the user.
 *
 * buffer Data page in which to return data to userspace
 * buffer_location Start location of page.  Used when more than 4K to user
 * offset Offset into page.  Used when more than 4K to user
 * buffer_length Length of data (< 4K))
 * eof Set when all data is read.
 * data Private data
 */

/*
 * Get the overall status of the FIFO queue
 *
 * 0 - OK The FIFO queue is ready and rarin' to go
 * 1 - FIFO_FULL The FIFO is currently full
 * 2 - NO_READER There are no readers so an open on file will block
 */
unsigned int
read_status(char *buffer, char **buffer_location,
		off_t offset, int buffer_length, int *eof, void *data) {

	int length = 0;

	int check = CHK_OK;

	unsigned short bytes = 0;

	extract_pipe_data(data);
	if (pipe) {

		/* If there is an offset then we have finished reading
	  	    else send back the information */
		if (offset == 0) {

			spin_lock(&pipe->inode->i_lock);
				bytes = pipe->inode->i_bytes;
			spin_unlock(&pipe->inode->i_lock);

			/* Check if the queue is full */
			if (bytes >= MAX_FIFO_SIZE) {
				check |= CHK_FIFO_FULL;
			}

			/* Check if there is a reader (ie writer will not block on open) */
			if (pipe->readers == 0) {
				check |= CHK_NO_READER;
			}

			length = insert_output(buffer, "%u\n", check);
		}

		let_go_pipe_info();
	}
	else {
		printk (KERN_ALERT "Error: Unable to create pipe information\n");
	}

	return length;
}

/*
 * Get number of readers on FIFO
 */
unsigned int
read_readers(char *buffer, char **buffer_location,
		off_t offset, int buffer_length, int *eof, void *data) {

	int length = 0;

	extract_pipe_data(data);
	if (pipe) {

		/* If there is an offset then we have finished reading
	  	    else send back the information */
		if (offset == 0) {
			length = insert_output(buffer, "%u\n", pipe->readers);
		}

		let_go_pipe_info();
	}
	else {
		printk (KERN_ALERT "Error: Unable to create pipe information\n");
	}

	return length;
}

/*
 * Get number of writers on FIFO
 */
unsigned int
read_writers(char *buffer, char **buffer_location,
		off_t offset, int buffer_length, int *eof, void *data) {

	int length = 0;

	int writers = 0;

	extract_pipe_data(data);
	if (pipe) {

		/* If there is an offset then we have finished reading
	  	    else send back the information */
		if (offset == 0) {
			writers = pipe->writers + pipe->waiting_writers;
			length = insert_output(buffer, "%u\n", pipe->writers);
		}

		let_go_pipe_info();
	}
	else {
		printk (KERN_ALERT "Error: Unable to create pipe information\n");
	}

	return length;
}

/*
 * Get the number of bytes currently in FIFO
 */
unsigned int
read_size(char *buffer, char **buffer_location,
		off_t offset, int buffer_length, int *eof, void *data) {

	int length = 0;

	unsigned short bytes = 0;

	extract_pipe_data(data);
	if (pipe) {

		/* If there is an offset then we have finished reading
	  	    else send back the information */
		if (offset == 0) {

			spin_lock(&pipe->inode->i_lock);
				bytes = pipe->inode->i_bytes;
			spin_unlock(&pipe->inode->i_lock);
			bytes = i_size_read(inode);
			length = insert_output(buffer, "%u\n", bytes);
		}

		let_go_pipe_info();
	}
	else {
		printk (KERN_ALERT "Error: Unable to create pipe information\n");
	}

	return length;
}

/*
 * Get the octal permissions of the FIFO
 */
unsigned int
read_mode(char *buffer, char **buffer_location,
		off_t offset, int buffer_length, int *eof, void *data) {

	int length = 0;

	int mask = 0777;

	extract_pipe_data(data);
	if (pipe) {

		/* If there is an offset then we have finished reading
	  	    else send back the information */
		if (offset == 0) {
			length = insert_output(buffer, "%o\n", pipe->inode->i_mode & mask);
		}

		let_go_pipe_info();
	}
	else {
		printk (KERN_ALERT "Error: Unable to create pipe information\n");
	}

	return length;
}

