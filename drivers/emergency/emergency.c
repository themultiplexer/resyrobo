#include <linux/module.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/sched/signal.h>
//#include <linux/signal.h>

static dev_t gpio_dev_number;
static struct cdev *driver_object;
static struct class *gpio_class;
static struct device *emergency_dev;
static struct timeval previous_time;
static struct task_struct *task;
static int irq_pin;
static int pid;

// ToDo: GPIO entsprechend der Verschaltung anpassen.
#define INPUT_PIN    22

int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

static irqreturn_t intr_handler(int irq, void *dev){
	struct timeval current_time;
	do_gettimeofday(&current_time);
	struct timeval result;
	timeval_subtract(&result, &current_time, &previous_time);
	
	if(result.tv_sec > 0 || result.tv_usec > 250000){
		printk("Emergency %lu %lu", current_time.tv_sec, current_time.tv_usec);
		int signum = SIGUSR1;
		struct siginfo info;
		memset(&info, 0, sizeof(struct siginfo));
		info.si_signo = signum;
		int ret = send_sig_info(signum, &info, task);
		if (ret < 0) {
			printk(KERN_INFO "error sending signal\n");
		}
		previous_time = current_time;
	}
	return IRQ_HANDLED;
}

static int driver_open( struct inode *geraetedatei, struct file *instanz )
{
	int err = -1;
	err = gpio_request( INPUT_PIN, "rpi-gpio-echo" );
	if (err) {
		printk("gpio_request failed\n");
		gpio_free( INPUT_PIN );
		return -EIO;
	}
	err = gpio_direction_input( INPUT_PIN );
	if (err) {
		printk("gpio_direction_input failed\n");
		gpio_free( INPUT_PIN );
		return -EIO;
	}

	task = current;

	if ( (irq_pin = gpio_to_irq(INPUT_PIN)) < 0 ) {
		printk("GPIO to IRQ mapping failure %d\n", INPUT_PIN);
		return -EIO;
	}

	if (request_irq(irq_pin, intr_handler, IRQF_TRIGGER_RISING, "emergency", emergency_dev)) {
			printk(KERN_INFO "short: can't get assigned irq %i\n", irq_pin);
			return -EIO;
	}
	printk("gpio %d successfull configured\n", INPUT_PIN);
	return 0;
}

static int driver_close( struct inode *geraete_datei, struct file *instanz )
{
	printk( "driver_close called\n");
	free_irq(irq_pin, emergency_dev);
	gpio_free( INPUT_PIN );
	return 0;
}

static struct file_operations fops = {
	.owner= THIS_MODULE,
	.open= driver_open,
	.release= driver_close,
};

static int __init mod_init( void )
{
	if( alloc_chrdev_region(&gpio_dev_number,0,1,"emergency")<0 )
		return -EIO;
	driver_object = cdev_alloc(); /* Anmeldeobjekt reservieren */
	if( driver_object==NULL )
		goto free_device_number;
	driver_object->owner = THIS_MODULE;
	driver_object->ops = &fops;
	if( cdev_add(driver_object,gpio_dev_number,1) )
		goto free_cdev;
	/* Eintrag im Sysfs, damit Udev den Geraetedateieintrag erzeugt. */
	gpio_class = class_create( THIS_MODULE, "emergency" );
	if( IS_ERR( gpio_class ) ) {
		pr_err( "gpio: no udev support\n");
		goto free_cdev;
	}
	emergency_dev = device_create( gpio_class, NULL, gpio_dev_number, NULL, "%s", "emergency" );

	dev_info(emergency_dev, "mod_init");
	return 0;
free_cdev:
	kobject_put( &driver_object->kobj );
free_device_number:
	unregister_chrdev_region( gpio_dev_number, 2 );
	return -EIO;
}

static void __exit mod_exit( void )
{
	dev_info(emergency_dev, "mod_exit");
	/* Loeschen des Sysfs-Eintrags und damit der Geraetedatei */
	device_destroy( gpio_class, gpio_dev_number );
	class_destroy( gpio_class );
	/* Abmelden des Treibers */
	cdev_del( driver_object );
	unregister_chrdev_region( gpio_dev_number, 1 );
	return;
}

module_init( mod_init );
module_exit( mod_exit );

/* Metainformation */
MODULE_LICENSE("GPL");
