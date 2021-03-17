#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>

struct tty_driver* kbd_driver= NULL;

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
	return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
	printk(KERN_INFO "sys_ledctl: loading\n");
	printk(KERN_INFO "sys_ledctl: fgconsole is %x\n", fg_console);
	return vc_cons[fg_console].d->port.tty->driver;
}

SYSCALL_DEFINE1(ledctl, unsigned int, mask)
{
	int ret = 0;
	int aux = 0;
	/* Cuerpo de la función */
	if ((mask > 7) || (mask < 0)){
		printk(KERN_INFO "sys_ledctl: Mask values is from 0 to 7 \n");
		ret = -EINVAL;
	} else {
		aux = mask >> 1 & 1;
		aux = aux << 1;
		aux += mask >> 2 & 1;
		aux = aux << 1;
		aux += mask & 1;
		kbd_driver= get_kbd_driver_handler();
		printk(KERN_INFO "sys_ledctl: mask is %d\n", aux);
		ret = set_leds(kbd_driver,aux); 
	}
	return ret;
}