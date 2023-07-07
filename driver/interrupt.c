/* Linux 中断例程 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int irq = 121; /* 这个值不是随便填的，得从设备树中查找 */
char *interface = "aaa";

static irqreturn_t myirq_handler(int irq, void *dev)
{
	printk("%d IRQ is working\n", irq);
	return IRQ_NONE;
}

static int __init myirq_init(void)
{
	printk("the module is working!\n");
	printk("the irq is ready for working!\n");
	if (request_irq(irq, myirq_handler, IRQF_SHARED, interface, &irq)) {
		printk(KERN_ERR "%s interrrupt can't register %d IRQ \n", interface, irq);
		return -EIO;
	}
	printk("%s request %d IRQ\n", interface, irq);
	return 0;
}

static void __exit myirq_exit(void)
{
	printk("the module is leaving!\n");
	printk("the irq is bye bye!\n");
	free_irq(irq, &irq);
	printk("%s interrupt free %d IRQ\n", interface, irq);
}

module_init(myirq_init);
module_exit(myirq_exit);

MODULE_LICENSE("GPL");