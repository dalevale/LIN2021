#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>

#define MAX_CHARS_KBUF 20

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Práctica Final LIN 2020-2021");
MODULE_AUTHOR("Dale Francis Valencia Calicdan");

static short int max_entries = 5;
static short int max_size = 32;
module_param(max_entries, short, 0000);
MODULE_PARM_DESC(max_entries, "Número maximo de entradas.");
module_param(max_size, short, 0000);
MODULE_PARM_DESC(max_size, "Número maximo de elementos en el buffer circular.");


static unsigned int num_of_process = 1;
struct proc_dir_entry *multipc=NULL;
static struct proc_dir_entry *proc_entry;
static struct list_head proc_list; 			/* Lista enlazada */
struct list_item {							/* Nodos de la lista */
	unsigned int proc_num;					/* Numero identificador de la entrada /proc */
    char *name;								/* Nombre de la entrada /proc */
	char *type;								/* Tipo del buffer circular */
	struct kfifo cbuf; 						/* Buffer circular compartido */
	struct semaphore elementos,huecos; 		/* Semaforos para productor y consumidor */
	struct semaphore mtx; 					/* Para garantizar exclusión mutua en acceso a buffer */
    struct list_head links;
};
struct semaphore mtx_num_process;			/* Para garantizar exclusión mutua en acceso al variable num_of_process */

void cleanup_buffer(struct kfifo *cbuf)
{
	void *val= NULL;
	int numbytes = 0;

	if(kfifo_is_empty(cbuf))
		return;
	while(!kfifo_is_empty(cbuf)){
		numbytes += kfifo_out(cbuf,&val,sizeof(void *));
		vfree(val);
	}
	printk(KERN_INFO "multipc: Eliminando %d bytes del buffer circular.", (int) numbytes);
}


void cleanup_list(void)
{
    struct list_item *watch, *next;


    if(!list_empty(&proc_list)) {
        list_for_each_entry_safe(watch, next, &proc_list, links) {
        	printk(KERN_INFO "multipc: Eliminando la entrada %s.", watch->name);
	    	remove_proc_entry(watch->name, multipc);
	    	cleanup_buffer(&watch->cbuf);
	    	kfifo_free(&watch->cbuf);
	    	vfree(watch->name);
	    	list_del(&watch->links);
            vfree(watch);
    	}
    }
}


int valid_num(char *val)
{
	int ret = 0;
	int i = 0;

	while(i < strlen(val)){
		if(!isdigit(val[i])){
			return -1;
		}
		ret*= 10;
		ret+= val[i] - '0';
		i++;
	}
	return ret ;
}


static int test_open(struct inode *inodep, struct file *file) 
{
    try_module_get(THIS_MODULE);
    return 0;
}


static ssize_t test_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{

	struct list_item *proc_entry = (struct list_item *) PDE_DATA(filp->f_inode);
	char kbuf[MAX_CHARS_KBUF+1];
	int num=0;
	void *val= NULL;

	//printk(KERN_INFO "multipc: Escribiendo en proc numero %d", proc_entry->proc_num);

	if ((*off) > 0) /* The application can write in this entry just once !! */
		return 0;

	if (len > MAX_CHARS_KBUF) {
		return -ENOSPC;
	}
	if (copy_from_user( kbuf, buf, len )) {
		return -EFAULT;
	}

	kbuf[len-1] ='\0';
	*off+=len;            /* Update the file pointer */
	
	printk(KERN_INFO "multipc/%s: %s", proc_entry->name, kbuf);
	if(!strcmp(proc_entry->type, "i") && ((num = valid_num(kbuf)) >= 0)){
		val = vmalloc(sizeof(int *));
		*(int *)val = num;
	}
	else if (!strcmp(proc_entry->type, "s")){	
		val = vmalloc(sizeof(char *));
		sprintf((char *)val , "%s", kbuf);
	}
	else
		return -EINVAL;

	/* Bloqueo hasta que haya huecos */
	if (down_interruptible(&proc_entry->huecos))
		return -EINTR;

	/* Entrar a la SC */
	if (down_interruptible(&proc_entry->mtx)) {
		up(&proc_entry->huecos);
		return -EINTR;
	}

	/* Inserción segura en el buffer circular */
	kfifo_in(&proc_entry->cbuf,&val,sizeof(void *));

	/* Salir de la SC */
	up(&proc_entry->mtx);

	/* Incremento del número de elementos (reflejado en el semáforo) */
	up(&proc_entry->elementos);
	return len;
}

