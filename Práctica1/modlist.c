#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#define PROCFS_MAX_SIZE 256
MODULE_LICENSE("GPL");

static struct proc_dir_entry *proc_entry; /* Para la entrada /proc */
static struct list_head mylist; /* Lista enlazada */
/* Nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};

//Añadir número a la lista
void add_number(int num)
{
    struct list_item *new_node=(struct list_item*) vmalloc((sizeof(struct list_item )));
  
    new_node->data = num;
    list_add_tail(&(new_node->links), &mylist);
}
		
//Añadir número a la lista
void remove_number(int num)
{
    struct list_item *watch, *next;    
    list_for_each_entry_safe(watch, next, &mylist, links) {
		if(watch->data==num) {	
		    list_del(&watch->links);
	            vfree(watch);
		}
    }
}

//Borrar toda la lista
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

static ssize_t read_list(char *p, char *kbuf)
{
    struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
    int ret=0;
    printk(KERN_INFO "Modlist: Leyendo datos de la lista ");
    list_for_each(cur_node , &mylist){
		item=list_entry(cur_node, struct list_item, links);
		p+=sprintf(p, "%d\n", item->data);
		ret=p-kbuf;
		//printk(KERN_INFO "Numero insertado %d",item->data);
	}
    return ret;
}

/* Función que se invoca cuando un sistema intenta escribir en la entrada /proc */
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    char command[20];
    int num=0;
    int nr_bytes_num=0;
    int nr_bytes_buf=0;
    char kbuf[PROCFS_MAX_SIZE];
    char* p=kbuf;

    if (copy_from_user(command, buf, len))
        return -EFAULT;

    if(sscanf(command, "add %d", &num)==1){
        /* Añadir nodo */
        /* Comprobar tamaño del buffer */
		p+=sprintf(p, "%d\n", num);
		nr_bytes_num=p-kbuf;
		printk(KERN_INFO "Modlist: Size of num %d\n", nr_bytes_num);
		p=kbuf;
		nr_bytes_buf=read_list(p, kbuf);
		if(nr_bytes_buf+nr_bytes_num >= PROCFS_MAX_SIZE){
		    printk("Modlist: No hay mas memoria para escribir");
		    return -ENOSPC;
		}

	    printk(KERN_INFO "Modlist: Añadiendo el valor %d\n", num);
	    add_number(num);

    } else if (sscanf(command, "remove %d", &num)==1){
	/* Eliminar nodo */
		printk(KERN_INFO "Modlist: Eliminando el valor %d\n", num);
		if(list_empty(&mylist)){
		    printk(KERN_INFO "Modlist: Lista vacia");
		    return -EINVAL;
	    }

	remove_number(num);

    } else if(sscanf(command, "cleanup")==0) {
		printk(KERN_INFO "Modlist: Borrando lista entera");
		cleanup_list();
    } else	
	return -EINVAL;

    *off+=len; 

    return len;
}

/* Función que se invoca cuando un sistema intenta leer desde la entrada /proc */
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) 
{
    char kbuf[PROCFS_MAX_SIZE];
    char* p=kbuf;
    int nr_bytes=0;

    nr_bytes=(read_list(p, kbuf));
     /* Tell the application that there is nothing left to read */
    if ((*off) > 0)
        return 0;
    
    /* Not enough space in the buffer where to copy data */
    if (len<nr_bytes)
        return -ENOSPC;

    /* Transfer data from the kernel to userspace */  
    if (copy_to_user(buf, kbuf,nr_bytes)){
        printk(KERN_INFO "Modlist: Error al copiar datos al usuario ");
        return -EINVAL;
    }
   
    (*off)+=len;  /* Update the file pointer */
 
    printk(KERN_INFO "Numero de bytes %d",nr_bytes);
    return nr_bytes; 
}

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modlist_init(void)
{
    int ret = 0;
    proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
	ret = -ENOMEM;
	printk(KERN_INFO "Modlist: No se puede crear entrada /proc.\n");
    } else {
	// Inilizar inicio de lista
	INIT_LIST_HEAD(&mylist);
	printk(KERN_INFO "Modlist: Modulo cargado con exito.\n");
    }

    /* Devolver 0 para indicar una carga correcta del módulo */
    return ret;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modlist_clean(void)
{
    cleanup_list();
    remove_proc_entry("modlist", NULL);
    printk(KERN_INFO "Modlist: Modulo descargado.\n");
}

/* Declaracion de funciones init y cleanup */
module_init(modlist_init);
module_exit(modlist_clean);
