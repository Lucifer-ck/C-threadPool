﻿#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;
// 创建线程池并初始化
ThreadPool* threadPoolCreate(int min, int max, int queueSize);
// 销毁线程池
int threadPoolDestroy(ThreadPool* pool);
// 给线程池添加任务
void threadPoolAdd(ThreadPool* pool, void (*func)(void*), void* arg);
// 获取线程池中工作的线程个数
int getThreadPoolBusyNum(ThreadPool* pool);
// 获取线程池中活着的线程个数
int getThreadPoolLiveNum(ThreadPool* pool);

/////////////
void* worker(void* agr);
void* manager(void* agr);
void threadExit(ThreadPool* pool);
#endif