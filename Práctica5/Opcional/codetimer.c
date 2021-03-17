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
struct semaphore sem_coderead, sem_list, sem_num, sem_reader;                /* Semáforos para la lista y cola de espera */
 
struct work_struct codetimer_work;                      /* Work descriptor */

static struct list_head list_even, list_odd;            /* Lista enlazada */
struct list_item {                                      /* Nodos de la lista */
    char *data;
    struct list_head links;
};

unsigned int timer_period_ms=0;
char code_format[FORMAT_LEN+1];
unsigned int emergency_threshold=0;
unsigned int num_readers=0;
unsigned int even_odd=0;


void add_code(char *codigo, int len)
{
    struct list_item *new_node=(struct list_item*) vmalloc((sizeof(struct list_item )));
    new_node->data = vmalloc(len);
    sprintf(new_node->data, "%s", codigo);

    down(&sem_list);

    if(strlen(codigo) % 2 == 0)
        list_add_tail(&(new_node->links), &list_even);
    else 
        list_add_tail(&(new_node->links), &list_odd);
    up(&sem_list);
}


void cleanup_list(void)
{
    struct list_item *watch, *next;

    if(!list_empty(&list_even)) {
        list_for_each_entry_safe(watch, next, &list_even, links) {
        list_del(&watch->links);
            vfree(watch);
        }
    }
    if(!list_empty(&list_odd)) {
        list_for_each_entry_safe(watch, next, &list_odd, links) {
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


static void generate_format(void){
    unsigned int num, format_len;
    int i = 0;
    char *p = code_format;
    char c[3] = "0Aa";

    num = get_random_int();
    format_len = (num & 0x7ffff) % 8;

    for(i = 0; i < format_len; i++){
        int pos = ((num >> (i*2)) & 0xf) % 3;
        p += sprintf(p, "%c", c[pos]);
    }
}


static void generate_code(unsigned long data) 
{
    unsigned int num;
    char code[FORMAT_LEN+1];
    char* p = code;
    int i = 0;
    int code_len = 0;
    int cpu_actual = smp_processor_id();

    generate_format();
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
    	}
        else
            printk(KERN_INFO "No more space in buffer");
        
        if(kfifo_avail(&cbuffer) < (CBUF_SIZE * (100 - emergency_threshold) / 100)){
            /* Enqueue work */
    		cpu_actual = cpu_actual % 2 == 0 ? 1 : 0;
           	schedule_work_on(cpu_actual, &codetimer_work);
        }
    
    spin_unlock(&sp_cbuffer);

    /* Re-activate the timer one second from now */
    mod_timer( &(codetimer), jiffies + msecs_to_jiffies(timer_period_ms)); 
}


static ssize_t codeconfig_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	char command[COMMAND_SIZE];
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
    } else 
        return -EINVAL;

    *off+=len; 
    return len;
}


static ssize_t codeconfig_read(struct file *filp, char __user *buf, size_t len, loff_t *off) 
{
	char kbuf[CONFIG_OUTPUT_SIZE];
    int nr_bytes=0;

    nr_bytes = sprintf(kbuf, "timer_period_ms=%d\nemergency_threshold=%d\n", //code_format=%s\n",
             timer_period_ms, emergency_threshold); //, code_format);
     /* Tell the application that there is nothing left to read */
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

    /* Leer variable num_read */
    if (down_interruptible(&sem_num)){
        return -EINTR;
    }

    num_readers++;

    if (num_readers == 1){
        retval = kfifo_alloc(&cbuffer, CBUF_SIZE, GFP_KERNEL);
        if (retval) {
            kfifo_free(&cbuffer);
            return -ENOMEM;
        }
        
        INIT_LIST_HEAD(&list_even);
        INIT_LIST_HEAD(&list_odd);

        /* Create timer */
        init_timer(&codetimer);
        
        up(&sem_num);

        if (down_interruptible(&sem_reader)){
            down(&sem_num);
            num_readers--;
            up(&sem_num);
            return -EINTR;
        }
    } else {
        /* Esperar en la cola*/
        up(&sem_reader);
        while(num_readers != 2){
            up(&sem_num);

            if (down_interruptible(&sem_reader)){
                down(&sem_num);
                num_readers--;
                up(&sem_num);
                return -EINTR;
            }
            if (down_interruptible(&sem_num))
                return -EINTR;
        }
        /* Activate the timer for the first time */
        add_timer(&codetimer);
    }

    /* Sistema para el leactor lea la lista sin lector */
    file->private_data = even_odd;
    even_odd = even_odd + 1 % 2;
    up(&sem_num);   

    return 0;
}


static ssize_t codetimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off) 
{
	int nr_bytes=0;
    char kbuf[PROCFS_MAX_SIZE];
    char *p=kbuf;
    struct list_item *watch, *next;
    struct list_head *list_to_read;

    if(filp->private_data == 0)
        list_to_read = &list_even;
    else
        list_to_read = &list_odd;


    /* Entrar a la sección crítica */
    if(down_interruptible(&sem_list))
        return -EINTR;

    while(list_empty(list_to_read)) {
        /* Bloqueo en cola de espera */
        up(&sem_list);
        
        if (down_interruptible(&sem_coderead))
            return -EINTR;
        
        if(down_interruptible(&sem_list))
            return -EINTR;
    }
    
    /* Traversar lista para sacar cada codigo y liberar memoria */
    list_for_each_entry_safe(watch, next, list_to_read, links) {
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

    /* Leer variable num_readers */
    if (down_interruptible(&sem_num)){
        return -EINTR;
    }
        num_readers--;

    if(file->private_data == 0)
        even_odd=0;
    else
        even_odd=1;

    if(num_readers == 0){
        flush_work(&codetimer_work);
        kfifo_free(&cbuffer);
        cleanup_list();
    } else if (num_readers == 1)
        del_timer_sync(&codetimer);

    up(&sem_num);
    up(&sem_reader);
    
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
        sema_init(&sem_reader, 0);
        sema_init(&sem_list, 1);
        sema_init(&sem_num, 1);


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
	remove_proc_entry("codeconfig", NULL);
	remove_proc_entry("codetimer", NULL);
	printk(KERN_INFO "Codetimer: Modulo descargado.\n");
}


module_init( init_codetimer_module );
module_exit( cleanup_codetimer_module );