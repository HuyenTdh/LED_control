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
struct tasklet_struct gpio_tasklet;
unsigned char irq_active = 0;

struct gpiodev_private_data {
    char label[20];
    struct gpio_desc *desc;
};

struct gpiodrv_private_data {
    int total;
    struct class *class_gpio;
    struct device **dev;
};

struct gpiodrv_private_data gpio_drv_data;

struct of_device_id gpio_device_match[] = {
    {.compatible="org,bone-gpio-sysfs"},
    {}
};

ssize_t direction_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t direction_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t value_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
ssize_t label_show(struct device *dev, struct device_attribute *attr, char *buf);


static DEVICE_ATTR_RW(direction);
static DEVICE_ATTR_RW(value);
static DEVICE_ATTR_RO(label);

static struct attribute* gpio_attrs[] = {
    &dev_attr_direction.attr,
    &dev_attr_value.attr,
    &dev_attr_label.attr,
    NULL
};

static struct attribute_group gpio_attr_group = {
    .attrs = gpio_attrs
};

static const struct attribute_group* gpio_attr_groups[] = {
    &gpio_attr_group,
    NULL
};

ssize_t direction_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(dev);
    int dir;
    char *direction;

    dir = gpiod_get_direction(dev_data->desc);
    if (dir < 0)
        return dir;
    direction = (dir == 0) ? "out" : "in";
    return sprintf(buf, "%s\r\n", direction);
}

ssize_t direction_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(dev);
    int ret;

    if (sysfs_streq(buf, "in"))
        ret = gpiod_direction_input(dev_data->desc);
    else if (sysfs_streq(buf, "out"))
        ret = gpiod_direction_output(dev_data->desc, 0);
    else
        return -EINVAL;
    return ret ? : count;
}
ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(dev);
    int ret;

    ret = gpiod_get_value(dev_data->desc);
    return sprintf(buf, "%d\r\n", ret);
}
ssize_t value_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(dev);
    int ret;
    long value;

    ret = kstrtol(buf, 0, &value);
    if (ret)
        return ret;
    gpiod_set_value(dev_data->desc, value);
    return count;
}
ssize_t label_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(dev);

    return sprintf(buf, "%s\r\n", dev_data->label);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    spin_lock(&gpio_lock);
	irq_active = 1;
    tasklet_schedule(&gpio_tasklet);
    spin_unlock(&gpio_lock);
	return IRQ_HANDLED;
}

void tasklet_func(unsigned long data)
{
    unsigned char *i = (unsigned char*)data;
    unsigned char tmp = 0;
    int d = 0;

    spin_lock(&gpio_lock);
    if (*i) {
        *i = 0;
        tmp = 1;
    }
    spin_unlock(&gpio_lock);
    if (tmp) {
        printk("gpio_irq: Interrupt was triggered and ISR was called!\n");
        for (d = 1; d < gpio_drv_data.total; d++) {
            struct gpiodev_private_data *dev_data = (struct gpiodev_private_data*)dev_get_drvdata(gpio_drv_data.dev[d]);
            gpiod_set_value(dev_data->desc, 0);
        }
    }
}

int bone_probe(struct platform_device *pdev)
{
    struct device_node *parent = pdev->dev.of_node;
    struct device_node *child = NULL;
    struct device *dev = &pdev->dev;
    struct gpiodev_private_data *dev_data;
    const char *label;
    int ret;
    unsigned char i = 0;

    gpio_drv_data.total = of_get_available_child_count(parent);
    if (!gpio_drv_data.total) {
        dev_err(dev, "No device found\r\n");
        return -EINVAL;
    }
    dev_info(dev,"Total device is: %d\r\n", gpio_drv_data.total);

    gpio_drv_data.dev = devm_kzalloc(dev, gpio_drv_data.total * sizeof(struct device), GFP_KERNEL);
    if (!gpio_drv_data.dev) {
        dev_err(dev, "Cannot allocate device structure\r\n");
        return -ENOMEM;
    }

    for_each_child_of_node(parent, child) {
        dev_data = devm_kzalloc(dev, sizeof(struct gpiodev_private_data), GFP_KERNEL);
        if (!dev_data) {
            dev_err(dev, "Cannot allocate device data\r\n");
            return -ENOMEM;
        }
        if (of_property_read_string(child, "label", &label)) {
            dev_warn(dev, "GPIO label missing\r\n");
            snprintf(dev_data->label, sizeof(dev_data->label), "Unkngpio-%d",i);
        }
        else {
            strcpy(dev_data->label, label);
            dev_info(dev, "GPIO label: %s\r\n", dev_data->label);
        }

        dev_data->desc = devm_fwnode_get_gpiod_from_child(dev, "bone", &child->fwnode,\
                GPIOD_ASIS, dev_data->label);
        if (IS_ERR(dev_data->desc)) {
            ret = PTR_ERR(dev_data->desc);
            if (ret == -ENOENT)
                dev_err(dev, "No GPIO has been assigned to the requested function and/or index\r\n");
                return ret;
        }

        if (sysfs_streq(dev_data->label, "gpio2.2")) {
            ret = gpiod_direction_input(dev_data->desc);
            if (ret) {
                dev_err(dev, "Cannot set GPIO direction: %s\r\n", dev_data->label);
                return ret;
            }
            irq_number = gpiod_to_irq(dev_data->desc);
            if (irq_number < 0) {
                dev_err(dev, "Cannot get irq number\r\n");
                return irq_number;
            }

            spin_lock_init(&gpio_lock);
            tasklet_init(&gpio_tasklet, tasklet_func, (unsigned long)&irq_active);

            if (request_irq(irq_number, gpio_irq_handler, IRQF_TRIGGER_FALLING, "bone_gpio_irq", NULL) != 0) {
                dev_err(dev, "Cannot request IRQ interrupt\r\n");
                return -1;
            }
        }
        else {
            ret = gpiod_direction_output(dev_data->desc, 0);
            if (ret) {
                dev_err(dev, "Cannot set GPIO direction: %s\r\n", dev_data->label);
                return ret;
            }
            gpio_drv_data.dev[i] = device_create_with_groups(gpio_drv_data.class_gpio, dev, 0,\
                    (void*)dev_data, gpio_attr_groups, dev_data->label);
            if (IS_ERR(gpio_drv_data.dev[i])) {
                dev_err(dev, "Cannot create device\r\n");
                return PTR_ERR(gpio_drv_data.dev[i]);
            }   
        }
        i++;
    }

    return 0;
}

int bone_remove(struct platform_device *pdev)
{
    if (irq_number > 0)
        free_irq(irq_number, NULL);
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
    gpio_drv_data.class_gpio = class_create(THIS_MODULE, "bone-gpios");
    if (IS_ERR(gpio_drv_data.class_gpio)) {
        pr_err("Cannot create class");
        return PTR_ERR(gpio_drv_data.class_gpio);
    }

    platform_driver_register(&gpio_driver);
    return 0;
}

static void __exit interrupt_exit(void)
{
    class_destroy(gpio_drv_data.class_gpio);
    platform_driver_unregister(&gpio_driver);
}

module_init(interrupt_init);
module_exit(interrupt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HuyenTdh");
MODULE_DESCRIPTION("GPIO interrupt driver");