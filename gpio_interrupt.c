#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

unsigned int irq_number;
unsigned int gpio1 = 60;

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	printk("gpio_irq: Interrupt was triggered and ISR was called!\n");
	return IRQ_HANDLED;
}

static int __init interrupt_init(void)
{
    printk("Module init");
    if (gpio_request(gpio1, "sysfs") < 0) {
        pr_err("Cannot allocate GPIO 60");
        return -1;
    }

    if (gpio_direction_input(gpio1) < 0) {
        pr_err("Cannot set GPIO 60 to input");
        gpio_free(gpio1);
        return -1;
    }

    irq_number = gpio_to_irq(gpio1);

    if (request_irq(irq_number, gpio_irq_handler, IRQF_TRIGGER_FALLING, "my_gpio_irq", NULL) != 0) {
        pr_err("Cannot request interrupt nr.: %d\n", irq_number);
        gpio_free(gpio1);
        return -1;
    }
    printk("GPIO 60 is mapped to IRQ Nr.: %d\n", irq_number);
    return 0;
}

static void __exit interrupt_exit(void)
{
    printk("Module exit");
    free_irq(irq_number, NULL);
    gpio_free(gpio1);
}

module_init(interrupt_init);
module_exit(interrupt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HuyenTdh");
MODULE_DESCRIPTION("GPIO interrupt driver");