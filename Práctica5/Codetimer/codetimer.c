#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");

#define PROCFS_MAX_SIZE 256
#define COMMAND_SIZE 50
#define CONFIG_OUTPUT_SIZE 100
#define CBUF_SIZE 32
#define FORMAT_LEN 8

static struct proc_dir_entry *proc_entry_codeconfig;    /* Para la entrada /proc */
static struct proc_dir_entry *proc_entry_codetimer;     /* Para la entrada /proc */

struct timer_list codetimer;                            /* Structure that describes the kernel timer */
struct kfifo cbuffer;                                   /* Buffer circular */
struct spinlock sp_cbuffer;                             /* para garantizar Exclusión Mutua */
struct semaphore sem_coderead, sem_list;                /* Semáforos para la lista y cola de espera */

static struct workqueue_struct* codetimer_wq;           /* Workqueue descriptor */  
struct work_struct codetimer_work;                      /* Work descriptor */

static struct list_head mylist;                         /* Lista enlazada */
struct list_item {                                      /* Nodos de la lista */
    char *data;
    struct list_head links;
};

unsigned int timer_period_ms=0;
char code_format[FORMAT_LEN+1];
unsigned int emergency_threshold=0;


void add_code(char *codigo, int len)
{
    struct list_item *new_node=(struct list_item*) vmalloc((sizeof(struct list_item )));
    new_node->data = vmalloc(len);
    sprintf(new_node->data, "%s", codigo);

    down(&sem_list);
    list_add_tail(&(new_node->links), &mylist);
    up(&sem_list);
}


void cleanup_list(void)
{
    struct list_item *watch, *next;

    if(!list_empty(&mylist)) {
        list_for_each_entry_safe(watch, next, &mylist, links) {
        list_del(&watch->links);
            vfree(watch);
        }
    }
}


static void codetimer_wq_function( struct work_struct *work )
{
    unsigned long flags;
    char buf[CBUF_SIZE];
    char *code_p=buf;
    int len = 0;
    
    spin_lock_irqsave(&sp_cbuffer, flags);
    
        len = kfifo_out(&cbuffer,&buf, CBUF_SIZE);
        if(!kfifo_is_empty(&cbuffer))
            printk(KERN_INFO "There's something wrong here.");
    
    spin_unlock_irqrestore(&sp_cbuffer, flags);
    
    while(code_p - buf < len){
        add_code(code_p, strlen(code_p));
        code_p += strlen(code_p) + 1;
    }

    up(&sem_coderead);
}


static void generate_code(unsigned long data) 
{
    unsigned int num;
    char code[FORMAT_LEN+1];
    char* p = code;
    int i = 0;
    int code_len = 0; /* strlen(code) + strlen('\0') */
    int cpu_actual = smp_processor_id();

    num = get_random_int();

    for(i = 0; i < strlen(code_format); i++){
        if(code_format[i] == '0')
            p += sprintf(p, "%d", ((num >> (i*4) & 0xf) % 10));
        else if(code_format[i] == 'a')
            p += sprintf(p, "%c", ((num >> (i*4) & 0xf) + 97));
        else if(code_format[i] == 'A')
            p += sprintf(p, "%c", ((num >> (i*4) & 0xf) + 65));
    }
    printk(KERN_INFO "Generated code: %s", code);
    
    spin_lock(&sp_cbuffer);
    
        code_len = p - code + 1;
        if (kfifo_avail(&cbuffer) >= code_len){
    	    if(kfifo_in(&cbuffer,&code, code_len) != code_len)
    	        printk(KERN_INFO "There's something wrong here.");
    	} else
            printk(KERN_INFO "No more space in buffer");
        
        if(kfifo_avail(&cbuffer) < (CBUF_SIZE * (100 - emergency_threshold) / 100)){
            /* Enqueue work */
    		cpu_actual = cpu_actual % 2 == 0 ? 1 : 0;
           	queue_work_on(cpu_actual, codetimer_wq, &codetimer_work);
        }
    
    spin_unlock(&sp_cbuffer);

    /* Re-activate the timer one second from now */
    mod_timer( &(codetimer), jiffies + msecs_to_jiffies(timer_period_ms)); 
}


int valid_format(char *kbuf)
{
    int ret = 1;
    int i = 0;
    if (strlen(kbuf) > FORMAT_LEN)
        ret = 0;
    else {
        for(i = 0; i < strlen(kbuf); i++){
            if(kbuf[i] != '0' && kbuf[i] != 'a' && kbuf[i] != 'A')
                ret = 0;
        }
    }
    return ret;
}


static ssize_t codeconfig_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	char command[COMMAND_SIZE];
    char kbuf[FORMAT_LEN+1];
    int num;

    if(len > COMMAND_SIZE)
        return -EINVAL;

    if (copy_from_user(command, buf, len))
        return -EFAULT;

    if (sscanf(command, "timer_period_ms %d", &num)==1){
        if(num < 0)
            return -EINVAL;
        printk(KERN_INFO "Codetimer: Modificando periodo del temporizador a %d\n", num);
        timer_period_ms = num;

    } else if (sscanf(command, "emergency_threshold %d", &num)==1){
        if(num < 0 || num > 100)
            return -EINVAL;
        printk(KERN_INFO "Codetimer: Modificando periodo del threshold a %d\n", num);
        emergency_threshold = num;

    } else if ((sscanf(command, "code_format %s", kbuf)==1) && valid_format(kbuf)){
        kbuf[strlen(kbuf)] = '\0';
        sprintf(code_format, "%s", kbuf);
        printk(KERN_INFO "Codetimer: Modificando formato del codigo a %s\n", kbuf);
    } else 
        return -EINVAL;

    *off+=len; 
    return len;
}


