#include "chkfifo.h"

#include <linux/proc_fs.h>

#include <linux/fs.h>
#include <linux/namei.h>

/* The /proc/fifo file structure structure */
static struct proc_dir_entry *fifo_dir;

/* List of the proc entries */
static char *fifo_prop[FIFO_PROP_COUNT] = {
		"status", "readers", "writers", "size", "mode"
};

/* List of the function to call in response to readin property */
static void *fifo_prop_func[FIFO_PROP_COUNT] = {
		&read_status, &read_readers, &read_writers,
		&read_size, &read_mode
};

/*
 * Helper function to ensure that named file exists and is a FIFO queue
 */
int
fifo_stat(const char *name, struct kstat *stat) {

	int error;

	struct	nameidata nd;

	error = path_lookup(name, 00, &nd);
	if (!error) {

		error = vfs_getattr(nd.path.mnt, nd.path.dentry, stat);
		if (!error && stat != NULL) {

		    if (!S_ISFIFO(stat->mode)) {
				error = -EPIPE;
		        printk(KERN_ALERT "Ignoring file: %s, not a fifo queue\n", name);
		    }
			else {
				printk(KERN_INFO "Monitoring FIFO queue: %s\n", name);
			}
		}
		else {
			error = -EIO;
			printk(KERN_ALERT "FIFO Queue %s not able to be stat'd, kernel error %i\n", name, error);
		}
	}
	else {
		error = -ENOENT;
		printk(KERN_ALERT "Ignoring FIFO queue: %s, does not exist\n", name);
	}

	return error;
}

/*
 * Recursively traverse the proc entry tree delete nodes and free any data
 */
void
delete_proc_entry(struct proc_dir_entry *entry) {

	struct proc_entry_data *proc_data;

	/* Depth first */
	if (entry->subdir != NULL) {
		delete_proc_entry(entry->subdir);
	}

	/* Then across */
	if (entry->next != NULL) {
		delete_proc_entry(entry->next);
	}

	/* This a leaf node with no siblings remove it */
	if (entry->subdir == NULL && entry->next == NULL) {

		/* Set data member to NULL on leaf as it has pointer to parent */
		if (entry->data != NULL) {
			if (entry->data == entry->parent) {
				entry->data = NULL;
			}
			else {
				proc_data = (struct proc_entry_data *)entry->data;
				kfree(proc_data->fifo_name);
				kfree(entry->data);
			}
		}

		printk(KERN_INFO "Removing proc entry %s under %s\n", entry->name, entry->parent->name);
		remove_proc_entry(entry->name, entry->parent);
	}
}

/*
 * Create the subdirectories and entries according to files being monitored
 */