static ssize_t test_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	struct list_item *proc_entry = (struct list_item *) PDE_DATA(filp->f_inode);
	int nr_bytes=0;
	int bytes_extracted = 0;
	char kbuff[MAX_CHARS_KBUF];
	void *val;

	//printk(KERN_INFO "multipc: Leyendo en proc numero %d", proc_entry->proc_num);

	if ((*off) > 0)
		return 0;

	/* Bloqueo hasta que haya elementos que consumir */
	if (down_interruptible(&proc_entry->elementos))
		return -EINTR;

	/* Entrar a la SC  */
	if (down_interruptible(&proc_entry->mtx)) {
		up(&proc_entry->elementos);
		return -EINTR;
	}
	
	/* Extraer el primer entero del buffer */
	bytes_extracted=kfifo_out(&proc_entry->cbuf,&val,sizeof(void *));

	/* Salir de la SC */
	up(&proc_entry->mtx);

	/* Incremento del número de huecos (reflejado en el semáforo) */
	up(&proc_entry->huecos);

	if(!strcmp(proc_entry->type, "i"))
		nr_bytes=sprintf(kbuff,"%i\n", *(int*)val);
	else if(!strcmp(proc_entry->type, "s"))
		nr_bytes=sprintf(kbuff, "%s\n", (char*)val);

	vfree(val);

	if (bytes_extracted!=sizeof(void*))
		return -EINVAL;

	if (len < nr_bytes)
		return -ENOSPC;

	if (copy_to_user(buf,kbuff,nr_bytes))
		return -EINVAL;

	(*off)+=nr_bytes; 

	return nr_bytes;
}


static int test_release(struct inode *inodep, struct file *file)
{
    module_put(THIS_MODULE);
    return 0;
}


static const struct file_operations proc_entry_fops_test = {
	.open = test_open,
	.read = test_read,
	.write = test_write,
	.release = test_release,
};


int add_proc(char *name, char *type, int num){
	struct proc_dir_entry *proc_entry=NULL;		
	struct list_item *new_node=(struct list_item*) vmalloc(sizeof(struct list_item ));
	
	proc_entry = proc_create_data(name, 0666, multipc, &proc_entry_fops_test, new_node);

	if (proc_entry == NULL) {
		printk(KERN_INFO "multipc: No se puede crear la entrada \"%s\" en proc\n", name);
		vfree(new_node);
		return  -ENOMEM;
	} else if (kfifo_alloc(&new_node->cbuf, max_size*sizeof(void *),GFP_KERNEL)){		
		remove_proc_entry(name, multipc);
		vfree(new_node);
		return -ENOMEM;
	}	
	
	/* Semaforo elementos inicializado a 0 (buffer vacío) */
	sema_init(&new_node->elementos,0);

	/* Semaforo huecos inicializado a max_size (buffer vacío) */
	sema_init(&new_node->huecos,max_size);

	/* Semaforo para garantizar exclusion mutua */
	sema_init(&new_node->mtx,1);

	new_node->proc_num = num;
	new_node->name=vmalloc(strlen(name)+1);
	new_node->type=vmalloc(strlen(type)+1);
	sprintf(new_node->name, "%s", name);
	sprintf(new_node->type, "%s", type);
	list_add(&(new_node->links), &proc_list);

	return 0;
}


