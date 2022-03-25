#include"../header/thread_pool.h"


//创建线程池
threadpool_t* threadpool_create(int min_thr_num, int max_thr_num, int queue_max_num)
{
    threadpool_t* thp = NULL;
    //初始化出错时，直接跳转销毁线程池。相当于goto
    do
    {
        if ((thp = (threadpool_t*) malloc(sizeof(threadpool_t))) == NULL)
        {
            printf("malloc threadpool error\n");
            break;
        }

        thp->min_thr_num = min_thr_num;
        thp->max_thr_num = max_thr_num;
        thp->busy_thr_num = 0;
        thp->live_thr_num = min_thr_num;

        thp->queue_size = 0;
        thp->queue_max_size = queue_max_num;
        thp->queue_head = 0;
        thp->queue_tail = 0;
        thp->shutdown = false;

        //根据最大线程数，开辟线程池中的工作线程空间
        thp->threads = (pthread_t*) malloc(sizeof(pthread_t) * max_thr_num);
        if (thp->threads == NULL)
        {
            printf("malloc threads error\n");
            break;
        }

        //任务队列空间
        thp->task_queue = (threadpool_task_t*) malloc(sizeof(threadpool_task_t) * queue_max_num);
        if (thp->threads == NULL)
        {
            printf("malloc task_queue error\n");
            break;
        }

        //初始化互斥锁和条件变量
        if (pthread_mutex_init(&thp->lock, NULL) != 0
            || pthread_mutex_init(&thp->thread_busy_cnt, NULL) != 0
            || pthread_cond_init(&thp->queue_not_empty, NULL) != 0
            || pthread_cond_init(&thp->queue_not_full, NULL) != 0)
        {
            printf("malloc lock error\n");
            break;
        }

        //启动线程
        for (int i = 0;i < min_thr_num;++i)
        {
            pthread_create(&(thp->threads[i]), NULL, work_thread, (void*) thp);
            pthread_detach(thp->threads[i]);
            printf("start thread %u...\n", (unsigned int) thp->threads[i]);
        }
        pthread_create(&(thp->manage_thread), NULL, manage_thread, (void*) thp);
        return thp;
    } while (0);
    threadpool_free(thp);
    return NULL;
}

//线程池中添加任务
int threadpool_add(threadpool_t* thp, void* (*function)(void* arg), void* arg)
{
    pthread_mutex_lock(&(thp->lock));
    //队列满时
    while ((thp->queue_size == thp->queue_max_size) && (thp->shutdown == false))
    {
        pthread_cond_wait(&(thp->queue_not_full), &(thp->lock));
    }
    if (thp->shutdown == true)
    {
        pthread_mutex_unlock(&thp->lock);
        return -1;
    }
    //释放队尾回调函数参数
    // if (thp->task_queue[thp->queue_tail].arg != NULL)
    // {
    //     free(thp->task_queue[thp->queue_tail].arg);
    //     thp->task_queue[thp->queue_tail].arg = NULL;
    // }
    //添加任务进队尾
    thp->task_queue[thp->queue_tail].function = function;
    thp->task_queue[thp->queue_tail].arg = arg;
    thp->queue_tail = (thp->queue_tail + 1) % thp->queue_max_size;
    ++thp->queue_size;
    //添加任务后，队列不为空，唤醒工作线程
    pthread_cond_signal(&(thp->queue_not_empty));
    pthread_mutex_unlock(&(thp->lock));

    return 0;
}

//工作线程
void* work_thread(void* threadpool)
{
    threadpool_t* thp = (threadpool_t*) threadpool;
    threadpool_task_t task;
    while (true)
    {
        //锁住线程池
        pthread_mutex_lock(&(thp->lock));
        //任务队列中没有任务且不关闭线程池时，阻塞等待分配任务
        while ((thp->queue_size == 0) && (thp->shutdown == false))
        {
            //等待任务
            // printf("thred 0x%x is waiting\n", (unsigned int) pthread_self());
            pthread_cond_wait(&(thp->queue_not_empty), &(thp->lock));

            //线程冗余时
            if (thp->wait_exti_thr_num > 0)
            {
                --thp->wait_exti_thr_num;
                //如果线程池中线程个数大于最小值，可以结束当前线程
                if (thp->live_thr_num > thp->min_thr_num)
                {
                    printf("thread 0x%x is exiting\n", (unsigned int) pthread_self());
                    --thp->live_thr_num;
                    pthread_mutex_unlock(&(thp->lock));
                    pthread_exit(NULL);
                }
            }
        }
        //关闭线程池时，要关闭每一个线程
        if (thp->shutdown == true)
        {
            printf("thread 0x%x is exiting by shutdown\n", (unsigned int) pthread_self());
            --thp->live_thr_num;
            pthread_mutex_unlock(&(thp->lock));
            pthread_exit(NULL);
        }
        //从任务队列中获取任务
        task.function = thp->task_queue[thp->queue_head].function;
        task.arg = thp->task_queue[thp->queue_head].arg;
        //任务出队(环形队列)
        thp->queue_head = (thp->queue_head + 1) % thp->queue_max_size;
        --thp->queue_size;
        //通知可以添加新任务进入队列
        pthread_cond_signal(&(thp->queue_not_full));
        //取出任务后，可以释放线程锁
        pthread_mutex_unlock(&(thp->lock));

        //开始执行任务
        // printf("thread 0x%x is working\n", (unsigned int) pthread_self());
        //busy线程+1
        pthread_mutex_lock(&(thp->thread_busy_cnt));
        ++thp->busy_thr_num;
        pthread_mutex_unlock(&(thp->thread_busy_cnt));

        //执行业务逻辑
        task.function(task.arg);

        //任务结束 
        // printf("thread 0x%x end work\n", (unsigned int) pthread_self());
        //busy线程-1
        pthread_mutex_lock(&(thp->thread_busy_cnt));
        --thp->busy_thr_num;
        pthread_mutex_unlock(&(thp->thread_busy_cnt));
    }
    pthread_exit(NULL);
}

