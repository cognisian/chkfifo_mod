#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>

#define DRIVER_AUTHOR "Sean Chalmers <schalmers@symcor.com>"
#define DRIVER_DESC   "FIFO Queue monitor"

#define PROCFS_FIFO_ROOT "fifo"

#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,10) )
	#define MAX_FIFO_SIZE PIPE_SIZE
	#define MAX_ATOMIC_WRITE PIPE_BUF
#else
	#define MAX_FIFO_SIZE PIPE_SIZE
	#define MAX_ATOMIC_WRITE PIPE_BUF
#endif

/* Define Status codes for FIFO /proc status */
#define CHK_OK 0
#define CHK_FIFO_FULL 1
#define CHK_NO_READER 2

/* FIFO Properties being monitored */
#define FIFO_PROP_COUNT 5

/* An array of FIFO queue names from command line */
static char *fifo_names;

/* Get module parameters */
module_param(fifo_names, charp, 0000);
MODULE_PARM_DESC(fifo_names, "An comma seperated list of FIFO queues to monitor");

/* We need the device and inode numbers of the FIFO to get the pipe status */
struct proc_entry_data {
	u64 ino;
	dev_t dev;
	char *fifo_name;
};

/* Functions called when proc entry read */
/* THESE FUNCTIONS AND FOLLOWING ARRAY OF FUNCTION POINTERS MUST BE KEPT IN
    SYNC WITH THE ARRAY OF PROPERTIES fifo_prop */
unsigned int read_status(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
unsigned int read_readers(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
unsigned int read_writers(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
unsigned int read_size(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);
unsigned int read_mode(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data);