int
create_proc_fifo_entries(struct proc_dir_entry *fifo_root, const char *fifo_name, const struct kstat *stat) {

	int i = 0;

	int fifo_name_len;
	char *temp = NULL;
	char **runner = NULL;

	char *dir = NULL;

	struct proc_entry_data proc_data;
	struct proc_dir_entry *parent = NULL;
	struct proc_dir_entry *fifo_prop_entry = NULL;

	char *name = NULL;

	/* Copy the fifo name to temp so runner does not move the fifo_name ptr */
	fifo_name_len = strlen(fifo_name);
	temp = (char *)kzalloc(fifo_name_len, GFP_KERNEL);
	if (!temp) {
		return -ENOMEM;
	}
	memcpy(temp, fifo_name, fifo_name_len);
	runner = &temp;

	/* Create the proc directories based on fifo_name dir and name */
	parent = fifo_root;
	while ((dir = strsep(runner, "/"))) {

		/* since absolute path (starts with /) first dir name will be empty */
		if (*dir != 0) {

			/* Do not want to duplicate subdirectories for FIFOs which exist in a
				common directory structure.  Check to see if the current subdir
				exists if not create it */
			if ((parent->subdir == NULL) ||
					(parent->subdir != NULL &&
					 (strcmp(parent->subdir->name, dir) != 0))) {

				parent = proc_mkdir(dir, parent);
				if (parent == NULL) {

					printk(KERN_INFO "Error: Could not create FIFO proc dir %s in %s\n",
							dir, fifo_root->name);
					kfree(temp);
					return 1;
				}
			}
			else {
				parent = parent->subdir;
			}
		}
	}

	kfree(temp);

	/* create the FIFO monitoring entries for fifo_name */
	if (parent != NULL) {

		/* Store this FIFO stat info in dir data associated with FIFO file name*/
		parent->data = kzalloc(sizeof(struct proc_entry_data), GFP_KERNEL);
		if (parent->data) {

			proc_data.ino = stat->ino;
			proc_data.dev = stat->dev;

			proc_data.fifo_name = (char *)kzalloc(strlen(fifo_name), GFP_KERNEL);
			name = memcpy(proc_data.fifo_name, fifo_name, strlen(fifo_name));

			memcpy(parent->data, &proc_data, sizeof(struct proc_entry_data));
		}

		/* Create each proc entry used by user to get values for a FIFO queue
		    being monitored */
		for (i = 0; i < FIFO_PROP_COUNT; i++) {

			fifo_prop_entry = create_proc_entry(fifo_prop[i], 0644, parent);
			if (fifo_prop_entry == NULL) {

				remove_proc_entry(fifo_prop[i], parent);
				printk(KERN_ALERT "Error: Could not create FIFO proc entry %s in dir %s\n",
						fifo_prop[i], parent->name);

				return 1;
			}

			/* Set the callback function when read and the full FIFO name with proc entry which
                gets passed in void *data parameter of callback */
			fifo_prop_entry->read_proc  = fifo_prop_func[i];
			//fifo_prop_entry->owner 	    = THIS_MODULE;
			fifo_prop_entry->mode       = S_IFREG | S_IRUGO;
			fifo_prop_entry->uid 	    = 0;
			fifo_prop_entry->gid 	    = 0;
			fifo_prop_entry->data 	    = parent; /* store pointer to parent proc dir so retrieve when read */
		}
	}

	return 0;
}

/*
 * MODULE ENTRY/EXIT POINTS
 */

/*#include <linux/unistd.h>
 * Loads the module.
 *
 * During loading, as this module ues the /proc filesystem its entry must be
 * created.  Additionally the module command line is parsed for the comma
 * delimited list of FIFO queues to monitor.
 */
static int __init init_chkfifo_mod(void) {

	int error;

	int fifo_count = 0;

	char *fifo = NULL;

	struct kstat stat;

	/* Create the fifo proc dir to contain all fifo queues */
	fifo_dir = proc_mkdir(PROCFS_FIFO_ROOT, 0);
	if (fifo_dir == NULL) {
		return -ENOMEM;
	}

	//fifo_dir->owner 	  = THIS_MODULE;
	fifo_dir->mode     = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	fifo_dir->uid 	   = 0;
	fifo_dir->gid 	   = 0;

	/* Process data from command line into the list of FIFOs to monitor */
	while ((fifo = strsep(&fifo_names, ","))) {

		/* Check if fifo exists and is a FIFO and if so create the /proc structure */
		error = fifo_stat(fifo, &stat);
		if (!error) {
			error = create_proc_fifo_entries(fifo_dir, fifo, &stat);
			if (!error) {
				fifo_count++;
			}
		}
	}

	/* Set the size ls sees to the number of FIFOs being monitored */
	fifo_dir->size = fifo_count;

	printk(KERN_INFO "FIFO Queue monitoring initialized\n");
	printk(KERN_INFO "Monitoring FIFO Queues\n");

	return 0;
}

/*
 * Unloads the module.
 *
 * During unloading, as this module ues the /proc filesystem its entry must be
 * removed.
 */
static void __exit exit_chkfifo_mod(void) {

	/* We pass in the subdir here as delete_proc_entry will traverse and delete all siblings
	    of current proc entry.  Passing the fifo_dir directly would then result in all siblings of
		fifo to also be deleted which is all the entries under /proc. VERY, VERY BAD */
	delete_proc_entry(fifo_dir->subdir);
	remove_proc_entry(fifo_dir->name, fifo_dir->parent);
	printk(KERN_INFO "FIFO Queue monitoring stopped\n");
}

/* Set KERNEL module entry/exit points */
module_init(init_chkfifo_mod);
module_exit(exit_chkfifo_mod);

/* Set KERNEL documentation */
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

