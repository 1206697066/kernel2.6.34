#include <linux/sched.h>
#include <linux/list.h>
#include <asm/div64.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/percpu.h>

#ifndef PARTITION_DEBUG
#define PARTITION_DEBUG
#endif

void init_partition_job(struct partition_job *new_job, struct task_struct *p);
void print_link_job(struct partition_scheduling *p_sched);

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

/*
 *initilize PARTITION_rq for each cpu, called by sched_init(void) in /kernel/sched.c when the system boots
 */

void init_partition_rq(struct partition_rq *partition_rq)
{
	spin_lock_init(&partition_rq->lock);
	partition_rq->ready = 0;
	partition_rq->curr= NULL;
	partition_rq->idle_partition_job = NULL;
	INIT_LIST_HEAD(&partition_rq->allocated_list);
	INIT_LIST_HEAD(&partition_rq->ready_list);
	#ifdef PARTITION_DEBUG
        printk(KERN_ALERT "init_partition_rq\n");
	#endif
	
}

/*把所有的非空任务添加到partition_sched结构体中的partiton_link_job中，在setpara中完成*/
void add_partition_task_list(struct rq *rq, struct task_struct *p)
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
		#ifdef PARTITION_DEBUG
		printk(KERN_ALERT "add_partition_idle_task_list");
		#endif
	}

}
//在所有的任务加入到全局结构体partition_sched结构体之前，先要初始化所有的任务
void init_partition_job(struct partition_job *new_job, struct task_struct *p)
{
	new_job->task = p;
	new_job->release_time = 0;
	new_job->deadline = 0;
	new_job->next_release_time = 0;
	new_job->next_deadline = 0;
	
	new_job->uti = (p->c_real)*1000 / (p->p);
	//new_job->uti = (double)3/(double)5;
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
	printk(KERN_ALERT "print_info in");
	for(i=0; i<p_sched->total_num_cpu;i++)
	{
		printk(KERN_ALERT "times:%d ",i);
		if(p_sched->cpu_bitmap[i])
		{
			rq = cpu_rq(i);
			
			list_for_each(ptr,&rq->partition.ready_list)
			{
				idle_task = list_entry(ptr, struct task_struct, partition_link);
				printk(KERN_ALERT "PARTITION:line:%d file:%s",__LINE__,__FILE__);
				printk(KERN_ALERT "partition_id:%d,rq->cpu:%d ",idle_task->partition_id,rq->cpu);
			}
			
		}
	}
}
void print_link_job(struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct partition_job *job;
	list_for_each(ptr, &p_sched->partition_link_job)
	{
		job = list_entry(ptr, struct partition_job, list);
		{
			printk(KERN_ALERT "PARTITION:line:%d file:%s",__LINE__,__FILE__);
			printk(KERN_ALERT "partition_id:%d ",job->task->partition_id);
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
	print_link_job(p_sched);//输出partittion_scheduling中非空任务信息
	#endif
}

void allocate_idle_tasks_on_cpus_change(struct partition_scheduling *p_sched)
{
	int i;
	struct list_head *ptr;
	struct partition_job *job;
	struct rq *src_rq;
	struct rq *des_rq;
	int cpu;
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "allocate_idle_tasks_on_cpus_change start");
	#endif	
	cpu = 1,i=0;
	list_for_each(ptr, &p_sched->partition_idle_link_job)
	{
		if(i == 1)
		{
			job = list_entry(ptr, struct partition_job, list);
			src_rq = task_rq(job->task);
			des_rq = cpu_rq(cpu);
			if(src_rq != des_rq)
			{   
				printk(KERN_ALERT "src_rq != des_rq");
		        while( task_running(src_rq, job->task) )
				{
		        	mb();
		        }
				deactivate_task(src_rq, job->task, 0);
				set_task_cpu(job->task, des_rq->cpu);//设置任务在指定cpu上运行
				activate_task(des_rq, job->task, 0);
		    }
			else
			{
				printk(KERN_ALERT "src_rq == des_rq");
				//deactivate_task(src_rq, job->task, 0);
				//set_task_cpu(job->task, des_rq->cpu);//设置任务在指定cpu上运行
				activate_task(des_rq, job->task, 0);
			}
		}
		i++;
	}
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "allocate_idle_tasks_on_cpus_change end");
	print_idle_job_in_rq(p_sched);//输出idle任务信息
	#endif
}

