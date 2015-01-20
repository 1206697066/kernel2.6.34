#include <linux/sched.h>
#include <linux/list.h>
#include <asm/div64.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/percpu.h>

#define T 1000 //uti扩大的倍数，因为内核不能运算浮点数，所以把利用率扩大

#ifndef PARTITION_DEBUG
#define PARTITION_DEBUG
#endif

void init_partition_job(struct partition_job *new_job, struct task_struct *p);
void print_link_job(struct partition_scheduling *p_sched);
void print_order_link_job(struct partition_scheduling *p_sched);

struct partition_scheduling partition_sched;

struct partition_scheduling* get_partition_scheduling(void)
{
     return &partition_sched;
}

/*
 *dinitilize struct partition_scheduling(global struct var), called by sched_init(void) in /kernel/sched.c when the system boots
 */

void init_partition_scheduling(void)
{
	spin_lock_init(&partition_sched.lock);
	partition_sched.total_num_cpu = 0;
	partition_sched.partition_num_cpu = 0;
	partition_sched.cpu_bitmap = NULL;
	partition_sched.turn_on = 0;
	INIT_LIST_HEAD(&partition_sched.partition_link_job);
	INIT_LIST_HEAD(&partition_sched.partition_order_link_job);
	INIT_LIST_HEAD(&partition_sched.partition_idle_link_job);
	
	partition_sched.lcm = 0;

	#ifdef CONFIG_SCHED_PARTITION_POLICY
    printk(KERN_ALERT "PARTITION_SCHED turns on and PARTITION_POLICY:%d\n", SCHED_PARTITION);
	#endif

	#ifdef CONFIG_FAIR_GROUP_SCHED
    printk(KERN_ALERT "Alert: init_PARTITION_scheduling: FAIR_GROUP_SCHED turns on\n");
	#endif

	#ifdef CONFIG_RT_GROUP_SCHED
    printk(KERN_ALERT "Alert: init_PARTITION_scheduling: RT_GROUP_SCHED turns on\n");
	#endif
}

void re_ini_partition_scheduling(struct partition_scheduling *partition_sched)
{
	partition_sched->total_num_cpu = 0;
	partition_sched->partition_num_cpu = 0;
	if(partition_sched->cpu_bitmap != NULL)
	{
		kfree(partition_sched->cpu_bitmap);
		partition_sched->cpu_bitmap = NULL;
	}
	partition_sched->cpu_bitmap = NULL;
	partition_sched->turn_on = 0;
	partition_sched->activate_flag = 0;//默认加入队尾
	INIT_LIST_HEAD(&partition_sched->partition_link_job);
	INIT_LIST_HEAD(&partition_sched->partition_order_link_job);
	INIT_LIST_HEAD(&partition_sched->partition_idle_link_job);
	
	partition_sched->lcm = 0;
}

/*
 *initilize PARTITION_rq for each cpu, called by sched_init(void) in /kernel/sched.c when the system boots
 */

void init_partition_rq(struct partition_rq *partition_rq)
{
	spin_lock_init(&partition_rq->lock);
	partition_rq->num_task = 0;
	partition_rq->num_from_expired_to_ready = 0;
	partition_rq->ready = 0;
	partition_rq->partition_cpu_tick = 0;
	partition_rq->tail_flag = 0;
	partition_rq->curr= NULL;
	partition_rq->expired_p = NULL;
	partition_rq->ready_p = NULL;
	partition_rq->idle_partition_job = NULL;
	INIT_LIST_HEAD(&partition_rq->expired_list);
	INIT_LIST_HEAD(&partition_rq->ready_list);
	
	INIT_LIST_HEAD(partition_rq->ptr1);
	INIT_LIST_HEAD(partition_rq->ptr2);
	
	#ifdef PARTITION_DEBUG
        printk(KERN_ALERT "init_partition_rq\n");
	#endif
	
}

