#pragma once
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<exception>
#include<iostream>
#include<unistd.h>


#define DEFAULT_TIME            10      //检测时间
#define DEFAULT_STEP            10      //创建或销毁线程的步长
#define DEFAULT_MAX_THR_NUM     32      //默认最大线程数
#define DEFAULT_MIN_THR_NUM     4       //默认最小线程数
#define DEFAULT_MAX_QUEUE_SIZE  4096    //默认最大任务数


template<typename T>
class threadpool
{
public:
    threadpool(int max_thr_num = DEFAULT_MAX_THR_NUM, int min_thr_num = DEFAULT_MIN_THR_NUM, int max_queue_size = DEFAULT_MAX_QUEUE_SIZE);
    ~threadpool();

    //添加任务
    bool add_task(T* task);
    //工作线程，pos表示其在线程集合中的位置
    static void worker(threadpool<T>* thi, int pos);
    static void manager(threadpool<T>* thi);
private:
    int m_max_thread_num;                       //最大线程数
    int m_min_thread_num;                       //最小线程数
    int m_busy_thread_num;                      //当前繁忙线程数
    int m_live_thread_num;                      //当前存活线程数
    int m_wait_destory_thread_num;              //等待销毁线程数

    std::thread** m_work_threads;                //工作线程
    std::thread* m_manage_thread;               //管理者线程

    std::queue<T*> m_task_queue;                //任务队列
    int m_max_queue_size;                       //最大任务数

    std::mutex m_pool_mutex;                    //线程池锁
    std::condition_variable m_queue_not_empty;  //条件变量,队列不为空
    std::condition_variable m_queue_not_full;   //条件变量,队列不为满

    bool shutdown;                              //是否关闭线程池

};

//---------------------------线程池实现------------------------------//

template<typename T>
void threadpool<T>::manager(threadpool<T>* thi)
{
    while (thi->shutdown == false)
    {
        //定时管理
        sleep(DEFAULT_TIME);
        std::cout << "----------admin work--------------" << std::endl;

        //线程与队列相关参数，用来控制线程数量
        int queue_size;
        int live_num;
        int busy_num;

        {
            std::unique_lock<std::mutex> uni_mutex(thi->m_pool_mutex);
            queue_size = thi->m_task_queue.size();
            live_num = thi->m_live_thread_num;
            busy_num = thi->m_busy_thread_num;
            std::cout
                << "queue_size: " << queue_size
                << "\nlive_num: " << live_num
                << "\nbusy_num:" << busy_num << std::endl;
        }

        //当任务数 > 4*存活线程数,增加线程
        if (queue_size > 4 * live_num)
        {
            std::unique_lock<std::mutex> uni_mutex(thi->m_pool_mutex);
            int addnum = 0;
            for (int i = 0;live_num < thi->m_max_thread_num, addnum < DEFAULT_STEP, i < thi->m_max_thread_num;++i)
            {
                if (thi->m_work_threads[i] == nullptr)
                {
                    thi->m_work_threads[i] = new std::thread(worker, thi, i);
                    if (!thi->m_work_threads[i]) throw std::exception();
                    std::cout << "creat " << thi->m_work_threads[i]->get_id() << " worker pthread successfully" << std::endl;
                    thi->m_work_threads[i]->detach();
                    thi->m_live_thread_num++;
                    addnum++;
                }
            }
        }

        //当忙碌线程*2 < 存活线程
        if (2 * busy_num<live_num && live_num>thi->m_min_thread_num)
        {
            {
                std::unique_lock<std::mutex> uni_mutex(thi->m_pool_mutex);
                thi->m_wait_destory_thread_num = DEFAULT_STEP;
            }

            for (int i = 0;i < DEFAULT_STEP;++i)
                thi->m_queue_not_empty.notify_one();
        }

        std::cout << "---------admin workend------------" << std::endl;
    }
}