static ssize_t codeconfig_read(struct file *filp, char __user *buf, size_t len, loff_t *off) 
{
	char kbuf[CONFIG_OUTPUT_SIZE];
    int nr_bytes=0;

    nr_bytes = sprintf(kbuf, "timer_period_ms=%d\nemergency_threshold=%d\ncode_format=%s\n",
             timer_period_ms, emergency_threshold, code_format);

    if ((*off) > 0)
        return 0;

    if (len<nr_bytes)
        return -ENOSPC;

    if (copy_to_user(buf, kbuf, nr_bytes)){
        printk(KERN_INFO "Codetimer: Error al copiar datos al usuario ");
        return -EINVAL;
    }
   
    (*off)+=len; 
    return nr_bytes; 
}


static int codetimer_open(struct inode *inodep, struct file *file) 
{
    int retval;
    
    try_module_get(THIS_MODULE);

    retval = kfifo_alloc(&cbuffer, CBUF_SIZE, GFP_KERNEL);
    if (retval) {
        kfifo_free(&cbuffer);
        return -ENOMEM;
    }

    /* Activate the timer for the first time */
    add_timer(&codetimer);
    
    INIT_LIST_HEAD(&mylist);

    /* Create timer */
    init_timer(&codetimer);
    return 0;
}


static ssize_t codetimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off) 
{
	int nr_bytes=0;
    char kbuf[PROCFS_MAX_SIZE];
    char *p=kbuf;
    struct list_item *watch, *next;
    
    /* Entrar a la sección crítica */
    if(down_interruptible(&sem_list))
        return -EINTR;

    while(list_empty(&mylist)) {
        /* Bloqueo en cola de espera */
        up(&sem_list);
        
        if (down_interruptible(&sem_coderead))
            return -EINTR;
        
        if(down_interruptible(&sem_list))
            return -EINTR;
    }
    
    /* Traversar lista para sacar cada codigo y liberar memoria */
    list_for_each_entry_safe(watch, next, &mylist, links) {
        p+=sprintf(p, "%s\n", watch->data);
        list_del(&watch->links);
        vfree(watch);
    }
    nr_bytes = p-kbuf;

    up(&sem_list);

    if (copy_to_user(buf, kbuf,nr_bytes))
        return -EINVAL;

    (*off)+=nr_bytes;
    return nr_bytes; 
}


static int codetimer_release(struct inode *inodep, struct file *file)
{
    flush_workqueue( codetimer_wq );
    kfifo_free(&cbuffer);
    del_timer_sync(&codetimer);
    cleanup_list();
    module_put(THIS_MODULE);

    return 0;
}


static const struct file_operations proc_entry_fops_codeconfig = {
    .write = codeconfig_write,
    .read = codeconfig_read,
};


static const struct file_operations proc_entry_fops_codetimer = {
    .open = codetimer_open,
    .read = codetimer_read,
    .release = codetimer_release
};


int init_codetimer_module( void )
{
	char format[FORMAT_LEN+1] = "0000AAA\0";	

	codetimer_wq = create_workqueue("codetimer_wq");

	if (!codetimer_wq)
		return -ENOMEM;

	proc_entry_codeconfig = proc_create( "codeconfig", 0666, NULL, &proc_entry_fops_codeconfig);
	proc_entry_codetimer = proc_create( "codetimer", 0666, NULL, &proc_entry_fops_codetimer);

	if (proc_entry_codeconfig == NULL) {
	    kfifo_free(&cbuffer);
	    return -ENOMEM;
	    printk(KERN_INFO "Codetimer: No se puede crear entrada /proc/codeconfig.\n");
	} else if (proc_entry_codetimer == NULL) {
	    kfifo_free(&cbuffer);
	    return -ENOMEM;
	    printk(KERN_INFO "Codetimer: No se puede crear entrada /proc/codetimer.\n");
	} else {
		sprintf(code_format, "%s", format);
        code_format[strlen(code_format)] = '\0';
        timer_period_ms=1000;
        emergency_threshold=75;

		/* Inicializacion de semáforos que permite acceso en exclusión mutua a la SC */
		spin_lock_init(&sp_cbuffer);
        sema_init(&sem_coderead, 0);
        sema_init(&sem_list, 1);

        /* Initialize work structure (with function) */
        INIT_WORK(&codetimer_work, codetimer_wq_function );

		/* Initialize field */
        codetimer.data=0;
        codetimer.function=generate_code;
        codetimer.expires=jiffies + msecs_to_jiffies(timer_period_ms);  /* Activate it one second from now */
        
	    printk(KERN_INFO "Codetimer: Modulo cargado con exito.\n");
	}
	return 0;
}


void cleanup_codetimer_module( void )
{
    destroy_workqueue( codetimer_wq );
	remove_proc_entry("codeconfig", NULL);
	remove_proc_entry("codetimer", NULL);
	printk(KERN_INFO "Codetimer: Modulo descargado.\n");
}


module_init( init_codetimer_module );
module_exit( cleanup_codetimer_module );