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

#define CBUF_SIZE 256
#define KBUF_SIZE 256


static struct proc_dir_entry *proc_entry;
struct kfifo cbuffer;	/* Buffer circular */
int prod_count = 0;		/* Número de procesos que abrieron la entrada /proc para 
	escritura (productores) */ 
int cons_count = 0;		/* Número de procesos que abrieron la entrada /proc para 
	lectura (consumidores) */
struct semaphore mtx;		/* para garantizar Exclusión Mutua */
struct semaphore sem_prod;	/* cola de espera para productor(es) */
struct semaphore sem_cons;	/* cola de espera para consumidor(es) */
int nr_prod_waiting=0;		/* Número de procesos productores esperando */
int nr_cons_waiting=0;		/* Número de procesos consumidores esperando */


static int fifoproc_open(struct inode *inodep, struct file *file) 
{
	/* Entrar a la sección crítica */
	if (down_interruptible(&mtx)) 
		return -EINTR;

	if (file->f_mode & FMODE_READ) {
		/* Un consumidor abrió el FIFO */
		cons_count++;
		up(&sem_prod);

		/* Esperar a que haya un productor si no hay ninguno. */
		while(prod_count==0) { 
			nr_cons_waiting++;
			/* Liberar el 'mutex' antes de bloqueo*/
			up(&mtx);
			
			if (down_interruptible(&sem_cons)){
				down(&mtx);
				nr_cons_waiting--;
				cons_count--;
				up(&sem_prod);
				up(&mtx);
				return -EINTR;
			}
			
			if (down_interruptible(&mtx)) 
				return -EINTR;
		}
		nr_cons_waiting--;
	} else {
		/* Un productor abrió el FIFO */
		prod_count++;
		up(&sem_prod);

		/* Esperar a que haya un consimudor si no hay ninguno. */
		while(cons_count==0) { 
			nr_prod_waiting++;
			/* Liberar el 'mutex' antes de bloqueo*/
			up(&mtx);
			
			if (down_interruptible(&sem_prod)){
				down(&mtx);
				nr_prod_waiting--;
				prod_count--;
				up(&sem_cons);
				up(&mtx);
				return -EINTR;
			}
			
			if (down_interruptible(&mtx)) 
				return -EINTR;
		}
		nr_prod_waiting--;
	}

	up(&mtx);
	return 0;
}



static ssize_t fifoproc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	int nr_bytes=0;
	char kbuffer[KBUF_SIZE]="";

	if (len > CBUF_SIZE || len > KBUF_SIZE) 
		return -ENOSPC;

	if (copy_from_user(kbuffer, buf, len))
		return -EFAULT;

	/* Entrar a la sección crítica */
	if (down_interruptible(&mtx)){
		return -EINTR;
	} 

	/* Esperar hasta que haya hueco para insertar (debe haber consumidores) */	
	while (kfifo_avail(&cbuffer) < len && cons_count > 0) {

		nr_prod_waiting++;

		/* Despertar a posible consumidor bloqueado */
		up(&sem_cons);
		nr_cons_waiting--;

		/* Liberar el 'mutex' antes de bloqueo*/
		up(&mtx);

		/* Bloqueo en cola de espera */
		if (down_interruptible(&sem_prod)){
			down(&mtx);
			nr_prod_waiting--;
			prod_count--;
			up(&sem_cons);
			up(&mtx);
			return -EINTR;
		}

		/* Readquisición del 'mutex' antes de entrar a la SC */
		if (down_interruptible(&mtx)){
			return -EINTR;
		}
	}

	/* Detectar fin de comunicacion por error (productor cierra FIFO antes) */
	if (cons_count==0){
		up(&mtx);
		return -EPIPE;
	}

	nr_bytes=kfifo_in(&cbuffer,&kbuffer, len);

	/* Despertar a posible consumidor bloqueado */
	if(nr_cons_waiting > 0) {
		up(&sem_cons);
		nr_cons_waiting--;
	}

	up(&mtx);
	(*off)+=nr_bytes;  /* Update the file pointer */
	return nr_bytes;
}