template<typename T>
void threadpool<T>::worker(threadpool<T>* thi, int pos)
{
    while (true)
    {
        T* task = nullptr;
        {
            std::unique_lock<std::mutex> uni_mutex(thi->m_pool_mutex);
            //队列为空时，等待任务
            while (thi->shutdown == false && thi->m_task_queue.size() == 0)
            {
                thi->m_queue_not_empty.wait(uni_mutex);
                //管理者线程也会唤醒线程以销毁多余线程
                if (thi->m_wait_destory_thread_num > 0)
                {
                    thi->m_wait_destory_thread_num--;
                    if (thi->m_live_thread_num > thi->m_min_thread_num)
                    {
                        std::cout << "thread " << pthread_self() << " is exiting" << std::endl;
                        thi->m_live_thread_num--;
                        delete thi->m_work_threads[pos];
                        return;
                    }
                }
            }
            //线程池关闭
            if (thi->shutdown == true)
            {
                std::cout << "thread " << thi->m_work_threads[pos]->get_id() << " is exiting" << std::endl;
                thi->m_live_thread_num--;
                delete thi->m_work_threads[pos];
                return;
            }
            //取任务
            task = thi->m_task_queue.front();
            thi->m_task_queue.pop();
            thi->m_busy_thread_num++;
            std::cout << "m_busy_thread_num:" << thi->m_busy_thread_num << std::endl;
        }

        //task中的业务处理
        if (task)
            task->process();
        {
            std::unique_lock<std::mutex> uni_mutex(thi->m_pool_mutex);
            thi->m_busy_thread_num--;
        }
    }
}

template<typename T>
threadpool<T>::threadpool(int max_thr_num, int min_thr_num, int max_queue_size) :
    m_max_thread_num(max_thr_num), m_min_thread_num(min_thr_num), m_busy_thread_num(0),
    m_live_thread_num(min_thr_num), m_wait_destory_thread_num(0), m_max_queue_size(max_queue_size),
    m_work_threads(nullptr), m_manage_thread(nullptr), shutdown(false)
{
    //初始化参数错误，抛出异常
    if ((m_max_thread_num < m_min_thread_num) || (m_max_thread_num <= 0) || (m_min_thread_num < 0) || (m_max_queue_size <= 0))
        throw std::exception();

    //初始化管理者线程
    m_manage_thread = new std::thread(manager, this);
    if (!m_manage_thread) throw std::exception();
    std::cout << "creat manager threade successfully" << std::endl;

    //初始化工作线程集合
    m_work_threads = new std::thread * [max_thr_num];
    if (!m_work_threads) throw std::exception();
    for (int i = 0;i < min_thr_num;++i)
    {
        m_work_threads[i] = new std::thread(worker, this, i);
        if (!m_work_threads[i]) throw std::exception();
        std::cout << "creat " << m_work_threads[i]->get_id() << " worker pthread successfully" << std::endl;
        m_work_threads[i]->detach();
    }

    std::cout << "creat threadpool successfully" << std::endl;

}

template<typename T>
threadpool<T>::~threadpool()
{
    shutdown = true;

    m_manage_thread->join();
    delete m_manage_thread;

    //循环唤醒工作线程，结束其工作
    while (m_live_thread_num > 0)
        m_queue_not_empty.notify_one();
    delete[] m_work_threads;

    std::cout << "pthreadpool shutdown" << std::endl;
}

template<typename T>
bool threadpool<T>::add_task(T* task)
{

    std::unique_lock<std::mutex> uni_mutex(m_pool_mutex);
    //队列满时，阻塞，直至队列不为满或线程池将关闭
    while (shutdown == false && m_task_queue.size() == m_max_queue_size)
        m_queue_not_full.wait(uni_mutex);

    if (shutdown == true) return false;

    //添加任务，并唤醒等待任务的线程
    m_task_queue.push(task);
    m_queue_not_empty.notify_one();
    return true;;
}