/*把所有的非空任务添加到partition_sched结构体中的partiton_link_job中，在setpara中完成*/
/*void add_partition_task_list(struct rq *rq, struct task_struct *p)
{
	struct partition_job *new_job = NULL;
	struct partition_scheduling *partition_sched = get_partition_scheduling();
	new_job = (struct partition_job *)kmalloc(sizeof(struct partition_job), GFP_KERNEL);
	if( p && new_job)
	{
		init_partition_job(new_job, p);
		list_add_tail(&new_job->list, &partition_sched->partition_link_job);
		#ifdef PARTITION_DEBUG
		printk(KERN_ALERT "add_partition_task_list");
		#endif
	}

}*/

/*把所有的非空任务添加到partition_sched结构体中的partiton_order_link_job中(按照利用率由高到低排序)，在setpara中完成*/
void add_partition_task_list_to_order(struct rq *rq, struct task_struct *p)
{
	int flag = 0;
	struct list_head *ptr;
	struct partition_job *new_job = NULL;
	struct partition_job *job;
	struct partition_scheduling *partition_sched = get_partition_scheduling();
	new_job = (struct partition_job *)kmalloc(sizeof(struct partition_job), GFP_KERNEL);
	if( p && new_job)
	{
		init_partition_job(new_job, p);
		//list_add_tail(&new_job->list, &partition_sched->partition_link_job);
		if(!list_empty(&partition_sched->partition_order_link_job))
		{
			list_for_each(ptr, &partition_sched->partition_order_link_job)
			{
				job = list_entry(ptr, struct partition_job, list);
				if(new_job->uti > job->uti)
				{
					flag = 1;
					list_add_tail(&new_job->list, &job->list);
					break;
				}
			}
			if(flag == 0)
				list_add_tail(&new_job->list, &partition_sched->partition_order_link_job);
		}
		else
			list_add_tail(&new_job->list, &partition_sched->partition_order_link_job);

		#ifdef PARTITION_DEBUG
		printk(KERN_ALERT "add_partition_task_list_order");
		#endif
	}

}
//把所有的空任务添加到partition_sched结构体中的partition_idle_link_job中，在setpara中完成
void add_partition_idle_task_list(struct rq *rq, struct task_struct *p)
{
	struct partition_job *new_job = NULL;
	struct partition_scheduling *partition_sched = get_partition_scheduling();
	new_job = (struct partition_job *)kmalloc(sizeof(struct partition_job), GFP_KERNEL);
	if( p && new_job)
	{
		init_partition_job(new_job, p);
		list_add_tail(&new_job->list, &partition_sched->partition_idle_link_job);
		//#ifdef PARTITION_DEBUG
		//printk(KERN_ALERT "add_partition_idle_task_list");
		//#endif
	}

}
//在所有的任务加入到全局结构体partition_sched结构体之前，先要初始化所有的任务
void init_partition_job(struct partition_job *new_job, struct task_struct *p)
{
	new_job->task = p;
	//new_job->release_time = 0;
	//new_job->deadline = 0;
	//new_job->next_release_time = 0;
	//new_job->next_deadline = 0;
	new_job->release_time = 0;
	new_job->deadline = p->p;
	new_job->next_release_time = new_job->deadline;
	new_job->next_deadline = new_job->next_release_time + p->p;
	if(p->idle_flag == 0)//非空任务
		new_job->uti = (p->c_real)*T / (p->p);
	else
		new_job->uti = 1;
	new_job->remaining_c = new_job->task->c_real;
	INIT_LIST_HEAD(&new_job->list);
	INIT_LIST_HEAD(&p->partition_link);
	p->my_job = new_job;
	#ifdef PARTITION_DEBUG
    	printk(KERN_ALERT "init_partition_job\n");
	#endif
}
//打印通过系统调用传递给sched_param的所有任务的变量
void print_sched_kparam_of_job(int num_partition_thread, struct sched_param para)
{
	printk(KERN_ALERT "job:para_c_level:%d para_c_real:%d para_p:%d para_idle_flag:%d para_partition_id:%d\n",para.c_level, para.c_real, 				para.p, para.idle_flag, para.partition_id);
}
//打印传递给task_struct中的所有的任务变量
void print_p_para_of_task(struct task_struct *p)
{
	printk(KERN_ALERT "task:p_c_level:%d p_c_real:%d p_p:%d p_idle_flag:%d p_partition_id:%d\n",p->c_level, p->c_real, p->p, p->idle_flag,
			p->partition_id);	
}
//获取第一个执行partition的cpu
int get_first_partition_cpu(struct partition_scheduling *p_sched)
{
	int i;
	for(i=0; i<p_sched->total_num_cpu; i++)
	{
		if(p_sched->cpu_bitmap[i] == 1)
			return i;
	}
	return -1;
}
//获取下一个执行partition的cpu
int next_cpu_rt_fair(int curr, struct partition_scheduling *p_sched)
{
	int i;
	for(i=curr+1; i<p_sched->total_num_cpu; i++)
	{
		if(p_sched->cpu_bitmap[i])
			return i;
	}
	for(i=0; i<curr; i++)
		if(p_sched->cpu_bitmap[i])
			return i;
	return curr;
}
void print_idle_job_in_rq(struct partition_scheduling *p_sched)
{
	int i;
	struct list_head *ptr;
	struct task_struct *idle_task;
	struct rq *rq;
	printk(KERN_ALERT "print_info idle_job");
	for(i=0; i<p_sched->total_num_cpu;i++)
	{
		printk(KERN_ALERT "times:%d ",i);
		if(p_sched->cpu_bitmap[i])
		{
			rq = cpu_rq(i);
			
			list_for_each(ptr,&rq->partition.ready_list)
			{
				idle_task = list_entry(ptr, struct task_struct, partition_link);
				printk(KERN_ALERT "idle_job info partition_id:%d,rq->cpu:%d deadline:%llu",idle_task->partition_id,rq->cpu,idle_task->my_job->deadline);
			}
			
		}
	}
}


