#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/mm.h>
#include<linux/mm_types.h>
#include<linux/file.h>
#include<linux/fs.h>
#include<linux/path.h>
#include<linux/slab.h>
#include<linux/dcache.h>
#include<linux/sched.h>
#include<linux/uaccess.h>
#include<linux/fs_struct.h>
#include <asm/tlbflush.h>
#include<linux/uaccess.h>
#include<linux/sched/task_stack.h>
#include<linux/ptrace.h>
#include<linux/device.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/sysfs.h>



#define DEVICE_NAME "cs614_device"
#define SYSFS_DIR_NAME "cs614_sysfs"
#define SYSFS_FILE_NAME "cs614_value"
#define PERMS 0660


MODULE_LICENSE("GPL");
MODULE_AUTHOR("bgupta@cse.iitk.ac.in");
MODULE_DESCRIPTION("Character device driver for CS614 assignment Part 1");


static int major;
static int has_written;
static dev_t dev;
static struct cdev char_dev;
static struct class *demo_class;
struct device *demo_device;
static int command = -10;
atomic_t  device_opened;


static char *demo_devnode(struct device *dev, umode_t *mode)
{
        if (mode && dev->devt == MKDEV(major, 0))
                *mode = 0660;
        return NULL;
}

//addG

/* sysfs directory to create sysfs file to store command */
static struct kobject *cs614_kobj;
static ssize_t cs614_show(struct kobject *kobj, struct kobj_attribute *attr,
                         char *buff) {
    return sprintf(buff, "%d\n", command);
}

static ssize_t cs614_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buff, size_t count) {
                           
  
	int newval;
	int err = kstrtoint(buff, 10, &newval);
	printk(KERN_INFO "value of err inside store = %d\n", err);

    // validate command 
    if (err || newval < 0 || newval > 6) {
        command = -1;	
        has_written = 0;
        // to include a check in read / write for this value of command.
        return -EINVAL;
    }

    command = newval;
    has_written = 1;
    
    return count;
    
}

static struct kobj_attribute cs614_attribute =
    __ATTR(cs614_value, PERMS, cs614_show, cs614_store);

static struct attribute *cs614_attrs[] = {
        &cs614_attribute.attr,
        NULL,
};

static struct attribute_group cs614_attr_group = {
        .attrs = cs614_attrs,
};


static int char_device_open(struct inode *inode, struct file *file) {

    atomic_inc(&device_opened);
    try_module_get(THIS_MODULE);
    has_written = 0;
    printk(KERN_INFO "Device opened successfully\n");
    return 0;
}

static int char_device_release(struct inode *inode, struct file *file) {

    atomic_dec(&device_opened);
    module_put(THIS_MODULE);
    printk(KERN_INFO "Device closed successfully\n");
    return 0;
}

static ssize_t char_device_read(struct file *filp, char *buf, size_t count,
                                loff_t *f_pos) {
    int ret = 0;
    int len = 0;
    char str[1025];
    struct task_struct *task = current;

    if(has_written == 1)
	{
	    switch (command) {
	    
		case 0: //Pid 
		    ret = sprintf(str, "%d", task->pid);
		    break;
		    
		    
		case 1: // Static priority 
		    ret = sprintf(str, "%d", task->static_prio);
		    break;
		    
		    
		case 2: // Command Name     
		    //ret = sprintf(str, "%s", task->comm);
		    char *pathname,*p;    
		    struct mm_struct* mm;       
		    mm = current->mm;
		    if (mm)
		    {
	    		mmap_read_lock(current->mm);
			if (mm->exe_file) {
		        pathname = kmalloc(PATH_MAX, GFP_ATOMIC);
		              if (pathname) {
		                  p = d_path(&mm->exe_file->f_path, pathname, 			PATH_MAX);
		            /*Now you have the path name of exe in p*/
		              }
		        }
		       mmap_read_unlock(current->mm);
		   }
		   
		    ret = sprintf(str, "%s", p);
		   
		    break;
		    
		    
		case 3: // Ppid 
		    ret = sprintf(str, "%d", task->real_parent->pid);
		    break;
		    
		    
		case 4: // Number of voluntary context switches 
		    ret = sprintf(str, "%lu", task->nvcsw);
		    break;
		    
		    
		default:
		    ret = sprintf(str, "Invalid Command\n");
		    break;
		}

		len = strlen(str);
		if(len <= count)
    			count = len;
		printk(KERN_INFO "STR = %s\n", str);
		if(copy_to_user(buf, str, count))
			return -EINVAL;
		
    }

    else
    {
		ret = sprintf(str, "Read before write\n");
		len = strlen(str);
		printk(KERN_INFO "STR = %s\n", str);
		if(copy_to_user(buf, str, len))
			return -EINVAL;
		
	
    }
	
    return len;
}

