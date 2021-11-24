// SPDX-License-Identifier: GPL-2.0-only
/*
 * x2100_ec_sys.c
 *
 * Author:
 *      vladisslav2011
 *      exander77 <exander77@pm.me>
 */

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include "internal.h"

MODULE_AUTHOR("exander77");
MODULE_DESCRIPTION("51nb X210/X2100 EC module");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.01");

static bool write_support;
module_param(write_support, bool, 0644);
MODULE_PARM_DESC(write_support, "Dangerous, reboot and removal of battery may "
		 "be needed.");

#define EC_SPACE_SIZE 256

static struct dentry *acpi_ec_debugfs_dir;

static ssize_t acpi_ec_read_io(struct file *f, char __user *buf,
			       size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */
	unsigned int size = EC_SPACE_SIZE;
	loff_t init_off = *off;
	int err = 0;

	if (*off >= size)
		return 0;
	if (*off + count >= size) {
		size -= *off;
		count = size;
	} else
		size = count;

	while (size) {
		u8 byte_read;
		err = ec_read(*off, &byte_read);
		if (err)
			return err;
		if (put_user(byte_read, buf + *off - init_off)) {
			if (*off - init_off)
				return *off - init_off; /* partial read */
			return -EFAULT;
		}
		*off += 1;
		size--;
	}
	return count;
}

static ssize_t acpi_ec_write_io(struct file *f, const char __user *buf,
				size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */

	unsigned int size = count;
	loff_t init_off = *off;
	int err = 0;

	if (!write_support)
		return -EINVAL;

	if (*off >= EC_SPACE_SIZE)
		return 0;
	if (*off + count >= EC_SPACE_SIZE) {
		size = EC_SPACE_SIZE - *off;
		count = size;
	}

	while (size) {
		u8 byte_write;
		if (get_user(byte_write, buf + *off - init_off)) {
			if (*off - init_off)
				return *off - init_off; /* partial write */
			return -EFAULT;
		}
		err = ec_write(*off, byte_write);
		if (err)
			return err;

		*off += 1;
		size--;
	}
	return count;
}

static const struct file_operations acpi_ec_io_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_io,
	.write = acpi_ec_write_io,
	.llseek = default_llseek,
};

#define ACPI_EC_FLAG_OBF	0x01	/* Output buffer full */
#define ACPI_EC_FLAG_IBF	0x02	/* Input buffer full */
#define ACPI_EC_FLAG_CMD	0x08	/* Input buffer contains a command */
#define ACPI_EC_FLAG_BURST	0x10	/* burst mode */
#define ACPI_EC_FLAG_SCI	0x20	/* EC-SCI occurred */

int ec_wr_cmd(struct acpi_ec *ec,u8 cmd,u8 * st)
{
	u8 s=0;
	u32 k;

	for(k=0;k<65535;k++)
	{
		s = inb(ec->command_addr);
		if(!(s&ACPI_EC_FLAG_IBF))
		{
			outb(cmd, ec->command_addr);
			if(st != NULL)
				*st=s;
//			printk("ec cmd=%02x k=%d ps=%02x s=%02x\n",cmd,k,s,inb(ec->command_addr));
			return 0;
		}
		usleep_range(1,5);
	}
	if(st != NULL)
		*st=s;
	printk("!ec cmd=%02x\n",cmd);
	return -ETIME;
	
	
}

int ec_wr_data(struct acpi_ec *ec,u8 data, u8 * st)
{
	u8 s=0;
	u32 k;
	
	for(k=0;k<65535;k++)
	{
		s = inb(ec->command_addr);
		if(!(s&ACPI_EC_FLAG_IBF))
		{
			outb(data, ec->data_addr);
			if(st != NULL)
				*st=s;
//			printk("ec out=%02x k=%d ps=%02x s=%02x\n",data,k,s,inb(ec->command_addr));
			return 0;
		}
		usleep_range(1,5);
	}
	if(st != NULL)
		*st=s;
	printk("!ec out=%02x\n",data);
	return -ETIME;
	
	
}

