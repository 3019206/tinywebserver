#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <typename T>
class block_queue
{
public:
	block_queue(int max_size = 1000);
	~block_queue();
	
	void clear();
	bool full();
	bool empty();
	bool front(T& value);
	bool back(T& value);
	int size();
	int max_size();
	bool push(const T& value);
	bool pop(T& value);

private:
	locker m_mutex;
	cond m_cond;
	T* m_array;
	int m_size;
	int m_max_size;
	int m_front;
	int m_back;
};
template <typename T>
block_queue<T>::block_queue(int max_size) {
	if(max_size <= 0)
		exit(-1);

	m_max_size = max_size;
	m_size = 0;
	m_front = -1;
	m_back = -1;
	m_array = new T[max_size];
}
template <typename T>
block_queue<T>::~block_queue() {
	if(m_array != NULL)
		delete[] m_array;
}
template <typename T>
void block_queue<T>::clear() {
	m_mutex.lock();

	m_size = 0;
	m_front = -1;
	m_back = -1;

	m_mutex.unlock();
}
template <typename T>
bool block_queue<T>::full() {
	m_mutex.lock();
	if(m_size >= m_max_size) {
		m_mutex.unlock();
		return true;
	}
	m_mutex.unlock();
	return false;
}
template <typename T>
bool block_queue<T>::empty() {
	m_mutex.lock();
	if(m_size == 0) {
		m_mutex.unlock();
		return true;
	}
	m_mutex.unlock();
	return false;
}
template <typename T>
bool block_queue<T>::front(T& value) {
	m_mutex.lock();
	if(m_size == 0) {
		m_mutex.unlock();
		return false;
	}
	value = m_array[(m_front+1)%m_max_size];
	m_mutex.unlock();
	return true;
}
template <typename T>
bool block_queue<T>::back(T& value) {
	m_mutex.lock();
	if(m_size == 0) {
		m_mutex.unlock();
		return false;
	}
	value = m_array[m_back];
	m_mutex.unlock();
	return true;
}
template <typename T>
int block_queue<T>::size() {
	int ret;
	m_mutex.lock();
	ret = m_size;
	m_mutex.unlock();
	return ret;
}
template <typename T>
int block_queue<T>::max_size() {
	int ret;
	m_mutex.lock();
	ret = m_max_size;
	m_mutex.unlock();
	return ret;
}
template <typename T>
bool block_queue<T>::push(const T& value) {
	m_mutex.lock();
	if(m_size >= m_max_size) {
		m_cond.broadcast();
		m_mutex.unlock();
		return false;
	}
	m_back = (m_back+1)%m_max_size;
	m_array[m_back] = value;
	++m_size;
	m_cond.broadcast();
	m_mutex.unlock();
	return true;
}
template <typename T>
bool block_queue<T>::pop(T& value) {
	m_mutex.lock();
	while(m_size <= 0) {
		if(!m_cond.wait(m_mutex.get())) {
			m_mutex.unlock();
			return false;
		}
	}
	m_front = (m_front+1)% m_max_size;
	value = m_array[m_front];
	--m_size;
	m_mutex.unlock();
	return true;
}

#endif