static ssize_t char_device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	  
	int newval;
	int err;  
        printk(KERN_INFO "In write\n");
        printk(KERN_INFO "VALUE = %s\n", buff);
            
          
	err = kstrtoint(buff, 10, &newval);
        //printk(KERN_INFO "value of err inside store = %d\n", err);
          
    if (err || newval < 0 || newval > 6) {
        command = -1;	
        has_written = 0;
        return -EINVAL;
    }

    command = newval;
    has_written = 1;

    return len;
}



//addG

static struct file_operations fops = {
        .read = char_device_read,
        .write = char_device_write,
        .open = char_device_open,
        .release = char_device_release,
};

//addG

int init_module(void){

    int ret = 0;
    printk(KERN_INFO "Hello kernel\n");
	
    if((alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)) <0){
            pr_info("Cannot allocate major number\n");
            return -1;
    }
    printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
    cdev_init(&char_dev,&fops);
    
    if((cdev_add(&char_dev,dev,1)) < 0){
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
    }
    
    if(IS_ERR(demo_class = class_create(THIS_MODULE,DEVICE_NAME))){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
    }
    
    demo_class->devnode = demo_devnode;
    
    major = MAJOR(dev);    
    if(IS_ERR(demo_device = device_create(demo_class,NULL,MKDEV(major,0),NULL,DEVICE_NAME))){
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
    }
    
    
        
    /*
    
    	int err;
	int ret = 0; 
	
            
    major = register_chrdev(0, DEVICE_NAME, &fops);
    err = major;
    if (err < 0) {      
         printk(KERN_ALERT "Registering char device failed with %d\n", major);   
         goto error_regdev;
    }                 
    
    demo_class = class_create(THIS_MODULE, DEVICE_NAME);
    err = PTR_ERR(demo_class);
    if (IS_ERR(demo_class))
        goto error_class;

    demo_class->devnode = demo_devnode;

    demo_device = device_create(demo_class, NULL,
                                        MKDEV(major, 0),
                                        NULL, DEVICE_NAME);
    err = PTR_ERR(demo_device);
    if (IS_ERR(demo_device))
        goto error_device;
 
    */
    
    cs614_kobj = kobject_create_and_add("cs614_sysfs",kernel_kobj);

    
    ret = sysfs_create_group (cs614_kobj, &cs614_attr_group);
    if(unlikely(ret))
        printk(KERN_INFO "demo: can't create sysfs\n");

    printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));                                                           
    atomic_set(&device_opened, 0);

	return 0;

/*
error_device:
         class_destroy(demo_class);
         
error_class:
        unregister_chrdev(major, DEVICE_NAME);
error_regdev:
        return  err;

*/  
    
r_device:
	class_destroy(demo_class);
        
r_class:
        unregister_chrdev_region(dev,1);
        cdev_del(&char_dev);
        return -1;

}


void cleanup_module(void)
{

    
    sysfs_remove_group (cs614_kobj, &cs614_attr_group);
    kobject_put(cs614_kobj); 
    device_destroy(demo_class,dev);
    class_destroy(demo_class);
    cdev_del(&char_dev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "Removed the device driver module\n");

}

MODULE_AUTHOR("bgupta@cse.iitk.ac.in");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device driver for CS614 assignment Part 1");