void print_order_list(struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct partition_job *job;
	printk(KERN_ALERT "uti of the job in order:");
	list_for_each(ptr, &p_sched->partition_order_link_job)
	{
		job = list_entry(ptr, struct partition_job, list);
		printk(KERN_ALERT "uti:%d ",job->uti);
	}	
}
//排序所有的非空任务并放到partition_order_link_job队列中
void order_link_job(struct partition_scheduling *p_sched)
{
	int flag,first=0;
	struct list_head *ptr_job;
	struct list_head *ptr_order_job;
	struct partition_job *job;//job from partition_link_job
	struct partition_job *job_order;//job from partition_order_link_job
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "order_link_job start");
	#endif
	list_for_each(ptr_job, &p_sched->partition_link_job)
	{
		flag = 0;
		job = list_entry(ptr_job, struct partition_job, list);
		if(first == 0)
		{
			list_add_tail(&job->list, &p_sched->partition_order_link_job);
			if(list_empty(&p_sched->partition_order_link_job))
				printk(KERN_ALERT "partition_order_link_job is still empty,something is wrong");
			first = 1;
			mb();
		}		
		else
		{
			list_for_each(ptr_order_job, &p_sched->partition_order_link_job)
			{
				job_order = list_entry(ptr_order_job, struct partition_job,list);
				if(job->uti > job_order->uti)// 存在比当前uti小的节点，插入到比它小的节点的前面			
				{
					list_add_tail(&job->list, &job_order->list);			
					flag = 1;//标志存在比当前uti小的节点
				}
				mb();	
			}
			if(flag == 0)//不存在比当前uti小的节点，插入到队列尾部
				list_add_tail(&job->list, &p_sched->partition_order_link_job);
		}
	}
	#ifdef PARTITION_DEBUG
	printk(KERN_ALERT "order_link_job end");
	#endif			
	printk(KERN_ALERT "PARTITION:line:%d file:%s",__LINE__,__FILE__);
	#ifdef PARTITION_DEBUG
	print_order_list(p_sched);
	#endif
	
}


void allocate_tasks(struct partition_scheduling *p_sched)
{
		
}
//按照cpu利用率排序非空任务，并分配给固定的cpu
void allocate_tasks_on_cpus(struct partition_scheduling *p_sched)
{

	order_link_job(p_sched);
	allocate_tasks(p_sched);
} 

void update_data(struct rq *rq,unsigned long long time)
{

}

static void enqueue_task_partition(struct rq *rq, struct task_struct *p, int wakeup, bool head){
     
     // 待实现
	list_add_tail(&p->partition_link, &rq->partition.ready_list);
}

static void dequeue_task_partition(struct rq *rq, struct task_struct *p, int sleep){

     // 待实现
	list_del_init(&p->partition_link);
}
struct task_struct *get_next_task_partition(struct rq *rq,struct partition_scheduling *p_sched)
{
	struct list_head *ptr;
	struct rq *src_rq;
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

/*struct task_struct *get_curr(struct rq *rq,struct partition_scheduling *p_sched)
{
	int i;
	int cpu;
	struct list_head *ptr;
	struct rq *src_rq;
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
	return idle_task;
}*/
static struct task_struct* pick_next_task_partition(struct rq *rq)
{
	struct list_head *ptr;
	struct task_struct *idle_task;
	struct partition_scheduling *p_sched;
	idle_task = NULL;
	p_sched = get_partition_scheduling();
	if(p_sched->turn_on)
	{
		if(p_sched->cpu_bitmap[rq->cpu])
		{
			spin_lock(&rq->partition.lock);
			if(!list_empty(&rq->partition.ready_list))
			{
				list_for_each(ptr,&rq->partition.ready_list)
				{
					idle_task = list_entry(ptr, struct task_struct, partition_link);
					rq->partition.curr = idle_task;		
				}
			}
			if(list_empty(&rq->partition.ready_list))
			{
				idle_task = NULL;
			}	
			spin_unlock(&rq->partition.lock);
			return idle_task;		
		}
		else
			return NULL;
	}
	else
		return NULL;
	/*debug code
	struct list_head *ptr;
	struct task_struct *idle_task;
	struct partition_scheduling *p_sched;
	idle_task = NULL;
	p_sched = get_partition_scheduling();
	if(p_sched->turn_on)
	{
		if(p_sched->cpu_bitmap[rq->cpu])
		{
			(p_sched->lcm)++;
			if(p_sched->lcm < 40)
				printk(KERN_ALERT "I am IN,cpu:%d",rq->cpu);
			spin_lock(&rq->partition.lock);
			if(!list_empty(&rq->partition.ready_list) && p_sched->lcm <40)
			{
				list_for_each(ptr,&rq->partition.ready_list)
				{
					idle_task = list_entry(ptr, struct task_struct, partition_link);
					rq->partition.curr = idle_task;
					printk(KERN_ALERT "list is not empty curr id:%d partition_id:%d,rq->cpu:%d ,lcm:%llu,state:%d",rq->curr->pid,idle_task->partition_id,rq->cpu,p_sched->lcm,idle_task->state);		
				}
			}
			if(list_empty(&rq->partition.ready_list) && p_sched->lcm <40)
			{
				printk(KERN_ALERT "list is empty,curr id:%d,partition_id:%d,cpu:%d,state:%d",rq->curr->pid,rq->curr->partition_id,rq->cpu,rq->curr->state);
				list_add_tail(&rq->partition.idle_partition_job->task->partition_link, &rq->partition.ready_list);
				idle_task = NULL;
			}	
			spin_unlock(&rq->partition.lock);
			return idle_task;		
		}
		else
		{
			if(p_sched->lcm <40)
				printk(KERN_ALERT "normal_pick_next rq->cpu:%d lcm:%llu",rq->cpu,p_sched->lcm);
			return NULL;
		}
	}
	else
		return NULL;
	*/
	
}

static void task_tick_partition(struct rq *rq, struct task_struct *p, int queued)
{    
     //tick重要函数，待实现，tick处理都在这个函数中
	struct partition_scheduling *p_sched;
	p_sched =get_partition_scheduling();
	if(p_sched->cpu_bitmap[rq->cpu])
		printk(KERN_ALERT "idle is doing:%llu cpu:%d",p_sched->lcm,rq->cpu);

	
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
