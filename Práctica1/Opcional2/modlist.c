#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/seq_file.h>	
#define PROCFS_MAX_SIZE 256
MODULE_LICENSE("GPL");

static struct proc_dir_entry *proc_entry; /* Para la entrada /proc */
static struct list_head mylist; /* Lista enlazada */
/* Nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};
static int contador;

//Añadir número a la lista
void add_number(int num)
{
    struct list_item *new_node=(struct list_item*) vmalloc((sizeof(struct list_item )));
  
    new_node->data = num;
    list_add_tail(&(new_node->links), &mylist);
contador++;
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
// -----------------------------
static void *mydrv_seq_start(struct seq_file *seq, loff_t *pos)
{
	printk(KERN_INFO "start:%d",contador);
	if(contador==*pos)		
		return NULL;
	else 
		return list_entry(mylist.next, struct list_item, links);


}

static void *mydrv_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *n = ((struct list_item *)v)->links.next;

	++*pos; /* Advance position */
	/* Return the next iterator, which is the next node in the list */
	return(n != &mylist) ?
	    list_entry(n, struct list_item, links) : NULL;
}

static int mydrv_seq_show(struct seq_file *seq, void *v)
{
	struct list_item *p = v;
	printk(KERN_INFO "show");
	/* Interpret the iterator, 'v' */


	seq_printf(seq,"%d\n",p->data);

	return 0;
}

static void mydrv_seq_stop(struct seq_file *seq, void *v)
{
	printk(KERN_INFO "stop");
}
/* Define iterator operations */
static struct seq_operations mydrv_seq_ops = {
	.start = mydrv_seq_start,
	.next  = mydrv_seq_next,
	.stop  = mydrv_seq_stop,
	.show  = mydrv_seq_show,
};

static int mydrv_seq_open(struct inode *inode, struct file *file)
{
	/* Register the operators */
	return seq_open(file, &mydrv_seq_ops);
}

//---------------------------------------------------------------------------------------------------------------
static const struct file_operations proc_entry_fops = {
    .write = modlist_write,   
	.open    = mydrv_seq_open, 
	.read    = seq_read,       
	.llseek  = seq_lseek,      
	.release = seq_release,   
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modlist_init(void)
{
    int ret = 0;
    contador=0;
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