int ec_rd_data(struct acpi_ec *ec,u8 * data, u8 *st)
{
	u8 s=0,os=0;
	u32 k;
	if(data != NULL)
	{
		for(k=0;k<65536;k++)
		{
			s = inb(ec->command_addr);
			if(os!=s)
			{
//				printk("s=%02x=>%02x\n",os,s);
				os=s;
			}
			if(st != NULL)
				*st=s;
			if(s&ACPI_EC_FLAG_IBF)
			{
				usleep_range(1,5);
				continue;
			}
			if(s&ACPI_EC_FLAG_OBF)
			{
				*data=inb(ec->data_addr);
				return 0;
			}
			usleep_range(1,5);
		}
		printk("RDDATA TIMEO s=%02x\n",s);
		*data=inb(ec->data_addr);
		if(st != NULL)
			*st=s;
	}
	return 0;
	return -ETIME;
	
	
}

int ec_flush(struct acpi_ec *ec)
{
	u8 s=0;
	u32 k;
	u8 err=0;
	for(k=0;k<65535;k++)
	{
		s = inb(ec->command_addr);
		if(!(s&ACPI_EC_FLAG_SCI))
		{
			return 0;
		}
		if(!err)
			err=ec_wr_cmd(ec,0x84,NULL);
		if(!err)
			err=ec_rd_data(ec,&s,NULL);
		if(err)
			return err;
//		printk("GPE=%02x\n",s);
	}
	return -ETIME;
}

int ec_get_sci(struct acpi_ec *ec)
{
	u8 s=0;
	u32 k;
	u8 err=0;
	for(k=0;k<65535;k++)
	{
		s = inb(ec->command_addr);
		if(s&ACPI_EC_FLAG_SCI)
		{
			if(!err)
				err=ec_wr_cmd(ec,0x84,NULL);
			if(!err)
				err=ec_rd_data(ec,&s,NULL);
			if(err)
				return err;
			return s;
//		printk("GPE=%02x\n",s);
		}
		usleep_range(10,50);
	}
	return -ETIME;
}


#define EC_RAM_SIZE 0x01000000
#define EC_GPIO_SIZE 0x0100



int ec_read_gpio(u32 addr, u8 *val)
{
	int err=0;
	u8 ob;
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	
	ob=(addr)&0xff;
	

	mutex_lock(&ec->mutex);
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xba,&stat);
	if(!err)
		err=ec_wr_data(ec,ob,&stat);
	if(!err)
		err=ec_rd_data(ec,val,&stat);
	
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}

int ec_write_gpio(u32 addr, u8 val)
{
	int err=0;
	u8 ob;
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	
	ob=(addr)&0xff;
	if (!write_support)
		return -EINVAL;
	

	mutex_lock(&ec->mutex);
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xb9,&stat);
	if(!err)
		err=ec_wr_data(ec,val,&stat);
	
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}

int ec_read_ram(u32 addr, u8 *val)
{
	int err=0;
	u8 ob[4];
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	
	ob[0]=(addr >>16)&0xff;
	ob[1]=(addr >>8)&0xff;
	ob[2]=(addr)&0xff;
	ob[3]=0;
	

	mutex_lock(&ec->mutex);

	
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xbf,&stat);
	if(!err)
		err=ec_wr_data(ec,ob[0],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[1],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[2],&stat);
	if(!err)
		err=ec_rd_data(ec,val,&stat);
	
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}

static int ec_write_ram(u32 addr, u8 val)
{
	int err=0;
	u8 ob[4];
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	if (!write_support)
		return -EINVAL;
	
	ob[0]=(addr >>16)&0xff;
	ob[1]=(addr >>8)&0xff;
	ob[2]=(addr)&0xff;
	ob[3]=val;
	
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xbe,&stat);
	if(!err)
		err=ec_wr_data(ec,ob[0],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[1],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[2],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[3],&stat);
	
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}