void print_order_link_job(struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct partition_job *job;
	list_for_each(ptr, &p_sched->partition_order_link_job)
	{
		job = list_entry(ptr, struct partition_job, list);
		{
			printk(KERN_ALERT "not_idle_partition_id:%d deadline:%llu uti:%d",job->task->partition_id,job->deadline,job->uti);
		}
	}
}
//每个执行partition任务的cpu拥有一个空闲任务，以便在没有partition任务时，继续占有cpu
void allocate_idle_tasks_on_cpus(struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct partition_job *job;
	struct rq *src_rq;
	struct rq *des_rq;
	int cpu;
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "allocate_idle_tasks_on_cpus start");
	#endif	
	cpu = get_first_partition_cpu(p_sched);
	list_for_each(ptr, &p_sched->partition_idle_link_job)
	{
		job = list_entry(ptr, struct partition_job, list);
		src_rq = task_rq(job->task);
		des_rq = cpu_rq(cpu);
		if(src_rq != des_rq)
		{       
            while( task_running(src_rq, job->task) )
			{
            	mb();
            }
        }
		deactivate_task(src_rq, job->task, 0);
        set_task_cpu(job->task, des_rq->cpu);//设置任务在指定cpu上运行
        activate_task(des_rq, job->task, 0);
		//if(&des_rq->partition != NULL)
		//{	
		//	des_rq->partition.curr = job->task;
		//	printk(KERN_ALERT "idle task is in partiton.curr cpu:%d partition_id:%d",des_rq->cpu,des_rq->partition.curr->partition_id);		
		//}	       
		des_rq->partition.idle_partition_job = job;
        cpu = next_cpu_rt_fair(cpu, p_sched);

	}
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "allocate_idle_tasks_on_cpus end");
	print_idle_job_in_rq(p_sched);//输出idle任务信息
	print_order_link_job(p_sched);//按序输出partittion_scheduling中非空任务信息
	#endif
}

void init_cpu_uti(int *cpu_uti, struct partition_scheduling *p_sched)
{
	int i;
	for(i=0; i<p_sched->partition_num_cpu; ++i)
		cpu_uti[i] = 0;
}

int get_lowest_uti_cpu(int *cpu_uti, struct partition_scheduling *p_sched)
{
	int i;
	int num_cpu = 0;
	int uti_lowest = cpu_uti[0];
	for(i=1; i<p_sched->partition_num_cpu; ++i)
	{
		if(cpu_uti[i] == 0)
			return i;
		if(cpu_uti[i] < uti_lowest)
		{
			uti_lowest = cpu_uti[i];
			num_cpu = i;
		}
	}
	return num_cpu;
}