static ssize_t fifoproc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	int nr_bytes=0;
	char kbuffer[KBUF_SIZE]="";

	/* Entrar a la sección crítica */
	if (down_interruptible(&mtx)){
		return -EINTR;
	}

	/* Esperar hasta que haya caracteres para sacar (debe haber productores) */	
	while (kfifo_len(&cbuffer) < len && prod_count > 0) {

		nr_cons_waiting++;

		/* Despertar a posible productor bloqueado */
		up(&sem_prod);
		nr_prod_waiting--;

		/* Liberar el 'mutex' antes de bloqueo*/
		up(&mtx);

		/* Bloqueo en cola de espera */
		if (down_interruptible(&sem_cons)) {
			down(&mtx);
			nr_cons_waiting--;
			up(&sem_prod);
			up(&mtx);
			return -EINTR;
		}

		/* Readquisición del 'mutex' antes de entrar a la SC */
		if (down_interruptible(&mtx)){
			return -EINTR;
		}
	}

	/* Detectar fin de comunicacion por error (productor cierra FIFO antes) */
	if (prod_count==0 && kfifo_is_empty(&cbuffer)){
		up(&mtx);
		return 0;
	}

	nr_bytes=kfifo_out(&cbuffer,&kbuffer, len);	

	/* Despertar a posible productor bloqueado */
	if(nr_prod_waiting > 0) {
		up(&sem_prod);
		nr_prod_waiting--;
	}

	if (copy_to_user(buf, kbuffer, len))
		return -EFAULT;

	up(&mtx);
	(*off)+=nr_bytes;  /* Update the file pointer */
	return nr_bytes;
}


static int fifoproc_release(struct inode *inodep, struct file *file)
{
	/* Entrar a la sección crítica */
	if (down_interruptible(&mtx)) 
		return -EINTR;

	/* Si un consumidor cierra la conexión, avisa a un posible productor bloqueado */
	/* Vice-versa */
	if (file->f_mode & FMODE_READ) {
		cons_count--;
		up(&sem_prod);
	} else {
		prod_count--;
		up(&sem_cons);
	}

	/* Chequeo si no hay ni productor ni consumidor bloqueados */
	if(cons_count==0 && prod_count==0)
		kfifo_reset(&cbuffer);

	up(&mtx);

	return 0;
}

static const struct file_operations proc_entry_fops = {
	.read = fifoproc_read,
	.write = fifoproc_write,
	.open = fifoproc_open,
	.release = fifoproc_release
};

int init_fifoproc_module( void )
{
	int retval;
	/* Inicialización del buffer */
	retval = kfifo_alloc(&cbuffer, CBUF_SIZE*sizeof(int), GFP_KERNEL);

	if (retval)
		return -ENOMEM;

	/* Inicialización a 0 de los semáforos usados como colas de espera */
	sema_init(&sem_prod,0);
	sema_init(&sem_cons,0);

	/* Inicializacion a 1 del semáforo que permite acceso en exclusión mutua a la SC */
	sema_init(&mtx,1);

	prod_count=cons_count=0;

    proc_entry = proc_create( "fifoproc", 0666, NULL, &proc_entry_fops);

    if (proc_entry == NULL) {
		kfifo_free(&cbuffer);
		return -ENOMEM;
		printk(KERN_INFO "Fifo_proc: No se puede crear entrada /proc.\n");
    } else {
		printk(KERN_INFO "Fifo_proc: Modulo cargado con exito.\n");
    }

    /* Devolver 0 para indicar una carga correcta del módulo */
    return 0;
}

void exit_fifoproc_module( void )
{
	remove_proc_entry("fifoproc", NULL);
	kfifo_free(&cbuffer);
	printk(KERN_INFO "Fifo_proc: Modulo descargado.\n");
}


module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );