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
#include<linux/list.h>
#include<asm/current.h>
#include <linux/mutex.h>
#include <linux/fdtable.h>

#define DEVICE_NAME "cs614_device"
#define SYSFS_DIR_NAME "cs614_sysfs"
#define SYSFS_FILE_NAME "cs614_value"
#define PERMS 0660


MODULE_LICENSE("GPL");
MODULE_AUTHOR("bgupta@cse.iitk.ac.in");
MODULE_DESCRIPTION("Character device driver for CS614 assignment Part 3");

static DEFINE_MUTEX(my_mutex1);
static DEFINE_MUTEX(mutex_alloc);


struct device_data{

    atomic_t count_of_owners;
    int command;
    int has_written;
    pid_t tgid;
    struct list_head list;
    struct mutex lock;
};

struct thread_group_info{

    pid_t tgid;
    struct device_data* item;
    struct list_head list;

};

static struct list_head device_data_list;

static struct list_head process_info_list;

static int major;
static dev_t dev;
static struct cdev char_dev;
static struct class *demo_class;
struct device *demo_device;
atomic_t  device_opened;


static char *demo_devnode(struct device *dev, umode_t *mode)
{
        if (mode && dev->devt == MKDEV(major, 0))
                *mode = 0660;
        return NULL;
}

static struct device_data* allocate_device_data_object(pid_t tgid){


    struct thread_group_info * tg_info, * tg_temp;
    struct device_data* item = NULL;
    
    mutex_lock(&mutex_alloc);

    //to check whether a device object with tgid value has not been created during the wait time.

    list_for_each_entry_safe(tg_info, tg_temp, &process_info_list, list){

        if(tg_info->tgid == tgid){
            item = tg_info->item;
            break;
        }
    }

    if(!item){

        item = (struct device_data*)kmalloc(sizeof(*item), GFP_KERNEL);

        //if (!item)
        //    return -ENOMEM;

        item->command = -10;
        item->has_written = 0;
        item->tgid = tgid;
        atomic_set(&(item->count_of_owners),1);
        mutex_init(&item->lock);
        list_add_tail(&item->list, &device_data_list);

        
        tg_info = (struct thread_group_info*)kmalloc(sizeof(*tg_info), GFP_KERNEL);
        tg_info->tgid = tgid;
        tg_info->item = item;
        list_add_tail(&tg_info->list, &process_info_list);   

    }

    mutex_unlock(&mutex_alloc);

    return item;

}

static struct device_data* find_device_data_obj(pid_t tgid){

    struct device_data* item, *my_item, *temp;
    int flag = 0;

    list_for_each_entry_safe(item, temp, &device_data_list, list){

        if(item->tgid == tgid){

            my_item = item;
            atomic_inc(&(my_item->count_of_owners));
            flag = 1;
            break;
        }
    }

    if(flag == 1){

        return my_item;

    }
    else
        return allocate_device_data_object(tgid);

}


/* sysfs directory to create sysfs file to store command */
static struct kobject *cs614_kobj;


static ssize_t cs614_show(struct kobject *kobj, struct kobj_attribute *attr,
                         char *buff) {

    struct device_data* my_item;
    int current_command;
    struct task_struct* task;
    pid_t tgid;
    task  = current;
    tgid = task->tgid;

    my_item = find_device_data_obj(tgid);
    
    if(my_item->has_written == 0){

         return sprintf(buff, "Read before write\n");

    }
    current_command = my_item->command;
    
    return sprintf(buff, "%d\n", current_command);
    
}

static ssize_t cs614_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buff, size_t count) {
                           
    struct device_data* my_item;
    struct task_struct* task;
    int newval,err;
    pid_t tgid ;
    task  = current;
    tgid = task->tgid;
    
    my_item = find_device_data_obj(tgid);
    printk(KERN_INFO "value of myitem inside store = %p\n", my_item);
    printk(KERN_INFO "value of myitem inside store = %d\n", my_item->tgid);
    printk(KERN_INFO "value of myitem inside store = %d\n", my_item->command);
 

    err = kstrtoint(buff, 10, &newval);
    
    printk(KERN_INFO "value of err inside store = %d\n", err);

   
    mutex_lock(&my_item->lock);

    if (err || newval < 0 || newval > 7) {
        my_item->command = -1;
        my_item->has_written = 0;
        return -EINVAL;
    }

    my_item->command = newval;
    my_item->has_written = 1;

    mutex_unlock(&my_item->lock);
    
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
    
    struct device_data* my_item;
    my_item = find_device_data_obj(current->tgid);

    atomic_inc(&device_opened);
    try_module_get(THIS_MODULE);
    printk(KERN_INFO "Device opened successfully\n");
    return 0;

}

static int char_device_release(struct inode *inode, struct file *file) {

    struct device_data* my_item;
    my_item = find_device_data_obj(current->tgid);

    mutex_lock(&my_mutex1);

    if(atomic_dec_and_test(&(my_item->count_of_owners))){

        list_del(&(my_item->list));
        kfree(my_item);
    }

    atomic_dec(&device_opened);
    module_put(THIS_MODULE);
    printk(KERN_INFO "Device closed successfully\n");

    mutex_unlock(&my_mutex1);

    return 0;
}

