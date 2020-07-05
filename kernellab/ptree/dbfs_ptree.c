#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
struct debugfs_blob_wrapper blob;


void recursive_print(struct task_struct *curr)
{       //trace to pid 1, and print from there
        if (curr->pid != 1) recursive_print(curr->parent);

        //adjust blob size, save string output to blob
        //snprintf = (buffer, size, stringformat, string args)
        // stores formatting output string to buffer (blob.data), returns appended size
        blob.size += snprintf(blob.data + blob.size, 20000-blob.size, "%s (%d)\n", curr->comm, curr->pid);
}

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
        pid_t input_pid;
        
        //take input, put into task_struct
        sscanf(user_buffer, "%u", &input_pid);
        curr = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
        
        //init size with zero
        blob.size = 0;
        //print from top (pid 1) to bottom using common recursive call technique
        recursive_print(curr);

        return length;
}

static const struct file_operations dbfs_fops = {
        .write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{
        dir = debugfs_create_dir("ptree", NULL);
        
        if (!dir) {
                printk("Cannot create ptree dir\n");
                return -1;
        }
        inputdir = debugfs_create_file("input", 0444 , dir, NULL, &dbfs_fops);
        // read and write what's saved in blob
        ptreedir = debugfs_create_blob("ptree", 0666, dir, &blob);
	printk("dbfs_ptree module initialize done\n");
        
        // clear blob data in kernel
        char clear_blob[20000]; //note max blob size is assumed to 20000
	blob.data = clear_blob;

        return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);
	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);