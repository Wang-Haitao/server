#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<pthread.h>
#include<signal.h>
#include<errno.h>

#define DEFAULT_TIME            10      //检测时间
#define DEFAULT_STEP            10      //创建或销毁线程的步长
#define MAX_WAIT_TASK_NUM       10      //允许队列中等待的最大任务数量
#define true                    1
#define false                   0

typedef struct threadpool_task_t
{
    void* (*function)(void*);           //任务函数指针
    void* arg;                          //传入参数
}threadpool_task_t;                     //子线程任务结构体

//线程池相关信息
typedef struct threadpool_t
{
    pthread_mutex_t lock;               //线程池锁
    pthread_mutex_t thread_busy_cnt;    //记录busy状态线程个数的锁
    pthread_cond_t  queue_not_full;     //队列满时（false）,添加任务的线程阻塞
    pthread_cond_t  queue_not_empty;    //队列不为空时（true）,通知线程接受任务

    pthread_t* threads;                 //线程池中的线程集合
    pthread_t  manage_thread;           //管理者线程，负责创建和销毁线程
    threadpool_task_t* task_queue;      //任务队列

    int min_thr_num;                    //线程池最小线程数
    int max_thr_num;                    //线程池最大线程数
    int live_thr_num;                   //线程池存活的线程数
    int busy_thr_num;                   //线程池繁忙的线程数
    int wait_exti_thr_num;              //等待销毁的线程数

    int queue_head;                     //任务队列头下标
    int queue_tail;                     //任务队列尾下标
    int queue_size;                     //任务队列实际任务数
    int queue_max_size;                 //任务队列最大任务数

    int shutdown;                       //线程池状态，true关闭线程池
}threadpool_t;

//创建线程池
threadpool_t* threadpool_create(int min_thr_num, int max_thr_num, int queue_max_num);
//线程池中添加任务
int threadpool_add(threadpool_t* thp, void* (*function)(void* arg), void* arg);
//工作线程
void* work_thread(void* threadpool);
//管理者线程
void* manage_thread(void* threadpool);
//查看线程是否存活
int is_thread_alive(pthread_t tid);
//销毁线程池
int threadpool_destroy(threadpool_t* thp);
//释放线程池资源
int threadpool_free(threadpool_t* thp);