void allocte_task_on_cpu_rq_in_order(struct rq *rq, struct partition_job *job)
{
	struct rq *src_rq;
	struct rq *des_rq;
	src_rq = task_rq(job->task);
	des_rq = rq;
	if(src_rq != des_rq)
	{       
        while( task_running(src_rq, job->task) )
		{
            mb();
        }
    }
	deactivate_task(src_rq, job->task, 0);
    set_task_cpu(job->task, des_rq->cpu);//设置任务在指定cpu上运行
	activate_task(des_rq, job->task, 0);
}

void allocate_tasks(struct partition_scheduling *p_sched)
{
	struct rq *rq;
	struct list_head *ptr;
	struct partition_job *job;
	int *cpu_uti;//记录每个cpu的利用率
	int cpu_which;//返回的利用率最低的cpu
	cpu_uti = (int *)kmalloc(sizeof(int) * p_sched->partition_num_cpu,GFP_KERNEL);
	if(!cpu_uti)
		return; 
	init_cpu_uti(cpu_uti,p_sched);
	p_sched->activate_flag = 1;//flag=1,任务按deadline进队列
	list_for_each(ptr,&p_sched->partition_order_link_job)
	{
		job = list_entry(ptr, struct partition_job, list);
		cpu_which = get_lowest_uti_cpu(cpu_uti, p_sched);//获取执行partition任务利用率最低的cpu
		rq = cpu_rq(cpu_which);
		allocte_task_on_cpu_rq_in_order(rq,job);//把此job按执行顺寻添加到对应rq的ready_list中
		cpu_uti[cpu_which]+=job->uti;
	}
	
	
}

void print_tasks_on_each_cpu(struct partition_scheduling *p_sched)
{
	int firstcpu,nextcpu;
	struct list_head *ptr;
	struct rq *rq;
	struct task_struct *p;
	firstcpu = get_first_partition_cpu(p_sched);
	rq = cpu_rq(firstcpu);
	printk(KERN_ALERT "cpu%d的任务队列",firstcpu);
	list_for_each(ptr, &rq->partition.ready_list)
	{
		p = list_entry(ptr, struct task_struct, partition_link);
		printk(KERN_ALERT "uti:%d remaining_c:%llu release_time:%llu deadline:%llu pid:%d partition_id:%d",p->my_job->uti,p->my_job->remaining_c,p->my_job->release_time,p->my_job->deadline,p->pid,p->partition_id);
	}
	nextcpu = next_cpu_rt_fair(firstcpu, p_sched);
	while(nextcpu != firstcpu)
	{
		rq = cpu_rq(nextcpu);
		printk(KERN_ALERT "cpu%d的任务队列",nextcpu);
		list_for_each(ptr, &rq->partition.ready_list)
		{
			p = list_entry(ptr, struct task_struct, partition_link);
			printk(KERN_ALERT "uti:%d remaining_c:%llu release_time:%llu deadline:%llu pid:%d partition_id:%d",p->my_job->uti,
					p->my_job->remaining_c,p->my_job->release_time,p->my_job->deadline,p->pid,p->partition_id);
		}
		nextcpu = next_cpu_rt_fair(nextcpu, p_sched);
	}
}

/*void print_expired_tasks_on_each_cpu(struct partition_scheduling *p_sched)
{
	int firstcpu,nextcpu;
	struct list_head *ptr;
	struct rq *rq;
	struct task_struct *p;
	firstcpu = get_first_partition_cpu(p_sched);
	rq = cpu_rq(firstcpu);
	printk(KERN_ALERT "cpu%d的expired队列",firstcpu);
	if(!list_empty(&rq->partition.expired_list))
		list_for_each(ptr, &rq->partition.expired_list)
		{
			p = list_entry(ptr, struct task_struct, partition_link);
			printk(KERN_ALERT "release_time:%llu",p->my_job->release_time);
		}
	else
	{
		printk(KERN_ALERT "list is empty");
	}
	nextcpu = next_cpu_rt_fair(firstcpu, p_sched);
	while(nextcpu != firstcpu)
	{
		rq = cpu_rq(nextcpu);
		printk(KERN_ALERT "cpu%d的expired队列",nextcpu);
		if(!list_empty(&rq->partition.expired_list))
			list_for_each(ptr, &rq->partition.expired_list)
			{
				p = list_entry(ptr, struct task_struct, partition_link);
				printk(KERN_ALERT "release_time:%llu",p->my_job->release_time);
			}
		else
			printk(KERN_ALERT "list is empty");
		nextcpu = next_cpu_rt_fair(nextcpu, p_sched);
	}
}*/