int delete_proc(char *s)
{
   struct list_item *watch, *next;

    if(!list_empty(&proc_list)) {
    list_for_each_entry_safe(watch, next, &proc_list, links) {
    	if(!strcmp(watch->name, s)) {
	    	remove_proc_entry(watch->name, multipc);
	    	cleanup_buffer(&watch->cbuf);
	    	kfifo_free(&watch->cbuf);
	    	vfree(watch->name);
	    	list_del(&watch->links);
            vfree(watch);
            break;
    	}
    }
    }

    return 0;
}


static int admin_open(struct inode *inodep, struct file *file) 
{
    try_module_get(THIS_MODULE);
    return 0;
}


static ssize_t admin_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[MAX_CHARS_KBUF+1];
	char name[MAX_CHARS_KBUF+1];
	char type[MAX_CHARS_KBUF+1];
	int ret=0;
	int num=0;

	if ((*off) > 0)
		return 0;

	if (len > MAX_CHARS_KBUF)
		return -ENOSPC;

	if (copy_from_user( kbuf, buf, len ))
		return -EFAULT;
	kbuf[len] ='\0';
	
	if(sscanf(kbuf, "new %s %s", name, type)==2){
			
		if (down_interruptible(&mtx_num_process))
			return -EINTR;

		if(num_of_process > max_entries){
			printk(KERN_INFO "multipc: No se puede hacer mas de %d entradas", max_entries);
			up(&mtx_num_process);
			return -EINVAL;
		}
		num=num_of_process++;
		up(&mtx_num_process);

		printk(KERN_INFO "multipc: Creando la entrada %s de tipo %s.", name, type);
		ret = add_proc(name, type, num);
		
		if(ret != 0){			
			if (down_interruptible(&mtx_num_process))
				return -EINTR;
			num_of_process--;
			up(&mtx_num_process);
			
			return ret;
		}

	} else if(sscanf(kbuf, "delete %s", name)==1){
		printk(KERN_INFO "multipc: Eliminando la entrada %s.", name);
		ret = delete_proc(name);
		
		if(ret != 0)
			return ret;

		if (down_interruptible(&mtx_num_process))
			return -EINTR;
		num_of_process--;
		up(&mtx_num_process);
	}

	*off+=len;
	return len;
}


static int admin_release(struct inode *inodep, struct file *file)
{
    module_put(THIS_MODULE);
    return 0;
}


static const struct file_operations proc_entry_fops_admin = {
	.open = admin_open,
	.write = admin_write,
	.release = admin_release,
};


int init_prodcons_module( void )
{	
	int ret = 0;
    
	if(max_entries < 1){
		printk(KERN_INFO "multipc: max_entries tiene que ser mayor que 1.");
		return -EINVAL;   
	}
	if((max_size < 1) || ((max_size &(max_size - 1)) != 0)){
  		printk(KERN_INFO "multipc: max_size ha de ser potencia de 2.");
  		return -EINVAL;
	}

    printk(KERN_INFO "multipc: Número maximo de entradas establecido: %hd\n", max_entries);
    printk(KERN_INFO "multipc: Número de bytes establecido para el buffer circular: %hd\n", max_size);

    multipc=proc_mkdir("multipc",NULL);
    if (!multipc){
    	return -ENOMEM;    
    }

	proc_entry = proc_create_data("admin",0666, multipc, &proc_entry_fops_admin, NULL);

	if (proc_entry == NULL) {
		remove_proc_entry("multipc", NULL);
		printk(KERN_INFO "multipc: No se puede crear la entrada \"admin\" en proc\n");
		return  -ENOMEM;
	}

	INIT_LIST_HEAD(&proc_list);
	
	/* Semaforo para garantizar exclusion mutua */
	sema_init(&mtx_num_process,1);

	ret = add_proc("test", "i", num_of_process++);
	if(ret != 0)
		return ret;
	printk(KERN_INFO "multipc: Cargado el Modulo.\n");

	return 0;
}


void exit_prodcons_module( void )
{
	cleanup_list();
	remove_proc_entry("admin", multipc);
	remove_proc_entry("multipc", NULL);
	printk(KERN_INFO "multipc: Modulo descargado.\n");
}


module_init( init_prodcons_module );
module_exit( exit_prodcons_module );
