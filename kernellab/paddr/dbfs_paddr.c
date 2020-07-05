#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

struct packet { //take structure same as app.c packet structure
        pid_t pid;
        unsigned long vaddr;
        unsigned long paddr;
};

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
        // prepare five table procedures
        // intel's x86 supports five-level page table
        pgd_t *pgd;
        pud_t *pud;
        p4d_t *p4d;
        pmd_t *pmd;
        pte_t *pte;

        struct packet pckt; // same as app.c
        /*
        take input from app.c in form of:
        pckt.pid = getpid();
        pckt.vaddr = (unsigned long)mmap(NULL, sizeof(unsigned long), PROT_READ | PROT_WRITE, MAP_SHARED, mem, PADDR);
        pckt.paddr = 0;
        */
        copy_from_user(&pckt, user_buffer, length); // copy as given
        task = pid_task(find_vpid(pckt.pid), PIDTYPE_PID);
        
        //recursively find pte struct
        //struct mm_struct *pgdtop = task->mm;
        pgd = pgd_offset(task->mm, pckt.vaddr);
        // besides main four pages, also need p4d for x86
        p4d = p4d_offset(pgd, pckt.vaddr);
        pud = pud_offset(p4d, pckt.vaddr);
        pmd = pmd_offset(pud, pckt.vaddr);
        pte  = pte_offset_kernel(pmd, pckt.vaddr);

         //physical address is made of 52 bits/ 13 bytes
        // lower 12 bits (3 bytes) are from vaddr's lower 12 bits
        unsigned long offset = pckt.vaddr & 0x000000000fff; // take lower 12 bits = page offset
        //pte = 8 bytes, with 40-bit physical address base address
        unsigned long base_address = pte->pte & 0xffffffffff000; //take base address
        //combine to offset and base address for physical address
        pckt.paddr = offset | base_address; 

        copy_to_user(user_buffer, &pckt, length);
}

static const struct file_operations dbfs_fops = {
        .read = read_output,
};

static int __init dbfs_module_init(void)
{
        // Implement init module


        dir = debugfs_create_dir("paddr", NULL);

        if (!dir) {
                printk("Cannot create paddr dir\n");
                return -1;
        }

        output = debugfs_create_file("output", 0444, dir, NULL, &dbfs_fops);
	printk("dbfs_paddr module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
        // Implement exit module
        debugfs_remove_recursive(dir);
	printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