//按照cpu利用率排序非空任务，并分配给固定的cpu
void allocate_tasks_on_cpus(struct partition_scheduling *p_sched)
{

	printk(KERN_ALERT "allocate_tasks_on_cpus start");
	allocate_tasks(p_sched);//按着WFD算法分配任务到各个cpu上
	printk("tasks on each cpu");
	print_tasks_on_each_cpu(p_sched);
	printk(KERN_ALERT "allocate_task_on_cpus end");
} 

//初始化ready_list上的任务，release_time,deadline,next_release_time,next_deadline
void init_ready_job_on_partition_rq(struct partition_scheduling *p_sched)
{
	int i;
	struct rq *rq;
	struct list_head *ptr;
	struct task_struct *p;
	for(i=0; i<p_sched->total_num_cpu; i++)
	{
		if(p_sched->cpu_bitmap[i])
		{
			rq = cpu_rq(i);
			list_for_each(ptr, &rq->partition.ready_list)
			{
				p = list_entry(ptr, struct task_struct, partition_link);
				p->my_job->release_time = rq->partition.partition_cpu_tick;
				p->my_job->deadline = p->p;
				p->my_job->next_release_time = p->my_job->deadline;
				p->my_job->next_deadline = p->my_job->next_release_time + p->p;
			}
		}
		
	}
}

void update_data(struct rq *rq,unsigned long long time)
{
	
}

static void enqueue_task_partition(struct rq *rq, struct task_struct *p, int wakeup, bool head)
{  
     // 待实现
	struct list_head *ptr;
	struct task_struct *p_compare;
	struct partition_scheduling *p_sched;
	p_sched = get_partition_scheduling();
	printk(KERN_ALERT "enqueue_task_partition activate_flag:%d cpu:%d deadline:%llu",p_sched->activate_flag,rq->cpu,p->my_job->deadline);
	if(p_sched->activate_flag == 0)//加入队列的尾部，一般情况
		list_add_tail(&p->partition_link, &rq->partition.ready_list);
	else//flag=1,按照remaining_c从小到大加入队列
	{
	  if(!list_empty(&rq->partition.ready_list))
		list_for_each(ptr, &rq->partition.ready_list)
		{
			
			p_compare = list_entry(ptr, struct task_struct, partition_link);
			//printk(KERN_ALERT "I am in list_for_each pdeadline:%llu pcomdeadline:%llu",p->my_job->deadline,p_compare->my_job->deadline);
			//if(p->my_job->remaining_c < p_compare->my_job->remaining_c)
			if(p->my_job->deadline < p_compare->my_job->deadline)
			{
				list_add_tail(&p->partition_link, &p_compare->partition_link);
				break;
			}
		}
	  else
		printk(KERN_ALERT "ready_list is empty");
	}
}

