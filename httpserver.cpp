#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./httpconn/http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

using namespace std;

extern void setnonblocking(int fd);
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);

static timer_container timers;
static int epollfd;

void addsig(int sig, void (handler)(int), bool restart = true) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info) {
	printf("Error: %s\n", info);
	send(connfd, info, sizeof(info), 0);
	close(connfd);
}

void setup_sig(int epollfd, int sig_pipefd[2]) {
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	assert(ret != -1);

	setnonblocking(sig_pipefd[1]);
	addfd(epollfd, sig_pipefd[0]);
	addsig(SIGALRM, NULL);
	addsig(SIGTERM, NULL);
	addsig(SIGINT, NULL)
	addsig(SIGPIPE, SIG_IGN);
}

void timer_handler() {
	timers.tick();
	alarm(TIMESLOT);
}

void cb_func(int clientfd) {
	removefd(epollfd, clientfd);
}

int main(int argc, char const *argv[])
{
	if(argc != 3) {
		printf("Usage: %s <IP> <Port>\n", argv[0]);
		return 1;
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);

//create thread pool and array of connection objetcs
	threadpool<http_conn>* pool = NULL;
	try {
		pool = new threadpool<http_conn>;
	}
	catch(...) {
		return 1;
	}

	http_conn* users = new http_conn[MAX_FD];
	assert(users);
	int user_count = 0;

	sql_connection_pool* sql_pool = NULL;
	string uname = "root";
	string passwd = "1234";
	string dbname = "test";
	try {
		sql_pool = sql_connection_pool::get_instance();
		sql_pool->init("localhost",port+1,uname,passwd,dbname,8,1);
		users->init_sqlresult(sql_pool);
	}
	catch(...) {
		return 1;
	}

//
//socket program
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);
	struct linger tmp = {1,0};
	setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	
	struct sockaddr_in serv_addr, clnt_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ip);
	serv_addr.sin_port = htons(port);

	int ret = 0;
	ret = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	assert(ret != -1);
	ret = listen(listenfd, 5);
	assert(ret != -1);
//
//create epoll
	epollfd = epoll_create(5);
	assert(epollfd != -1);
	addfd(epollfd, listenfd, false);
	epoll_event events[MAX_EVENT_NUMBER];
	http_conn::m_epollfd = epollfd;
//
//setup signal
	int sig_pipefd[2];
	setup_sig(epollfd, sig_pipefd);
//event loop
	bool stop_server = false;
	while(!stop_server) {
		time_t tout;
		int number;
		if(timers.empty()) {
			number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		}
		else {
			tout = timers.top()->expire;
			number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, tout);
		}

		if(number < 0 && errno != EINTR) {
			puts("Epoll failure!");
			break;
		}
		for (int i = 0; i < number; ++i)
		{
			int sockfd = events[i].data.fd;

			if(sockfd == listenfd) {	//new connection
				struct sockaddr_in clnt_addr;
				socklen_t clnt_addr_size = sizeof(clnt_addr);
				int connfd = accept(listenfd, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
				if(connfd < 0) {
					printf("accept() Error: %d", errno);
					continue;
				}
				if(http_conn::m_user_count >= MAX_FD) {
					show_error(connfd, "Internet Server Busy...");
					continue;
				}
				time_t delay = time(NULL) + 3*TIMESLOT;
				util_timer* ut = new util_timer(delay, cb_func, connfd);
				timers.add_timer(ut);
				users[connfd].init(connfd, clnt_addr, ut);
			}
			else if(sockfd == sig_pipefd[0] && (events[i].events & EPOLLIN)) {
				int sig;
				char signals[1024];
				bool timeout = false;
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if(ret <= 0) {
					continue;
				}
				else {
					for (int i = 0; i < ret; ++i)
					{
						switch(signals[i]) {
							case SIGALRM : 
							{
								timeout = true;
								break;
							}
							case SIGTERM:
							case SIGINT:
							{
								stop_server = true;
								break;
							}
							default:
							{
								break;
							}
						}
					}
				}
			}
			else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				timers.del_timer(users[sockfd].m_timer);	//Error
				users[sockfd].close_conn();
			}
			else if(events[i].events & EPOLLIN) {	//read
				if(users[sockfd].read()) {
					timers.
					pool->append(&users[sockfd]);
				}
				else {
					timers.del_timer(users[sockfd].m_timer);
					users[sockfd].close_conn();
				}
			}
			else if(events[i].events & EPOLLOUT) {	//write
				if(users[sockfd].write()) {
					
				}
				else {
					timers.del_timer(users[sockfd].m_timer);
					users[sockfd].close_conn();
				}
			}
			else {}
		}
	}
//
	close(epollfd);
	close(listenfd);
	delete []users;
	delete pool;
	return 0;
}