int ec_read_ram_word(u32 addr, u8 *val)
{
	int err=0;
	u8 ob[4];
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	if(addr&1)
		return -EINVAL;
	ob[0]=(addr >>16)&0xff;
	ob[1]=(addr >>8)&0xff;
	ob[2]=(addr)&0xff;
	ob[3]=0;
	

	mutex_lock(&ec->mutex);

	
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xb8,&stat);
	if(!err)
		err=ec_wr_data(ec,ob[0],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[1],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[2],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[3],&stat);
	if(!err)
		err=ec_rd_data(ec,val,&stat);
	
	ob[3]=2;
	if(!err)
		err=ec_wr_cmd(ec,0xb8,&stat);
	if(!err)
		err=ec_wr_data(ec,ob[0],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[1],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[2],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[3],&stat);
	if(!err)
		err=ec_rd_data(ec,&val[1],&stat);
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}

static int ec_write_ram_word(u32 addr, u16 val)
{
	int err=0;
	u8 ob[5];
	u8 stat,d;
	struct acpi_ec *ec=first_ec;
	if (!write_support)
		return -EINVAL;
	if(addr&1)
		return -EINVAL;
	
	ob[0]=(addr >>16)&0xff;
	ob[1]=(addr >>8)&0xff;
	ob[2]=(addr)&0xff;
	ob[3]=val&0xff;
	ob[4]=val>>8;
	
	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);
//	printk("v=%02x\n",d);
	if(!err)
		err=ec_flush(ec);
	
	
	if(!err)
		err=ec_wr_cmd(ec,0xb7,&stat);
	if(!err)
		err=ec_wr_data(ec,ob[0],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[1],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[2],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[3],&stat);
	if(!err)
		err=ec_wr_data(ec,ob[4],&stat);
	
	
//	printk("s=%02x\n",stat);
	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);
	
	mutex_unlock(&ec->mutex);
	
	return err;
}

static ssize_t acpi_ec_read_ram(struct file *f, char __user *buf,
			       size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */
	unsigned int size = EC_RAM_SIZE;
	loff_t init_off = *off;
	int err = 0;
	u8 byte_read[2];

	if (*off >= size)
		return 0;
	if (*off + count >= size) {
		size -= *off;
		count = size;
	} else
		size = count;
	if((*off & 1)||(size&1))
		while (size) {
			err = ec_read_ram(*off, byte_read);
			if (err)
				return err;
			if (put_user(byte_read[0], buf + *off - init_off)) {
				if (*off - init_off)
					return *off - init_off; /* partial read */
				return -EFAULT;
			}
			*off += 1;
			size--;
		}
	else
		while (size) {
			err = ec_read_ram_word(*off, byte_read);
			if (err)
				return err;
			if (put_user(byte_read[0], buf + *off - init_off)) {
				if (*off - init_off)
					return *off - init_off; // partial read 
				return -EFAULT;
			}
			*off += 1;
			size--;
			if (put_user(byte_read[1], buf + *off - init_off)) {
				if (*off - init_off)
					return *off - init_off; // partial read 
				return -EFAULT;
			}
			*off += 1;
			size--;
		}
	
	return count;
}