static void dequeue_task_partition(struct rq *rq, struct task_struct *p, int sleep){

     // 待实现
	list_del_init(&p->partition_link);
}
struct task_struct *get_next_task_partition(struct rq *rq,struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct rq *src_rq = NULL;
	struct task_struct *idle_task = NULL;
	spin_lock(&rq->partition.lock);
	list_for_each(ptr,&rq->partition.ready_list)
	{
		idle_task = list_entry(ptr, struct task_struct, partition_link);
		if(idle_task != NULL)
		{
			printk(KERN_ALERT "PARTITION:line:%d file:%s",__LINE__,__FILE__);
			printk(KERN_ALERT "list is not empty  partition_id:%d,rq->cpu:%d ,lcm:%llu",idle_task->partition_id,rq->cpu,p_sched->lcm);
		}
		else
			printk(KERN_ALERT "idle_task is NULL");		
	}
	spin_unlock(&rq->partition.lock);
	
	 if(idle_task != NULL)
	{
     	src_rq = task_rq(idle_task);
        if(src_rq != rq)	
		{
        	while( task_running(src_rq, idle_task) )
			{
           		mb();
        	}
		}
    }
	deactivate_task(src_rq, idle_task, 0);
    set_task_cpu(idle_task, rq->cpu);
    activate_task(rq, idle_task, 0);
	if(list_empty(&rq->partition.ready_list) && p_sched->lcm <20)
	{
		printk(KERN_ALERT "%d list is empty,lcm:%llu",rq->cpu,p_sched->lcm);
		
	}
	return idle_task;
}

static struct task_struct* pick_next_task_partition(struct rq *rq)
{
	struct list_head *ptr;
	struct task_struct *idle_task;
	struct partition_scheduling *p_sched;
	idle_task = NULL;
	p_sched = get_partition_scheduling();
	if(p_sched->turn_on)
	{
		if(p_sched->cpu_bitmap[rq->cpu] && rq->partition.ready)
		{
			spin_lock(&rq->partition.lock);
			if(!list_empty(&rq->partition.ready_list))
			{
				list_for_each(ptr,&rq->partition.ready_list)
				{
					idle_task = list_entry(ptr, struct task_struct, partition_link);
					break;
				}
			}	
			spin_unlock(&rq->partition.lock);
			//if(idle_task != NULL)
			//	if(p_sched->lcm < 50)
			//		printk(KERN_ALERT "cpu%d id:%d ",rq->cpu,idle_task->partition_id);
			return idle_task;		
		}
		else
			return NULL;
	}
	else
		return NULL;
}

void add_task_to_expired_by_release_time(struct rq *rq, struct task_struct *p)
{
	struct list_head *ptr;
	struct task_struct *order_p;
	rq->partition.tail_flag = 0;
	if(list_empty(&rq->partition.expired_list))
		list_add_tail(&p->partition_link, &rq->partition.expired_list);
	else
	{
		list_for_each(ptr, &rq->partition.expired_list)
		{
			order_p = list_entry(ptr, struct task_struct, partition_link);
			if(p->my_job->release_time < order_p->my_job->release_time)
			{
				rq->partition.tail_flag = 1;
				list_add_tail(&p->partition_link, &order_p->partition_link);
				break;
			}
		}
		if(rq->partition.tail_flag == 0)
			list_add_tail(&p->partition_link, &rq->partition.expired_list);
	}
}


void move_task_from_ready_to_expired(struct rq *rq,struct task_struct *p)
{
	p->my_job->release_time = p->my_job->next_release_time;
	p->my_job->deadline = p->my_job->next_deadline;
	p->my_job->next_release_time = p->my_job->release_time + p->p;
	p->my_job->next_deadline = p->my_job->deadline + p->p;
	p->my_job->remaining_c = p->c_real;
	//spin_lock(&rq->partition.lock);
	list_del(&p->partition_link);
	//spin_unlock(&rq->partition.lock);
	add_task_to_expired_by_release_time(rq,p);
}

void move_task_from_expired_to_ready(struct rq *rq, struct task_struct *expired_p, struct partition_scheduling *p_sched)
{
	list_del(&expired_p->partition_link);
	enqueue_task_partition(rq, expired_p, 1, true);
	
}

void print_task_in_ready_expired(struct rq *rq)
{
	struct list_head *ptr;
	struct task_struct *p;
	if(!list_empty(&rq->partition.ready_list))
	{
		list_for_each(ptr, &rq->partition.ready_list)		
		{
			p = list_entry(ptr, struct task_struct, partition_link);
			printk(KERN_ALERT "ready_list cpu:%d remaining_c:%llu release_time:%llu deadline:%llu partition_id:%d real_id:%d",rq->cpu,p->my_job->remaining_c,p->my_job->release_time,p->my_job->deadline,p->partition_id,p->pid);
		}
	}
	if(!list_empty(&rq->partition.expired_list))
	{
		list_for_each(ptr, &rq->partition.expired_list)
		{
			p = list_entry(ptr, struct task_struct, partition_link);
			printk(KERN_ALERT "expired_list cpu:%d release_time:%llu deadline:%llu partition_id:%d real_id:%d",rq->cpu,p->my_job->release_time,
								p->my_job->deadline,p->partition_id,p->pid);
		}
	}
	
}


