#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/timer.h"

template <typename T>
class threadpool
{
public:
	threadpool(int actor_model, sql_connection_pool* sql_pool, int thread_number = 8, int max_requests = 10000);
	~threadpool();
	bool append(T* request);
	bool append(T* request, int state);
	static void* worker(void* arg);
	void run();
private:
	int m_thread_number;	//number of threads in the pool
	int m_max_requests;		//max number of requests in the queue
	pthread_t* m_threads;	//threads array
	std::list<T*> m_workqueue; //request queue
	locker m_queuelocker;	//request queue mutex
	sem m_queuestat;		//whether have works to be process
	bool m_stop;
	sql_connection_pool* m_sql_pool;
	int m_actor_model;
};

template <typename T>
threadpool<T>::threadpool(int actor_model, sql_connection_pool* sql_pool, int thread_number, int max_requests):
	m_actor_model(actor_model), m_sql_pool(sql_pool), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_stop(false)
{
	if(thread_number <= 0 || max_requests <= 0)
		throw std::exception();

	m_threads = new pthread_t[m_thread_number];
	if(!m_threads)
		throw std::exception();

	for (int i = 0; i < thread_number; ++i)
	{
		if(pthread_create(&m_threads[i], NULL, worker, this) != 0) {
			delete []m_threads;
			throw std::exception();
		}
		if(pthread_detach(m_threads[i]) != 0) {
			delete []m_threads;
			throw std::exception();
		}
	}
}
template <typename T>
threadpool<T>::~threadpool() {
	delete []m_threads;
	m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request) {
	m_queuelocker.lock();
	if(m_workqueue.size() >= m_max_requests) {
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	//printf("workqueue size = %ld\n", m_workqueue.size());
	return true;
}

template <typename T>
bool threadpool<T>::append(T* request, int state) {
	m_queuelocker.lock();
	if(m_workqueue.size() >= m_max_requests) {
		m_queuelocker.unlock();
		return false;
	}
	request->m_state = state;
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	
	return true;
}

//as a static function, it must access the member of class through a static member or this ptr
template <typename T>
void* threadpool<T>::worker(void* arg) {
	threadpool* pool = (threadpool*)arg;
	pool->run();
	return pool;
}
template <typename T>
void threadpool<T>::run() {	//proactor
	while(!m_stop) {
		m_queuestat.wait();
		m_queuelocker.lock();
		if(m_workqueue.empty()) {
			m_queuelocker.unlock();
			continue;
		}
		T* request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();
		if(!request)
			continue;

		if(m_actor_model == 0) {
			connectionRAII connRAII(request->m_sql_conn, m_sql_pool);
			request->process();
		}
		else {
			if(request->m_state == 0) {
				if(request->read()) {
					//server->refresh_timer(request->m_timer);
					request->m_flag = 1;
					connectionRAII connRAII(request->m_sql_conn, m_sql_pool);
					request->process();
				}
				else {
					//server->process_timer(request->m_timer, request->m_sockfd);
					request->m_flag = 2;
				}
			}
			else {
				if(request->write()) {
					request->m_flag = 1;
				}
				else {
					request->m_flag = 2;
				}
			}
		}
	}
}
#endif