//管理者线程
void* manage_thread(void* threadpool)
{
    threadpool_t* thp = (threadpool_t*) threadpool;
    while (thp->shutdown == false)
    {
        //定时管理
        sleep(DEFAULT_TIME);
        printf("admin  start........\n");

        pthread_mutex_lock(&(thp->lock));
        int queue_size = thp->queue_size;
        int live_thr_num = thp->live_thr_num;
        pthread_mutex_unlock(&(thp->lock));

        pthread_mutex_lock(&(thp->thread_busy_cnt));
        int busy_thr_num = thp->busy_thr_num;
        pthread_mutex_unlock(&(thp->thread_busy_cnt));

        printf("queeue size:%d\nlive:%d\nbusy:%d\n", queue_size, live_thr_num, busy_thr_num);

        /*调度算法*/
        //添加新线程:当队列中等待的任务数量大于允许值，且可以添加线程时
        if (queue_size > MAX_WAIT_TASK_NUM && queue_size < thp->queue_max_size)
        {
            pthread_mutex_lock(&(thp->lock));
            //一次增加DEFAULT_STEP线程
            int add = 0;
            for (int i = 0;thp->live_thr_num < thp->max_thr_num
                && add < DEFAULT_STEP && i < thp->max_thr_num;++i)
            {
                if (thp->threads[i] == 0 || !is_thread_alive(thp->threads[i]))
                {
                    pthread_create(&(thp->threads[i]), NULL, work_thread, (void*) thp);
                    pthread_detach(thp->threads[i]);
                    printf("start thread %u...\n", (unsigned int) thp->threads[i]);
                    ++add;
                    ++thp->live_thr_num;
                }
            }
            pthread_mutex_unlock(&(thp->lock));
        }
        //删除线程:当busy线程数不到存活线程一半时
        if ((busy_thr_num << 1) < live_thr_num && live_thr_num > thp->min_thr_num)
        {
            pthread_mutex_lock(&(thp->lock));
            thp->wait_exti_thr_num = 10;
            pthread_mutex_unlock(&(thp->lock));

            for (int i = 0;i < DEFAULT_STEP;++i)
                pthread_cond_signal(&(thp->queue_not_empty));
        }
        printf("admin  end........\n");
    }
    pthread_exit(NULL);
}

//查看线程是否存活
int is_thread_alive(pthread_t tid)
{
    int ret = pthread_kill(tid, 0);
    if (ret == ESRCH)
    {
        return false;
    }
    return true;
}

//销毁线程池
int threadpool_destroy(threadpool_t* thp)
{
    if (thp == NULL) return -1;
    thp->shutdown = true;
    pthread_join(thp->manage_thread, NULL);
    printf("live:%d\n", thp->live_thr_num);
    printf("pthread shutdown\n");
    while (thp->live_thr_num > 0)
    {
        pthread_cond_signal(&(thp->queue_not_empty));
    }
    threadpool_free(thp);
    return 0;
}

//释放线程池资源
int threadpool_free(threadpool_t* thp)
{
    if (thp == NULL) return -1;
    if (thp->task_queue) free(thp->task_queue);
    if (thp->threads)
    {
        free(thp->threads);
        pthread_mutex_destroy(&(thp->lock));
        pthread_mutex_destroy(&(thp->thread_busy_cnt));
        pthread_cond_destroy(&(thp->queue_not_empty));
        pthread_cond_destroy(&(thp->queue_not_full));
    }
    free(thp);
    thp = NULL;
    return 0;
}