void move_p_to_other_cpu(struct task_struct *p,int cpu_num)
{
	struct rq *src_rq;
	struct rq *des_rq;
	src_rq = task_rq(p);
	des_rq = cpu_rq(cpu_num);
	if(src_rq != des_rq)
	{       
        while( task_running(src_rq, p) )
		{
            mb();
        }
    }
	spin_lock(&src_rq->partition.lock);
	spin_lock(&des_rq->partition.lock);
	deactivate_task(src_rq, p, 0);
    /set_task_cpu(p, des_rq->cpu);//设置任务在指定cpu上运行
	activate_task(des_rq, p, 0);
	spin_unlock(&des_rq->partition.lock);
	spin_unlock(&src_rq->partition.lock);
}

static void task_tick_partition(struct rq *rq, struct task_struct *p, int queued)
{    
    //tick重要函数，待实现，tick处理都在这个函数中
	//struct list_head *ptr;
	//struct task_struct *expired_p;
	//struct task_struct *ready_p;
	struct partition_scheduling *p_sched;
	p_sched =get_partition_scheduling();
	if(p_sched->cpu_bitmap[rq->cpu])
	{
		if(p_sched->lcm < 2000)
			printk(KERN_ALERT "I am in task_tick_partition cpu:%d tick:%d remaining_c:%llu deadline:%llu release_time:%llu idle_flag:%d ",rq->cpu,
				rq->partition.partition_cpu_tick,p->my_job->remaining_c,p->my_job->deadline,p->my_job->release_time,p->idle_flag);
		rq->partition.partition_cpu_tick++;
		if( (p->idle_flag == 0) && (p->my_job->remaining_c > 0))
		//if(p->idle_flag == 0)
			p->my_job->remaining_c--;
		mb();
		if(p->my_job->remaining_c == 0)
		{
			if(p_sched->lcm < 2000)
				printk(KERN_ALERT "move_task_from_ready_to_expired cpu:%d",rq->cpu);
			spin_lock(&rq->partition.lock);
			move_task_from_ready_to_expired(rq, p);
			spin_unlock(&rq->partition.lock);
			print_task_in_ready_expired(rq);
		}
		//因为list_for_each的原因，这个循环的删除操作存在问题，需要修改(已修改)
		//spin_lock(&p_sched->lock);
		if(!list_empty(&rq->partition.expired_list))
		{	list_for_each(rq->partition.ptr1, &rq->partition.expired_list)
			{
				rq->partition.expired_p = list_entry(rq->partition.ptr1, struct task_struct, partition_link);
				if(rq->partition.expired_p->my_job->release_time == rq->partition.partition_cpu_tick)
				{
					rq->partition.num_from_expired_to_ready++;
				}
				else
				{
					break;
				}	
			}
			while(rq->partition.num_from_expired_to_ready > 0)
			{
				list_for_each(rq->partition.ptr1, &rq->partition.expired_list)
				{
					rq->partition.expired_p = list_entry(rq->partition.ptr1, struct task_struct, partition_link);
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "move_task_from_expired_to_ready cpu:%d",rq->cpu);
					spin_lock(&rq->partition.lock);
					move_task_from_expired_to_ready(rq, rq->partition.expired_p, p_sched);
					spin_unlock(&rq->partition.lock);
					if(p_sched->lcm < 2000)
						print_task_in_ready_expired(rq);
					break;
				}
				rq->partition.num_from_expired_to_ready--;
			}
		}
		else
		{
			if(p_sched->lcm < 1000)
				printk(KERN_ALERT "expired_list is empty cpu:%d",rq->cpu);
		}
		//spin_unlock(&p_sched->lock);
		
	}
		
	if(rq->cpu == 0)
		p_sched->lcm++; 

	//migration
	spin_lock(&p_sched->lock);
	if(rq->partition.partition_cpu_tick == 1800)
	{
		mb();
		if(p_sched->lcm < 2000)
						printk(KERN_EMERG "tick is 1800 cpu:%d",rq->cpu);
		mb();
		if(!list_empty(&rq->partition.ready_list))		
		{
			mb();
			list_for_each(rq->partition.ptr2, &rq->partition.ready_list)
			{
				rq->partition.ready_p = list_entry(rq->partition.ptr2, struct task_struct, partition_link);
				//1、待完善				
				/*rq->partition.num_task++;
				if(rq->partition.num_task == 2 && rq->partition.ready_p->idle_flag == 0)
				{
					mb();
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "before move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					//print_task_in_ready_expired(cpu_rq((rq->cpu)+1));
					move_p_to_other_cpu(rq->partition.ready_p,(rq->cpu)+1);
					//deactivate_task(task_rq(rq->partition.ready_p), rq->partition.ready_p, 0);
					//list_del(&rq->partition.ready_p->partition_link);
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "after move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					break;
					//print_task_in_ready_expired(cpu_rq((rq->cpu)+1));
					
				}*/
				//2、可运行
				/*if(rq->partition.ready_p->idle_flag == 0)
				{
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "before move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					print_task_in_ready_expired(cpu_rq(6));
					if(rq->cpu != 6)
					{
						spin_lock(&rq->partition.lock);
						spin_lock(&cpu_rq(6)->partition.lock);
						list_del_init(&rq->partition.ready_p->partition_link);
						enqueue_task_partition(cpu_rq(6), rq->partition.ready_p, 1, true); 
						spin_unlock(&cpu_rq(6)->partition.lock);
						spin_unlock(&rq->partition.lock);
					}
					
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "after move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					print_task_in_ready_expired(cpu_rq(6));
					break;
				}*/
				//3、new
				if(rq->partition.ready_p->idle_flag == 0)
				{
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "before move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					print_task_in_ready_expired(cpu_rq(6));
					if(rq->cpu != 6)
					{						
						move_p_to_other_cpu(rq->partition.ready_p,6);			
					}
					if(p_sched->lcm < 2000)
						printk(KERN_ALERT "after move_p_to_other_cpu cpu:%d",rq->cpu);
					print_task_in_ready_expired(rq);
					print_task_in_ready_expired(cpu_rq(6));
					break;
				}
					
			}
			
		}
	}
	spin_unlock(&p_sched->lock);


	set_tsk_need_resched(rq->curr);
	
}