static ssize_t char_device_read(struct file *filp, char *buf, size_t count,
                                loff_t *f_pos) {

    struct device_data* my_item;
    int ret = 0;
    int len = 0;
    int current_command;
    char str[1025];
    struct task_struct *task = current;

    printk(KERN_INFO "Inside device_read\n");
    my_item = find_device_data_obj(task->tgid);

    if(my_item->has_written == 0){

                return sprintf(buf, "Read before write\n");

    }

    current_command = my_item->command;

    if(my_item->has_written == 1)
    {
        switch (current_command) {
        
        case 0: //Pid 
            ret = sprintf(str, "%d", task->pid);
            break;
            
            
        case 1: // Static priority 
            ret = sprintf(str, "%d", task->static_prio);
            break;
            
            
        case 2: // process Name     
            ret = sprintf(str, "%s", task->comm);
            
            /* commmented out as it was not giving the right output

            char *pathname,*p;    
            struct mm_struct* mm;       
            mm = current->mm;
            if (mm)
            {
                mmap_read_lock(current->mm);
            if (mm->exe_file) {
                pathname = kmalloc(PATH_MAX, GFP_ATOMIC);
                      if (pathname) {
                          p = d_path(&mm->exe_file->f_path, pathname, PATH_MAX);
                    //Now you have the path name of exe in p
                      }
                }
               mmap_read_unlock(current->mm);
           }
           
            ret = sprintf(str, "%s", p);
           */

            break;
            
            
        case 3: // Ppid 
            ret = sprintf(str, "%d", task->real_parent->pid);
            break;
            
            
        case 4: // Number of voluntary context switches 
            ret = sprintf(str, "%lu", task->nvcsw);
            break;
            
       case 5: // Number of threads created by a process

                struct task_struct* utask = pid_task(find_vpid(current->tgid), PIDTYPE_PID);
                struct task_struct* thread;
                int t_count = 0;
                rcu_read_lock();
                for_each_thread(utask, thread) {
                    t_count++;
                 }
                rcu_read_unlock();            
                ret = sprintf(str, "%d", t_count);
            
                break;

       case 6: // Number of open files

            struct files_struct* myfile = task->files;
            struct fdtable *fdt;
            int fd_count = 0;
            fdt = files_fdtable(myfile);
              for (int i = 0; i < fdt->max_fds; i++) {
	        if (fdt->fd[i] != NULL){
      		   printk(KERN_INFO "fd[%d] = %d\n", i, i);
      		   fd_count ++;
      		   }
  	}
  	      ret = sprintf(str, "%d", fd_count);
            //ret = sprintf(str, "%d", atomic_read(&myfile->count));
            break;

      case 7: // pid of thread with highest consumption of user stack

            struct task_struct* task1;
            struct pt_regs* regs = task_pt_regs(task);
            unsigned long stack_ptr = regs->sp;
            unsigned long stack_use = stack_ptr - (task->mm->start_stack);
            unsigned long max = stack_use;
            pid_t max_usage_pid = task->pid;

            for_each_process(task1){

                if(task1->tgid == task->tgid){

                    regs = task_pt_regs(task1);
                    stack_ptr = regs->sp;
                    stack_use = stack_ptr - (task->mm->start_stack);
                    if(stack_use > max){
                        max = stack_use;
                        max_usage_pid = task1->pid;
                    }
                }
            }

            ret = sprintf(str, "%d", max_usage_pid);
            break;

        default:
            ret = sprintf(str, "Invalid Command\n");
            break;
        }
        
    }

    else
    {
        ret = sprintf(str, "Read before write\n");
    }

    len = strlen(str);
    if(len <= count)
    	count = len;
    printk(KERN_INFO "STR = %s\n", str);
    if(copy_to_user(buf, str, count ))
            return -EINVAL;

    printk(KERN_INFO "value of buf is: %s\n", buf); 
    return len;
}

static ssize_t char_device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
    
    struct device_data* my_item;

    int newval;
    int err;  
    printk(KERN_INFO "In write\n");
    printk(KERN_INFO "VALUE = %s\n", buff);

    my_item = find_device_data_obj(current->tgid);
                  
    err = kstrtoint(buff, 10, &newval);
        //printk(KERN_INFO "value of err inside store = %d\n", err);
          
    mutex_lock(&my_item->lock);

    if (err || newval < 0 || newval > 6) {
        my_item->command = -1;  
        my_item->has_written = 0;
        return -EINVAL;
    }

    my_item->command = newval;
    my_item->has_written = 1;

    mutex_unlock(&my_item->lock);

    return len;
}



static struct file_operations fops = {
        .read = char_device_read,
        .write = char_device_write,
        .open = char_device_open,
        .release = char_device_release,
};


int init_module(void){

    int ret = 0;
    printk(KERN_INFO "Hello kernel\n");

    INIT_LIST_HEAD(&device_data_list);
    INIT_LIST_HEAD(&process_info_list);

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
MODULE_DESCRIPTION("Character device driver for CS614 assignment Part 3");

