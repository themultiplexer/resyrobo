#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/notifier.h>

static dev_t gpio_dev_number;
static struct cdev *driver_object;
static struct class *gpio_class;
static struct device *motorl_dev, *motorr_dev;
static struct pwm_device *pwm_left, *pwm_right;
struct mutex mutex_left, mutex_right;
// ToDo: Hier muessen die verwendeten GPIOs eingetragen werden
#define ML1   6
#define ML2   5
#define MR1   23
#define MR2   24

// PWM Frequency in Hz
#define PWM_FREQ 1000

static int my_probe(struct platform_device *pdev)
{
	const char *side;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;

    if (of_property_read_string(np, "side", &side) ) {
        dev_err(dev, "of_property_read_u32\n");
        return -EINVAL;
    }

	if(strcmp(side, "left")){
		pwm_left = pwm_get(&pdev->dev, NULL);
		if (IS_ERR(pwm_left)){
			printk("Requesting left PWM failed");
			return -EIO;
		}
	} else if(strcmp(side, "right")) {
		pwm_right = pwm_get(&pdev->dev, NULL);
		if (IS_ERR(pwm_right)){
			printk("Requesting right PWM failed");
			return -EIO;
		}
	}
	return 0;
}

static int my_remove(struct platform_device *pdev)
{
    const char *side;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;

    if (of_property_read_string(np, "side", &side) ) {
        dev_err(dev, "of_property_read_u32\n");
        return -EINVAL;
    }

	if(strcmp(side, "left")){
		pwm_disable(pwm_left);
		pwm_put(pwm_left);
	} else if(strcmp(side, "right")) {
		pwm_disable(pwm_right);
		pwm_put(pwm_right);
	}
	return 0;
}


static struct of_device_id my_match_table[] = {
     {
             .compatible = "motor",
     },
	 {},
};
MODULE_DEVICE_TABLE(of, my_match_table);

static struct platform_driver my_platform_driver = {
	.probe = my_probe,
	.remove = my_remove,
	.driver = {
		.name = "motor",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(my_match_table),
	},
};

enum motor { left, right };

static int driver_open( struct inode *geraetedatei, struct file *instanz )
{
	int err;
	int motor_in1, motor_in2;
	struct device *motor_device;
	// Ein Treiber bedient zwei Motoren.
	if (iminor(geraetedatei)==0) { // motor_left
		motor_in1 = ML1;
		motor_in2 = ML2;
		motor_device = motorl_dev;
	} else { // motor_right
		motor_in1 = MR1;
		motor_in2 = MR2;
		motor_device = motorr_dev;
	}

	// GPIO für motor_in1 reservieren
	// Inklusive Fehlerbehandlung
	err = gpio_request( motor_in1, "rpi_gpio_motor_in1" );
	if (err!=0) {
		return -EIO;
	}
	// GPIO für motor_in1 auf Output schalten
	// Fehlerbehandlung: Falls Fehlschlag muss der GPIO wieder
	//                   freigegeben werden.
	err = gpio_direction_output( motor_in1, 0 );
	if (err!=0) {
		gpio_free( motor_in1 );
		return -EIO;
	}
	// GPIO für motor_in2 reservieren
	// Inklusive Fehlerbehandlung
	err = gpio_request( motor_in2, "rpi_gpio_motor_in2" );
	if (err!=0) {
		gpio_free( motor_in1 );
		return -EIO;
	}
	// GPIO für motor_in2 auf Output schalten
	// Fehlerbehandlung: Falls Fehlschlag müssen die GPIOs wieder
	//                   freigegeben werden.
	err = gpio_direction_output( motor_in2, 0 );
	if (err!=0) {
		gpio_free( motor_in1 );
		gpio_free( motor_in2 );
		return -EIO;
	}

	printk("gpio %d and  %d successfull configured\n",motor_in1,motor_in2);
	return 0;
}