static void yield_task_partition(struct rq *rq)
{

    return ;
}

static void check_preempt_curr_partition(struct rq *rq, struct task_struct *p, int flags)
{

    resched_task(rq->curr);

    return ;
}

static void put_prev_task_partition(struct rq *rq, struct task_struct *p)
{
    return ;
}
static void set_curr_task_partition(struct rq *rq)
{
   
    return ;
}


static void prio_changed_partition(struct rq *rq, struct task_struct *p,
			    int oldprio, int running)
{

    if(running)
    resched_task(rq->curr);

    return ;
}

static void switched_to_partition(struct rq *rq, struct task_struct *p,
			   int running)
{
    if(running)
    resched_task(rq->curr);
   
  
    return ;
}


static int select_task_rq_partition(struct task_struct *p, int sd_flag, int flags)
{
    int cpu ;
    cpu = smp_processor_id() ;

    return cpu;
}


static const struct sched_class partition_sched_class = {
	.next 			= &rt_sched_class,
	.enqueue_task		= enqueue_task_partition,
	.dequeue_task		= dequeue_task_partition,
        .yield_task		= yield_task_partition,

	.check_preempt_curr	= check_preempt_curr_partition,

	.pick_next_task		= pick_next_task_partition,
	.put_prev_task		= put_prev_task_partition,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_partition,
#endif
	.set_curr_task          = set_curr_task_partition,
	.task_tick		= task_tick_partition,
	.prio_changed		= prio_changed_partition,
	.switched_to		= switched_to_partition,
};
