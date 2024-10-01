#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/string.h>
#include <linux/spinlock.h>

spinlock_t gpio_lock;
unsigned int irq_number;
unsigned int gpio1 = 60;
struct tasklet_struct gpio_tasklet;
unsigned char irq_active = 0;

struct gpiodev_private_data{
    char label[20];
    struct gpio_desc *desc;
};

struct gpiodrv_private_data{
    int total;
    struct class *class_gpio;
    struct device **dev;
};

struct gpiodrv_private_data gpio_drv_data;

struct of_device_id gpio_device_match[] = {
    {.compatible="org,bone-gpio-sysfs"},
    {}
};

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    spin_lock(&gpio_lock);
	irq_active = 1;
    spin_unlock(&gpio_lock);
	return IRQ_HANDLED;
}

void tasklet_func(unsigned long data)
{
    unsigned char *i = (unsigned char*)data;

    spin_lock(&gpio_lock);
    if (*i) {
        *i = 0;
        printk("gpio_irq: Interrupt was triggered and ISR was called!\n");
    }
    spin_unlock(&gpio_lock);
}

int bone_probe(struct platform_device *pdev)
{
    struct device_node *parent = pdev->dev.of_node;
    struct device_node *child = NULL;
    struct device *dev = &pdev->dev;
    struct gpiodev_private_data *dev_data;
    const char *label;
    int ret;

    gpio_drv_data.total = of_get_available_child_count(parent);
    if (!gpio_drv_data.total) {
        dev_err(dev, "No device found\r\n");
        return -EINVAL;
    }
    dev_info(dev,"Total device is: %d\r\n", gpio_drv_data.total);

    for_each_child_of_node(parent, child) {
        if (of_property_read_string(child,"label", &label)) {
            dev_warn(dev, "Missing label information\r\n");
            continue;
        }
        if (strcmp(label, "gpio2.2"))
            continue;
        dev_data = devm_kzalloc(dev, sizeof(struct gpiodev_private_data), GFP_KERNEL);
        if (!dev_data) {
            dev_err(dev, "Cannot allocate dev_data's memory\r\n");
            return -ENOMEM;
        }
        strcpy(dev_data->label, label);
        dev_info(dev, "GPIO label: %s\r\n", dev_data->label);
        dev_data->desc = devm_fwnode_get_gpiod_from_child(dev, "bone", &child->fwnode,\
                            GPIOD_ASIS, dev_data->label);
        if (IS_ERR(dev_data->desc)) {
            return PTR_ERR(dev_data->desc);
        }
        ret = gpiod_direction_input(dev_data->desc);
        if (ret) {
            dev_err(dev, "gpio direction set failed\r\n");
            return ret;
        }
        irq_number = gpiod_to_irq(dev_data->desc);
        if (irq_number < 0) {
            dev_err(dev, "Cannot get IRQ number\r\n");
            return irq_number;
        }

        spin_lock_init(&gpio_lock);
        tasklet_init(&gpio_tasklet, tasklet_func, (unsigned long)&irq_active);

        if (request_irq(irq_number, gpio_irq_handler, IRQF_TRIGGER_FALLING, "my_gpio_irq", NULL) != 0) {
            pr_err("Cannot request interrupt nr.: %d\n", irq_number);
            return -1;
        }
        printk("%s is mapped to IRQ Nr.: %d\n", dev_data->label, irq_number);
        return 0;
    }

    return 0;
}

int bone_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Remove called\r\n");
    return 0;
}

struct platform_driver gpio_driver = {
    .probe = bone_probe,
    .remove = bone_remove,
    .driver = {
        .name = "bone-gpio-sysfs",
        .of_match_table = gpio_device_match,
    },
};

static int __init interrupt_init(void)
{
    platform_driver_register(&gpio_driver);
    return 0;
}

static void __exit interrupt_exit(void)
{
    platform_driver_unregister(&gpio_driver);
}

module_init(interrupt_init);
module_exit(interrupt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HuyenTdh");
MODULE_DESCRIPTION("GPIO interrupt driver");