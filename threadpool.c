#include "threadpool.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int debug = 0;
const int NUMBER = 2;
// 任务结构体
typedef struct Task
{
	void (*function)(void* arg);
	void* arg;
}Task;

// 线程池结构体
struct ThreadPool
{
	// 任务队列
	Task* taskQ;
	int queueCapacity; // 容量
	int queueSize; //当前任务个数
	int queueFront; // 队头
	int queueRear; // 队尾

	pthread_t managerID;  // 管理者线程ID
	pthread_t* threadIDs; // 工作线程ID数组
	int minNum;   // 工作的最小线程数
	int maxNum;   // 工作的最大线程数
	int busyNum;   // 忙线程数
	int liveNum; // 存活线程数
	int exitNum;  // 要销毁的线程数
	pthread_mutex_t mutexPool; // 锁整个线程池
	pthread_mutex_t mutexBusy; // 锁busyNum变量
	pthread_cond_t notFull;  // 任务队列是否满（生产者条件变量）
	pthread_cond_t notEmpty;  // 任务队列是否空（消费者条件变量）

	int shutdown;  // 是不是要销毁线程池，1为销毁，0为不销毁
};

ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	// 创建线程池结构体
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (pool == NULL) {
			printf("malloc threadpool fail...\n");
			break;
		}
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL) {
			if (debug)
				printf("malloc threadIDs fail...\n");
			break;
		}
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);  // 工作线程数组清零

		// 属性初始化
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;

		if (pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0) {
			if (debug)
				printf("mutex or condition init fail...\n");
			break;
		}

		// 任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		// 线程创建
		pthread_create(&pool->managerID, NULL, manager, pool); // 只创建一个管理者线程
		int i;
		for (i = 0; i < min; i++)
		{
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}
		return pool;
	} while (0);
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool) {
		free(pool);
	}
	return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL) {
		return -1;
	}

	// 关闭线程池
	pool->shutdown = 1;  // 管理者线程不在向下执行
	// 唤醒阻塞的消费者线程
	int i;
	for (i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	// 阻塞回收管理者线程
	pthread_join(pool->managerID, NULL);  // 回收管理者线程
	// 释放堆内存
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);
	free(pool);
	pool = NULL;
	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
		// 阻塞生产者线程
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown) { // 线程池要关闭，此时不能往下添加任务了
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	// 添加任务
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	pthread_cond_signal(&pool->notEmpty);  // 唤醒工作线程
	pthread_mutex_unlock(&pool->mutexPool);

}

int getThreadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int getThreadPoolLiveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int liveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return liveNum;
}

void* worker(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (1) {
		pthread_mutex_lock(&pool->mutexPool);
		// 当前任务队列是否为空
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			// 阻塞工作线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			// 判断是否要销毁线程
			if (pool->exitNum > 0) {
				pool->exitNum--;
				// 引导线程自杀时，需要满足活着的线程数大于最小线程数时才杀死线程
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}
		// 判断线程池是否被关闭
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		// 从任务队列中取出任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;

		// 移动头结点（循环队列）
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;

		pthread_cond_signal(&pool->notFull);  // 唤醒生产者线程
		pthread_mutex_unlock(&pool->mutexPool); // 解锁

		if (debug)
			printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.function(task.arg);  // 消费者消耗线程
		if (task.arg) {
			free(task.arg);   // 传入的agr实参是堆空间数据
			task.arg = NULL;
		}
		if (debug)
			printf("thread %ld end working...\n", pthread_self());

		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
	return NULL;
}

void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)  // 线程池不被销毁时
	{
		sleep(3);

		// 取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		// 取出忙线程的数量
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		// 添加线程 （当前任务个数 > 存活的线程个数 && 存活的线程数 < 最大线程数）
		if (queueSize > liveNum && liveNum < pool->maxNum) {
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			int i;
			for (i = 0; i < pool->maxNum && counter < NUMBER
				&& pool->liveNum < pool->maxNum; ++i) {
				if (pool->threadIDs[i] == 0) {
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		// 销毁线程 （忙的线程数 * 2 < 存活的线程数 && 存活线程数 > 最小线程数）
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			// 让工作的线程自杀,唤醒工作线程NUMBER次（只是引导线程自杀不是强制性的）
			int i;
			for (i = 0; i < NUMBER; ++i) {
				pthread_cond_signal(&pool->notEmpty);  // 唤醒工作线程
			}
		}

	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	int i;
	for (i = 0; i < pool->maxNum; ++i) {
		if (pool->threadIDs[i] == tid) {
			pool->threadIDs[i] = 0;
			if (debug)
				printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
