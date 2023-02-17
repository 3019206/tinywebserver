#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "./threadpool/threadpool.h"
#include "./httpconn/http_conn.h"
#include "./log/log.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer
{
public:
	WebServer();
	~WebServer();

	void init(	int port,
				string uname, string passwd, string dbname,
				int thread_num, int sql_num,
				char* ip, int close_log, int log_write, int opt_linger, int trig_mode, int actor_mode);
	void set_mode();
	void init_thread_pool();
	void init_sql_pool();
	void init_clnt_conn(int connfd, struct sockaddr_in clnt_addr);
	void event_listen();
	void event_loop();
	void log_write();
	bool process_conn();
	void process_timer(util_timer* timer, int sockfd);
	bool process_signal(bool& timeout, bool& stop_server);
	void process_read(int sockfd);
	void process_write(int sockfd);
	void refresh_timer(util_timer* timer);

public:
	
	//Socket
	char* m_ip;
	int m_port;
	int m_listenfd;

	//user http connection
	http_conn* m_users;

	//thread pool
	threadpool<http_conn>* m_thread_pool;
	int m_thread_num;

	//sql connection pool
	sql_connection_pool* m_sql_pool;
	string m_uname;
	string m_passwd;
	string m_dbname;
	int m_sql_num;

	//epoll
	int m_epollfd;
	epoll_event m_events[MAX_EVENT_NUMBER];

	//signal
	int m_sig_pipefd[2];

	//timer
	Utils m_utils;
	locker timers_lock;

	//config
	int m_close_log;	//open log
	int m_log_write;	//async log
	int m_trig_mode;	//LT or ET
		int m_listen_mode;
		int m_connect_mode;
	int m_opt_linger;
	int m_actor_model;
};

#endif