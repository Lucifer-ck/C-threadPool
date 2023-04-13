#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "threadpool.h"

void taskFunction(void* arg) {
	int num = *(int*)arg;
	printf("thread is working, number = %d, tid = %ld\n", num, pthread_self());
	//sleep(1);
}

int main()
{
	// 创建线程池 (参数为：最小线程数、最大线程数、当前需处理的任务数)
	ThreadPool* pool = threadPoolCreate(3, 10, 100);  
	for (int i = 0; i < 100; i++)
	{
		int* num = (int*)malloc(sizeof(int));
		*num = i + 100;
		threadPoolAdd(pool, taskFunction, (void*)num);

	}
	sleep(8); // 主线程等待10s后销毁线程池，若不等待，主线程会直接销毁线程池
	threadPoolDestroy(pool);
	printf("finish...\n");
	return 0;
}