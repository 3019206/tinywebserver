#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

//semaphore
class sem
{
private:
	sem_t m_sem;
public:
	sem() {
		if(sem_init(&m_sem, 0, 0) != 0) {
			throw std::exception();
		}
	}
	sem(int num) {
		if(sem_init(&m_sem, 0, num) != 0) {
			throw std::exception();
		}
	}
	~sem() {
		sem_destroy(&m_sem);
	}
	bool wait() {
		return sem_wait(&m_sem) == 0;
	}
	bool post() {
		return sem_post(&m_sem) == 0;
	}
};

//mutex
class locker
{
private:
	pthread_mutex_t m_mutex;
public:
	locker() {
		if(pthread_mutex_init(&m_mutex, NULL) != 0) {
			throw std::exception();
		}
	}
	~locker() {
		pthread_mutex_destroy(&m_mutex);
	}
	bool lock() {
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	bool unlock() {
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	pthread_mutex_t* get() {
		return &m_mutex;
	}
};

//cond
class cond
{
private:
	pthread_cond_t m_cond;
	//pthread_mutex_t m_mutex;
public:
	cond() {
		//if(pthread_mutex_init(&m_mutex, NULL) != 0) {
		//	throw std::exception();
		//}
		if(pthread_cond_init(&m_cond, NULL) != 0) {
			throw std::exception();
		}
	}
	~cond() {
		//pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cond);
	}
	bool wait(pthread_mutex_t* mutex) {
		int ret = 0;
		//pthread_mutex_lock(&m_mutex);
		ret = pthread_cond_wait(&m_cond, mutex);
		//pthread_mutex_unlock(&m_mutex);
		return ret == 0;
	}
	bool signal() {
		return pthread_cond_signal(&m_cond) == 0;
	}
	bool timewait(struct timespec t, pthread_mutex_t* mutex) {
		int ret = 0;
		//pthread_mutex_lock(&m_mutex);
		ret = pthread_cond_timedwait(&m_cond, mutex, &t);
		//pthread_mutex_unlock(&m_mutex);
		return ret == 0;
	}
	bool broadcast() {
		return pthread_cond_broadcast(&m_cond) == 0;
	}
};

#endif