static ssize_t acpi_ec_write_ram(struct file *f, const char __user *buf,
				size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */

	unsigned int size = count;
	loff_t init_off = *off;
	int err = 0;

	if (!write_support)
		return -EINVAL;

	if (*off >= EC_RAM_SIZE)
		return 0;
	if (*off + count >= EC_RAM_SIZE) {
		size = EC_RAM_SIZE - *off;
		count = size;
	}

	if((*off & 1)||(size&1))
		while (size) {
			u8 byte_write;
			if (get_user(byte_write, buf + *off - init_off)) {
				if (*off - init_off)
					return *off - init_off; /* partial write */
				return -EFAULT;
			}
			err = ec_write_ram(*off, byte_write);
			err=0;
			if (err)
				return err;
	
			*off += 1;
			size--;
		}
	else
		while (size) {
			u8 byte_write[2];
			if (get_user(byte_write[0], buf + *off - init_off)) {
				if (*off - init_off)
					return *off - init_off; /* partial write */
				return -EFAULT;
			}
			if (get_user(byte_write[1], buf + *off - init_off+1)) {
				if (*off - init_off)
					return *off - init_off; /* partial write */
				return -EFAULT;
			}
			err = ec_write_ram_word(*off, *(u16*)byte_write);
			err=0;
			if (err)
				return err;
	
			*off += 2;
			size-=2;
		}
	return count;
}

static ssize_t acpi_ec_read_gpio(struct file *f, char __user *buf,
			       size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */
	unsigned int size = EC_GPIO_SIZE;
	loff_t init_off = *off;
	int err = 0;

	if (*off >= size)
		return 0;
	if (*off + count >= size) {
		size -= *off;
		count = size;
	} else
		size = count;

	while (size) {
		u8 byte_read;
		err = ec_read_gpio(*off, &byte_read);
		if (err)
			return err;
		if (put_user(byte_read, buf + *off - init_off)) {
			if (*off - init_off)
				return *off - init_off; /* partial read */
			return -EFAULT;
		}
		*off += 1;
		size--;
	}
	return count;
}

static ssize_t acpi_ec_write_gpio(struct file *f, const char __user *buf,
				size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */

	unsigned int size = count;
	loff_t init_off = *off;
	int err = 0;

	if (!write_support)
		return -EINVAL;

	if (*off >= EC_GPIO_SIZE)
		return 0;
	if (*off + count >= EC_GPIO_SIZE) {
		size = EC_GPIO_SIZE - *off;
		count = size;
	}

	while (size) {
		u8 byte_write;
		if (get_user(byte_write, buf + *off - init_off)) {
			if (*off - init_off)
				return *off - init_off; /* partial write */
			return -EFAULT;
		}
		err = ec_write_gpio(*off, byte_write);
		err=0;
		if (err)
			return err;

		*off += 1;
		size--;
	}
	return count;
}

static const struct file_operations acpi_ec_ram_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_ram,
	.write = acpi_ec_write_ram,
	.llseek = default_llseek,
};

static const struct file_operations acpi_ec_gpio_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_gpio,
	.write = acpi_ec_write_gpio,
	.llseek = default_llseek,
};




static ssize_t acpi_ec_read_sci(struct file *f, char __user *buf,
			       size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */
	unsigned int size = count;
	loff_t init_off = *off;
	int err=0;
	struct acpi_ec *ec=first_ec;
	
	mutex_lock(&ec->mutex);
/*	if(!err)
		err=ec_wr_cmd(ec,0x82,&stat);
	if(!err)
		err=ec_rd_data(ec,&d,&stat);*/
	
	while (size) {
		err = ec_get_sci(ec);
		if (err<0)
		{
			mutex_unlock(&ec->mutex);
			return err;
		}
		if (put_user(err, buf + *off - init_off)) {
			mutex_unlock(&ec->mutex);
			if (*off - init_off)
				return *off - init_off; /* partial read */
			return -EFAULT;
		}
		*off += 1;
		size--;
	}
	
/*	if(!err)
		err=ec_wr_cmd(ec,0x83,&stat);*/
	
	mutex_unlock(&ec->mutex);

	return count;
}

static ssize_t acpi_ec_write_sci(struct file *f, const char __user *buf,
				size_t count, loff_t *off)
{
	/* Use this if support reading/writing multiple ECs exists in ec.c:
	 * struct acpi_ec *ec = ((struct seq_file *)f->private_data)->private;
	 */

	return -EINVAL;

}

static const struct file_operations acpi_ec_sci_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_sci,
	.write = acpi_ec_write_sci,
	.llseek = default_llseek,
};

