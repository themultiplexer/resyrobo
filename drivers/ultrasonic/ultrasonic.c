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
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/delay.h>

static dev_t gpio_dev_number;
static struct cdev *driver_object;
static struct class *gpio_class;
static struct device *ultrasonic_left_dev, *ultrasonic_right_dev;
static struct timeval left_previous_time, right_previous_time;
static int left_irq_rising_pin, left_irq_falling_pin, right_irq_rising_pin, right_irq_falling_pin;
static int left_distance, right_distance;

// ToDo: GPIO entsprechend der Verschaltung anpassen.
#define LEFT_ECHO_RISING_PIN    26
#define LEFT_ECHO_FALLING_PIN    3
#define LEFT_TRIGGER_PIN 19

#define RIGHT_ECHO_RISING_PIN    27
#define RIGHT_ECHO_FALLING_PIN    2
#define RIGHT_TRIGGER_PIN 17

static struct timer_list left_timer, right_timer;

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

static void trigger( unsigned long pin )
{
	gpio_set_value(pin, 1);
	usleep_range(10, 10);
	gpio_set_value(pin, 0);
	// Fallbacks
	if(pin == LEFT_TRIGGER_PIN) {
		mod_timer(&left_timer, jiffies + msecs_to_jiffies(200));
	} else if (pin == RIGHT_TRIGGER_PIN) {
		mod_timer(&right_timer, jiffies + msecs_to_jiffies(200));
	}
}

static irqreturn_t rising_handler(int irq, void *dev){

  if(irq == left_irq_rising_pin){
    do_gettimeofday(&left_previous_time);
  } else if(irq == right_irq_rising_pin) {
    do_gettimeofday(&right_previous_time);
  }

	return IRQ_HANDLED;
}

static irqreturn_t falling_handler(int irq, void *dev){
	struct timeval current_time, result;
	do_gettimeofday(&current_time);

  if(irq == left_irq_falling_pin){
    timeval_subtract(&result, &current_time, &left_previous_time);
  	left_distance = result.tv_usec;
    mod_timer(&left_timer, jiffies + msecs_to_jiffies(25));
  } else if(irq == right_irq_falling_pin) {
    timeval_subtract(&result, &current_time, &right_previous_time);
  	right_distance = result.tv_usec;
    mod_timer(&right_timer, jiffies + msecs_to_jiffies(25));
  }

	return IRQ_HANDLED;
}

