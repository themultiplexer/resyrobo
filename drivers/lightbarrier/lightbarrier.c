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

static dev_t gpio_dev_number;
static struct cdev *driver_object;
static struct class *gpio_class;
static struct device *lightbarrier_left_dev, *lightbarrier_right_dev;
static struct timeval left_previous_time, right_previous_time;
static int left_irq_pin, right_irq_pin;
static long left_ticks, right_ticks;
// ToDo: GPIO entsprechend der Verschaltung anpassen.
#define LEFT_INPUT_PIN    21
#define RIGHT_INPUT_PIN    20

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

	if (irq == left_irq_pin) {
		timeval_subtract(&result, &current_time, &left_previous_time);

		if(result.tv_sec > 0 || result.tv_usec > 5000) {
			left_ticks++;
		}
		left_previous_time = current_time;
	} else {
		timeval_subtract(&result, &current_time, &right_previous_time);

		if(result.tv_sec > 0 || result.tv_usec > 5000) {
			right_ticks++;
		}
		right_previous_time = current_time;
	}
	return IRQ_HANDLED;
}

static int driver_open( struct inode *geraetedatei, struct file *instanz )
{
	int err = -1;
	struct device* lightbarrier_dev;
	int pin, *irq_pin;

	if (iminor(geraetedatei)==0) {
		pin = LEFT_INPUT_PIN;
		lightbarrier_dev = lightbarrier_left_dev;
    irq_pin = &left_irq_pin;
	} else {
		pin = RIGHT_INPUT_PIN;
		lightbarrier_dev = lightbarrier_right_dev;
    irq_pin = &right_irq_pin;
	}

	err = gpio_request( pin, "rpi-gpio-echo" );
	if (err) {
		printk("gpio_request failed\n");
		gpio_free( pin );
		return -EIO;
	}
	err = gpio_direction_input( pin );
	if (err) {
		printk("gpio_direction_input failed\n");
		gpio_free( pin );
		return -EIO;
	}

  if ( (*irq_pin = gpio_to_irq(pin)) < 0 ) {
    printk("GPIO to IRQ mapping failure %d\n", pin);
    gpio_free( pin );
    return -EIO;
  }

  if (request_irq(*irq_pin, intr_handler, IRQF_TRIGGER_FALLING, "lightbarrier_left", lightbarrier_dev)) {
    printk(KERN_INFO "short: can't get assigned irq %i\n", *irq_pin);
    gpio_free( pin );
    return -EIO;
  }

	printk("gpio  %d successfull configured\n", pin);
	return 0;
}

static int driver_close( struct inode *geraetedatei, struct file *instanz )
{
	printk( "driver_close called\n");

	struct device* lightbarrier_dev;
	int pin, irq_pin;

	if (iminor(geraetedatei)==0) {
		pin = LEFT_INPUT_PIN;
		lightbarrier_dev = lightbarrier_left_dev;
	} else {
		pin = RIGHT_INPUT_PIN;
		lightbarrier_dev = lightbarrier_right_dev;
	}

	free_irq(gpio_to_irq(pin), lightbarrier_dev);
	gpio_free(pin);
	return 0;
}

static ssize_t driver_read( struct file *instanz, char __user *user,
	size_t count, loff_t *offset )
{
	int to_copy, not_copied;
	int ticks;

	if (iminor(instanz->f_inode)==0) {
		ticks = left_ticks;
	} else {
		ticks = right_ticks;
	}

	// Echopin zur Applikation kopieren
	to_copy = min( count, sizeof(ticks) );
	not_copied=copy_to_user( user, &ticks, to_copy );

	return to_copy-not_copied;
}

static struct file_operations fops = {
	.owner= THIS_MODULE,
	.read = driver_read,
	.open= driver_open,
	.release= driver_close,
};

static int __init mod_init( void )
{
	if( alloc_chrdev_region(&gpio_dev_number,0,2,"lightbarrier")<0 )
		return -EIO;
	driver_object = cdev_alloc(); /* Anmeldeobjekt reservieren */
	if( driver_object==NULL )
		goto free_device_number;
	driver_object->owner = THIS_MODULE;
	driver_object->ops = &fops;
	if( cdev_add(driver_object,gpio_dev_number,2) )
		goto free_cdev;
	/* Eintrag im Sysfs, damit Udev den Geraetedateieintrag erzeugt. */
	gpio_class = class_create( THIS_MODULE, "lightbarrier" );
	if( IS_ERR( gpio_class ) ) {
		pr_err( "gpio: no udev support\n");
		goto free_cdev;
	}
	lightbarrier_left_dev = device_create( gpio_class, NULL, gpio_dev_number,
		NULL, "%s", "lightbarrier-left" );
	lightbarrier_right_dev = device_create( gpio_class, NULL, gpio_dev_number +1,
		NULL, "%s", "lightbarrier-right" );


	dev_info(lightbarrier_left_dev, "mod_init");
	return 0;
free_cdev:
	kobject_put( &driver_object->kobj );
free_device_number:
	unregister_chrdev_region( gpio_dev_number, 2 );
	return -EIO;
}

static void __exit mod_exit( void )
{
	dev_info(lightbarrier_left_dev, "mod_exit");
	/* Loeschen des Sysfs-Eintrags und damit der Geraetedatei */
	device_destroy( gpio_class, gpio_dev_number + 1);
	device_destroy( gpio_class, gpio_dev_number );
	class_destroy( gpio_class );
	/* Abmelden des Treibers */
	cdev_del( driver_object );
	unregister_chrdev_region( gpio_dev_number, 2 );
	return;
}

module_init( mod_init );
module_exit( mod_exit );

/* Metainformation */
MODULE_LICENSE("GPL");