static ssize_t acpi_ec_read_einval(struct file *f, char __user *buf,
			       size_t count, loff_t *off)
{
	return -EINVAL;
}

static ssize_t acpi_ec_write_fan_timer(struct file *f, const char __user *buf,
					size_t count, loff_t *off)
{
	if (count < 1 || *off > 0)
		return 0;
	
	uint8_t val;
	if (get_user(val, buf))
		return -EFAULT;
	
	int err=0;
	u8 stat;
	struct acpi_ec *ec=first_ec;

	if (!write_support)
		return -EINVAL;
	
	mutex_lock(&ec->mutex);
	if (!err) err = ec_wr_cmd (ec, 0x81, &stat);
	if (!err) err = ec_wr_data(ec, 0x52, &stat);
	if (!err) err = ec_wr_data(ec, val, &stat);
	mutex_unlock(&ec->mutex);
	
	return err < 0 ? err : 1;
}

static const struct file_operations acpi_ec_fan_timer_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_einval,
	.write = acpi_ec_write_fan_timer,
	.llseek = default_llseek,
};

static ssize_t acpi_ec_write_fan_speed(struct file *f, const char __user *buf,
					size_t count, loff_t *off)
{
	if (count < 1 || *off > 0)
		return 0;
	
	uint8_t val;
	if (get_user(val, buf))
		return -EFAULT;
	
	int err=0;
	u8 stat;
	struct acpi_ec *ec=first_ec;

	if (!write_support)
		return -EINVAL;
	
	mutex_lock(&ec->mutex);
	if (!err) err = ec_wr_cmd (ec, 0x81, &stat);
	if (!err) err = ec_wr_data(ec, 0x51, &stat);
	if (!err) err = ec_wr_data(ec, val, &stat);
	mutex_unlock(&ec->mutex);
	
	return err < 0 ? err : 1;
}

static const struct file_operations acpi_ec_fan_speed_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = acpi_ec_read_einval,
	.write = acpi_ec_write_fan_speed,
	.llseek = default_llseek,
};

static void acpi_ec_add_debugfs(struct acpi_ec *ec, unsigned int ec_device_count)
{
	struct dentry *dev_dir;
	char name[64];
	umode_t mode = 0400;

	if (ec_device_count == 0)
		acpi_ec_debugfs_dir = debugfs_create_dir("ec", NULL);

	sprintf(name, "ec%u", ec_device_count);
	dev_dir = debugfs_create_dir(name, acpi_ec_debugfs_dir);

	debugfs_create_x32("gpe", 0444, dev_dir, &first_ec->gpe);
	debugfs_create_bool("use_global_lock", 0444, dev_dir,
			    &first_ec->global_lock);

	if (write_support)
		mode = 0600;
	debugfs_create_file_size("io", mode, dev_dir, ec, &acpi_ec_io_ops,EC_SPACE_SIZE);
	debugfs_create_file("sci", mode, dev_dir, ec, &acpi_ec_sci_ops);
	debugfs_create_file_size("ram", mode, dev_dir, ec, &acpi_ec_ram_ops,EC_RAM_SIZE);
	debugfs_create_file_size("gpio", mode, dev_dir, ec, &acpi_ec_gpio_ops,EC_GPIO_SIZE);
	mode = 0200;
	debugfs_create_file_size("fan_timer", mode, dev_dir, ec, &acpi_ec_fan_timer_ops,1);
	debugfs_create_file_size("fan_speed", mode, dev_dir, ec, &acpi_ec_fan_speed_ops,1);
}

static int __init acpi_ec_sys_init(void)
{
	if (first_ec)
		acpi_ec_add_debugfs(first_ec, 0);
	return 0;
}

static void __exit acpi_ec_sys_exit(void)
{
	debugfs_remove_recursive(acpi_ec_debugfs_dir);
}

module_init(acpi_ec_sys_init);
module_exit(acpi_ec_sys_exit);