static int driver_open( struct inode *geraetedatei, struct file *instanz )
{
	int err = -1;
  struct device* ultrasonic_dev;
  int trigger_pin, echo_falling_pin, echo_rising_pin, *irq_rising_pin, *irq_falling_pin;
  struct timer_list *timer;

  if (iminor(geraetedatei)==0) {
    trigger_pin = LEFT_TRIGGER_PIN;
    echo_falling_pin = LEFT_ECHO_FALLING_PIN;
    echo_rising_pin = LEFT_ECHO_RISING_PIN;
    ultrasonic_dev = ultrasonic_left_dev;
    irq_rising_pin = &left_irq_rising_pin;
    irq_falling_pin = &left_irq_falling_pin;
    timer = &left_timer;
    left_distance = 200000;
  } else {
    trigger_pin = RIGHT_TRIGGER_PIN;
    echo_falling_pin = RIGHT_ECHO_FALLING_PIN;
    echo_rising_pin = RIGHT_ECHO_RISING_PIN;
    ultrasonic_dev = ultrasonic_right_dev;
    irq_rising_pin = &right_irq_rising_pin;
    irq_falling_pin = &right_irq_falling_pin;
    timer = &right_timer;
    right_distance = 200000;
  }

	// TRIGGER_PIN reservieren
	err = gpio_request( trigger_pin, "rpi-gpio-trigger" );
	if (err) {
		printk("gpio_request failed\n");
		return -EIO;
	}
	// TRIGGER_PIN auf Ausgabe konfigurieren
	err = gpio_direction_output( trigger_pin, 0 );
	if (err) {
		printk("gpio_direction_output failed\n");
		gpio_free( trigger_pin );
		return -EIO;
	}
	err = gpio_request( echo_rising_pin, "rpi-gpio-echo-rising" );
	if (err) {
		printk("gpio_request failed\n");
		gpio_free( trigger_pin );
		return -EIO;
	}
	err = gpio_direction_input( echo_rising_pin );
	if (err) {
		printk("gpio_direction_input failed\n");
		gpio_free( trigger_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}
	err = gpio_request( echo_falling_pin, "rpi-gpio-echo-falling" );
	if (err) {
		printk("gpio_request failed\n");
		gpio_free( trigger_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}
	err = gpio_direction_input( echo_falling_pin );
	if (err) {
		printk("gpio_direction_input failed\n");
		gpio_free( trigger_pin );
		gpio_free( echo_falling_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}

	if ( (*irq_rising_pin = gpio_to_irq(echo_rising_pin)) < 0 ) {
		printk("GPIO to IRQ mapping failure %d\n", echo_rising_pin);
		gpio_free( trigger_pin );
		gpio_free( echo_falling_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}

	if ( (*irq_falling_pin = gpio_to_irq(echo_falling_pin)) < 0 ) {
		printk("GPIO to IRQ mapping failure %d\n", echo_falling_pin);
		gpio_free( trigger_pin );
		gpio_free( echo_falling_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}

	setup_timer(timer, trigger, trigger_pin);

	if (request_irq(*irq_rising_pin, rising_handler, IRQF_TRIGGER_RISING, "ultrasonic_rising", ultrasonic_dev)) {
		printk(KERN_INFO "short: can't get assigned irq %i\n", *irq_rising_pin);
		gpio_free( trigger_pin );
		gpio_free( echo_falling_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}

	if (request_irq(*irq_falling_pin, falling_handler, IRQF_TRIGGER_FALLING, "ultrasonic_falling", ultrasonic_dev)) {
		printk(KERN_INFO "short: can't get assigned irq %i\n", *irq_falling_pin);
		free_irq(*irq_rising_pin, ultrasonic_dev);
		gpio_free( trigger_pin );
		gpio_free( echo_falling_pin );
		gpio_free( echo_rising_pin );
		return -EIO;
	}

	printk("gpio %d and  (%d,%d) successfully configured\n",trigger_pin, echo_rising_pin, echo_falling_pin);
	trigger(trigger_pin);
	return 0;
}

static int driver_close( struct inode *geraete_datei, struct file *instanz )
{
	printk( "driver_close called\n");

  struct device* ultrasonic_dev;
  int trigger_pin, echo_falling_pin, echo_rising_pin, *irq_rising_pin, *irq_falling_pin;
  struct timer_list *timer;

  if (iminor(geraete_datei)==0) {
    trigger_pin = LEFT_TRIGGER_PIN;
    echo_falling_pin = LEFT_ECHO_FALLING_PIN;
    echo_rising_pin = LEFT_ECHO_RISING_PIN;
    ultrasonic_dev = ultrasonic_left_dev;
    irq_rising_pin = &left_irq_rising_pin;
    irq_falling_pin = &left_irq_falling_pin;
    timer = &left_timer;
  } else {
    trigger_pin = RIGHT_TRIGGER_PIN;
    echo_falling_pin = RIGHT_ECHO_FALLING_PIN;
    echo_rising_pin = RIGHT_ECHO_RISING_PIN;
    ultrasonic_dev = ultrasonic_right_dev;
    irq_rising_pin = &right_irq_rising_pin;
    irq_falling_pin = &right_irq_falling_pin;
    timer = &right_timer;
  }

	// TRIGGER_PIN und ECHO_PIN freigeben
	del_timer(timer);
	free_irq(*irq_rising_pin, ultrasonic_dev);
	free_irq(*irq_falling_pin, ultrasonic_dev);
	gpio_free( echo_falling_pin );
	gpio_free( echo_rising_pin );
	gpio_free( trigger_pin );
	return 0;
}

static ssize_t driver_read( struct file *instanz, char __user *user,
	size_t count, loff_t *offset )
{
	int to_copy, not_copied;
  int *distance;

  if(iminor(instanz->f_inode)==0){
    distance = &left_distance;
  } else {
    distance = &right_distance;
  }

	//printk( "driver_read %d\n", *distance);

	to_copy = min( count, sizeof(*distance) );
	not_copied=copy_to_user( user, distance, to_copy );

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
	if( alloc_chrdev_region(&gpio_dev_number,0,2,"ultrasonic")<0 )
		return -EIO;
	driver_object = cdev_alloc(); /* Anmeldeobjekt reservieren */
	if( driver_object==NULL )
		goto free_device_number;
	driver_object->owner = THIS_MODULE;
	driver_object->ops = &fops;
	if( cdev_add(driver_object,gpio_dev_number,2) )
		goto free_cdev;
	/* Eintrag im Sysfs, damit Udev den Geraetedateieintrag erzeugt. */
	gpio_class = class_create( THIS_MODULE, "ultrasonic" );
	if( IS_ERR( gpio_class ) ) {
		pr_err( "gpio: no udev support\n");
		goto free_cdev;
	}
	ultrasonic_left_dev = device_create( gpio_class, NULL, gpio_dev_number,
		NULL, "%s", "ultrasonic-left" );
  ultrasonic_right_dev = device_create( gpio_class, NULL, gpio_dev_number +1,
      NULL, "%s", "ultrasonic-right" );

	dev_info(ultrasonic_left_dev, "mod_init");
	return 0;
free_cdev:
	kobject_put( &driver_object->kobj );
free_device_number:
	unregister_chrdev_region( gpio_dev_number, 2 );
	return -EIO;
}

static void __exit mod_exit( void )
{
	dev_info(ultrasonic_left_dev, "mod_exit");
	/* Loeschen des Sysfs-Eintrags und damit der Geraetedatei */
	device_destroy( gpio_class, gpio_dev_number );
  device_destroy( gpio_class, gpio_dev_number + 1 );
	class_destroy( gpio_class );
	/* Abmelden des Treibers */
	cdev_del( driver_object );
	unregister_chrdev_region(gpio_dev_number, 2);
	return;
}

module_init( mod_init );
module_exit( mod_exit );

/* Metainformation */
MODULE_LICENSE("GPL");
