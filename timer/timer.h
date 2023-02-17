#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#define BUF_SIZE 64

using namespace std;

class util_timer;
struct client_data
{
	struct sockaddr_in addr;
	int sockfd;
	util_timer* timer;
};

class util_timer
{
public:
	util_timer() : prev(NULL), next(NULL) {}
	util_timer(time_t exp, void (*func)(int), int fd) :
				expire(exp), cb_func(func), userfd(fd), prev(NULL), next(NULL) {}
	~util_timer() {}
public:
	time_t expire;
	void (*cb_func)(int);
	int userfd;
	util_timer* prev;
	util_timer* next;
};

class timer_container
{
public:
	timer_container();
	~timer_container();

	void add_timer(util_timer* timer);
	void del_timer(util_timer* timer);
	void mod_timer(util_timer* timer);
	void tick();
	
private:
	void add_timer(util_timer* timer, util_timer* head);
	util_timer* dummy;
	util_timer* tail;
};

class Utils
{
public:
	Utils() {}
	~Utils() {}
	
	void init(int tslot);
	int setnonblocking(int fd);
	void addfd(int epollfd, int fd, bool one_shot, int trig_mode);
	//void removefd(int epollfd, int fd);
	void modfd(int epollfd, int fd, int ev, int trig_mode);
	void addsig(int sig, void (*handler)(int), bool restart = true);
	void show_error(int connfd, const char* info);
	void setup_sig(int epollfd, int sig_pipefd[2]);
	void timer_handler();
	static void sig_handler(int sig);

public:
	static int* sig_pipefd;
	static int epollfd;
	timer_container timers;
	int timeslot;
};

void cb_func(int fd);
/*
struct cmp
{
	bool operator ()(const util_timer* &a, const util_timer* &b) {
		return a->expire > b->expire;
	}
};

class timer_container
{
public:
	timer_container();
	~timer_container();

	void add_timer(util_timer* timer);
	void del_timer(util_timer* timer);
	void mod_timer(util_timer* timer);
	util_timer* top() const;
	void pop();
	void tick();
	bool empty() const;

private:
	priority_queue<util_timer*, vector<util_timer*>, cmp> m_heap; 
};
*/

#endif