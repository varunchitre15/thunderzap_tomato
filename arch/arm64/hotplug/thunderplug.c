/* Copyright (c) 2015, Varun Chitre <varun.chitre15@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A simple hotplugging driver optimized for Octa Core CPUs
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/powersuspend.h>
#include <linux/cpufreq.h>

static int suspend_cpu_num = 2, resume_cpu_num = 7;
static int endurance_level = 0;
static int device_cpus = 8;
static int core_limit = 8;

#define DEBUG 0

#define THUNDERPLUG "thunderplug"

#define DRIVER_VERSION  2
#define DRIVER_SUBVER 0

#define CPU_LOAD_THRESHOLD        (65)

#define DEF_SAMPLING_MS			(500)

static int sampling_time = DEF_SAMPLING_MS;
static int load_threshold = CPU_LOAD_THRESHOLD;

static struct workqueue_struct *tplug_wq;
static struct delayed_work tplug_work;
static unsigned int last_load[8] = {0, 0, 0, 0, 0, 0, 0, 0};

struct cpu_load_data {
	u64 prev_cpu_idle;
	u64 prev_cpu_wall;
	unsigned int avg_load_maxfreq;
	unsigned int cur_load_maxfreq;
	unsigned int samples;
	unsigned int window_size;
	cpumask_var_t related_cpus;
};

static DEFINE_PER_CPU(struct cpu_load_data, cpuload);

static inline void offline_cpus(void)
{
	unsigned int cpu;
	switch(endurance_level) {
		case 1:
			if(suspend_cpu_num > 4)
				suspend_cpu_num = 4;
		break;
		case 2:
			if(suspend_cpu_num > 2)
				suspend_cpu_num = 2;
		break;
		default:
		break;
	}
	for(cpu = 7; cpu > (suspend_cpu_num - 1); cpu--) {
		if (cpu_online(cpu))
			cpu_down(cpu);
	}
	pr_info("%s: %d cpus were offlined\n", THUNDERPLUG, (device_cpus - suspend_cpu_num));
}

static inline void cpus_online_all(void)
{
	unsigned int cpu;
	switch(endurance_level) {
	case 1:
		if(resume_cpu_num > 3 || resume_cpu_num == 1)
			resume_cpu_num = 3;
	break;
	case 2:
		if(resume_cpu_num > 1)
			resume_cpu_num = 1;
	break;
	case 0:
		if(resume_cpu_num < 7)
			resume_cpu_num = 7;
	break;
	default:
	break;
	}

	for (cpu = 1; cpu <= resume_cpu_num; cpu++) {
		if (cpu_is_offline(cpu))
			cpu_up(cpu);
	}

	pr_info("%s: all cpus were onlined\n", THUNDERPLUG);
}

static ssize_t thunderplug_suspend_cpus_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", suspend_cpu_num);
}

static ssize_t thunderplug_suspend_cpus_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val < 1 || val > 8)
		pr_info("%s: suspend cpus off-limits\n", THUNDERPLUG);
	else
		suspend_cpu_num = val;

	return count;
}

static ssize_t thunderplug_endurance_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", endurance_level);
}

static ssize_t __ref thunderplug_endurance_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	switch(val) {
	case 0:
	case 1:
	case 2:
		if(endurance_level!=val) {
		endurance_level = val;
		offline_cpus();
		cpus_online_all();
	}
	break;
	default:
		pr_info("%s: invalid endurance level\n", THUNDERPLUG);
	break;
	}

	return count;
}

static ssize_t thunderplug_sampling_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", sampling_time);
}

static ssize_t __ref thunderplug_sampling_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > 50)
		sampling_time = val;

	return count;
}

static ssize_t thunderplug_load_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", load_threshold);
}

static ssize_t __ref thunderplug_load_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	sscanf(buf, "%d", &val);
	if(val > 10)
		load_threshold = val;

	return count;
}

static unsigned int get_curr_load(unsigned int cpu)
{
	int ret;
	unsigned int idle_time, wall_time;
	unsigned int cur_load, load_max_freq;
	u64 cur_wall_time, cur_idle_time;
	struct cpu_load_data *pcpu = &per_cpu(cpuload, cpu);
	struct cpufreq_policy policy;

	ret = cpufreq_get_policy(&policy, cpu);
	if (ret)
		return -EINVAL;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, 0);

	wall_time = (unsigned int) (cur_wall_time - pcpu->prev_cpu_wall);
	pcpu->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int) (cur_idle_time - pcpu->prev_cpu_idle);
	pcpu->prev_cpu_idle = cur_idle_time;

	if (unlikely(!wall_time || wall_time < idle_time))
		return 0;

	cur_load = 100 * (wall_time - idle_time) / wall_time;
	return cur_load;
}

static void __cpuinit tplug_work_fn(struct work_struct *work)
{
	int i;
	unsigned int load[8], avg_load[8];

	switch(endurance_level)
	{
	case 0:
		core_limit = 8;
	case 1:
		core_limit = 4;
	case 2:
		core_limit = 2;
	default:
		core_limit = 8;
	}

	for(i = 0 ; i < core_limit; i++)
	{
		if(cpu_online(i))
			load[i] = get_curr_load(i);
		else
			load[i] = 0;

		avg_load[i] = ((int) load[i] + (int) last_load[i]) / 2;
		last_load[i] = load[i];
	}

	for(i = 0 ; i < core_limit; i++)
	{
	if(cpu_online(i) && avg_load[i] > load_threshold && cpu_is_offline(i+1))
	{
	if(DEBUG)
		pr_info("thunderplug : bringing back cpu%d\n",i);
		if(!((i+1) > 7))
			cpu_up(i+1);
	}
	else if(cpu_online(i) && avg_load[i] < load_threshold && cpu_online(i+1))
	{
	if(DEBUG)
		pr_info("thunderplug : offlining cpu%d\n",i);
		if(!(i+1)==0)
			cpu_down(i+1);
	}
	}

	queue_delayed_work_on(0, tplug_wq, &tplug_work,
		msecs_to_jiffies(sampling_time));

}

static void thunderplug_suspend(struct power_suspend *h)
{
	offline_cpus();

	pr_info("%s: suspend\n", THUNDERPLUG);
}

static void __ref thunderplug_resume(struct power_suspend *h)
{
	cpus_online_all();

	pr_info("%s: resume\n", THUNDERPLUG);
}

static ssize_t thunderplug_ver_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
       return sprintf(buf, "ThunderPlug %u.%u", DRIVER_VERSION, DRIVER_SUBVER);
}

static struct kobj_attribute thunderplug_ver_attribute =
       __ATTR(version,
               0444,
               thunderplug_ver_show, NULL);

static struct kobj_attribute thunderplug_suspend_cpus_attribute =
       __ATTR(suspend_cpus,
               0666,
               thunderplug_suspend_cpus_show, thunderplug_suspend_cpus_store);

static struct kobj_attribute thunderplug_endurance_attribute =
       __ATTR(endurance_level,
               0666,
               thunderplug_endurance_show, thunderplug_endurance_store);

static struct kobj_attribute thunderplug_sampling_attribute =
       __ATTR(sampling_rate,
               0666,
               thunderplug_sampling_show, thunderplug_sampling_store);

static struct kobj_attribute thunderplug_load_attribute =
       __ATTR(load_threshold,
               0666,
               thunderplug_load_show, thunderplug_load_store);

static struct attribute *thunderplug_attrs[] =
    {
        &thunderplug_ver_attribute.attr,
        &thunderplug_suspend_cpus_attribute.attr,
        &thunderplug_endurance_attribute.attr,
        &thunderplug_sampling_attribute.attr,
        &thunderplug_load_attribute.attr,
        NULL,
    };

static struct attribute_group thunderplug_attr_group =
    {
        .attrs = thunderplug_attrs,
    };

static struct power_suspend thunderplug_power_suspend_handler = 
	{
		.suspend = thunderplug_suspend,
		.resume = thunderplug_resume,
	};

static struct kobject *thunderplug_kobj;

static int __init thunderplug_init(void)
{
        int ret = 0;
        int sysfs_result;
        printk(KERN_DEBUG "[%s]\n",__func__);

        thunderplug_kobj = kobject_create_and_add("thunderplug", kernel_kobj);

        if (!thunderplug_kobj) {
                pr_err("%s Interface create failed!\n",
                        __FUNCTION__);
                return -ENOMEM;
        }

        sysfs_result = sysfs_create_group(thunderplug_kobj, &thunderplug_attr_group);

        if (sysfs_result) {
                pr_info("%s sysfs create failed!\n", __FUNCTION__);
                kobject_put(thunderplug_kobj);
        }

        register_power_suspend(&thunderplug_power_suspend_handler);
		tplug_wq = alloc_workqueue("tplug",
				WQ_HIGHPRI | WQ_UNBOUND, 1);

		INIT_DELAYED_WORK(&tplug_work, tplug_work_fn);
		queue_delayed_work_on(0, tplug_wq, &tplug_work,
		                      msecs_to_jiffies(10));

        pr_info("%s: init\n", THUNDERPLUG);

        return ret;
}

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Varun Chitre <varun.chitre15@gmail.com>");
MODULE_DESCRIPTION("Hotplug driver for OctaCore CPU");
late_initcall(thunderplug_init);