static int driver_close( struct inode *geraete_datei, struct file *instanz )
{
	int motor_in1, motor_in2;

	if (iminor(instanz->f_inode)==0) { // motor_left
		motor_in1 = ML1;
		motor_in2 = ML2;
	} else { // motor_right
		motor_in1 = MR1;
		motor_in2 = MR2;
	}

	printk( "driver_close called\n");
	gpio_free( motor_in1 );
	gpio_free( motor_in2 );
	return 0;
}

static int drive_motor(enum motor motor, int speed){
	
	struct pwm_device* pwm_device;
	int motor_in1, motor_in2;
	unsigned int period, duty_cycle;

	if(motor == left){
		pwm_device = pwm_left;
		motor_in1 = ML1;
		motor_in2 = ML2;
	} else if (motor == right) {
		pwm_device = pwm_right;
		motor_in1 = MR1;
		motor_in2 = MR2;
	}

	if(speed == 0){
		gpio_set_value( motor_in1, 0 );
		gpio_set_value( motor_in2, 0 );
		pwm_disable(pwm_device);
		return 0;
	}

	period = 1000000000 / PWM_FREQ; // Convert Hz to period in ns
	duty_cycle = period / 100 * abs(speed);
	
	//printk("period %d duty_cycle %d\n", period, duty_cycle);
	//printk("IN1 %d IN2 %d\n", motor_in1, motor_in2);

	pwm_config(pwm_device, duty_cycle, period);

	if(speed > 0){
		gpio_set_value( motor_in1, 1 );
		gpio_set_value( motor_in2, 0 );
	} else {
		gpio_set_value( motor_in1, 0 );
		gpio_set_value( motor_in2, 1 );
	}

	pwm_enable(pwm_device);
	return 0;
}

static ssize_t driver_write( struct file *instanz, const char __user *user,
		size_t count, loff_t *offset )
{
	unsigned long not_copied, to_copy;
	int value=0;

	to_copy = min( count, sizeof(value) );
	not_copied=copy_from_user(&value, user, to_copy);
	//dev_info( motorl_dev, "driver_write: value %x\n", value );

	if (iminor(instanz->f_inode)==0) { // motor_left
		drive_motor(left, value);
	} else { // motor_right
		drive_motor(right, value);
	}

	return to_copy-not_copied;
}

static struct file_operations fops = {
	.owner= THIS_MODULE,
	.write= driver_write,
	.open= driver_open,
	.release= driver_close,
};

static int __init mod_init( void )
{
	if( alloc_chrdev_region(&gpio_dev_number,0,2,"motor")<0 )
		return -EIO;
	driver_object = cdev_alloc(); /* Anmeldeobjekt reservieren */
	if( driver_object==NULL )
		goto free_device_number;
	driver_object->owner = THIS_MODULE;
	driver_object->ops = &fops;
	if( cdev_add(driver_object,gpio_dev_number,2) )
		goto free_cdev;
	/* Eintrag im Sysfs, damit Udev den Geraetedateieintrag erzeugt. */
	gpio_class = class_create( THIS_MODULE, "motor" );
	if( IS_ERR( gpio_class ) ) {
		pr_err( "gpio: no udev support\n");
		goto free_cdev;
	}
	motorl_dev = device_create( gpio_class, NULL, gpio_dev_number,
		NULL, "%s", "motor-left" );
	motorr_dev = device_create( gpio_class, NULL, gpio_dev_number+1,
		NULL, "%s", "motor-right" );

	dev_info(motorl_dev, "mod_init");
	return platform_driver_register(&my_platform_driver);
free_cdev:
	kobject_put( &driver_object->kobj );
free_device_number:
	unregister_chrdev_region( gpio_dev_number, 2 );
	return -EIO;
}

static void __exit mod_exit( void )
{
	dev_info(motorl_dev, "mod_exit");
	platform_driver_unregister(&my_platform_driver);
	/* Loeschen des Sysfs-Eintrags und damit der Geraetedatei */
	device_destroy( gpio_class, gpio_dev_number+1 );
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
