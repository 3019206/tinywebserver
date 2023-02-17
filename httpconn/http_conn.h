#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include <stdarg.h>
#include <sys/mman.h>
#include <mysql/mysql.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/timer.h"
#include "../log/log.h"

class http_conn
{
public:
	http_conn() {}
	~http_conn() {}
public:
	static const int FILENAME_LEN = 200;
	static const int READ_BUF_SIZE = 2048;
	static const int WRITE_BUF_SIZE = 1024;
	enum  METHOD
	{
		GET = 0, POST, HEAD
	};
	enum HTTP_CODE
	{
		NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR
	};
	enum CHECK_STATE
	{
		CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
	};
	enum LINE_STATUS
	{
		LINE_OK = 0, LINE_OPEN, LINE_BAD
	};
//methods
public:
	void init(int sockfd, const sockaddr_in& addr, util_timer* ut, int close_log, int trig_mode);		//initialize a new connnection
	void close_conn(bool real_close = true);	//close a connection
	void process();		//process request, called by work threads
	bool read();
	bool write();
	void init_sqlresult(sql_connection_pool* pool);
private:
	void init();
	HTTP_CODE process_read();
	bool process_write(HTTP_CODE ret);

	//called by process_read() to parse request
	HTTP_CODE parse_requestline(char* text);
	HTTP_CODE parse_headers(char* text);
	HTTP_CODE parse_content(char* text);
	HTTP_CODE do_request();
	LINE_STATUS parse_line();
	char* get_line() {return m_read_buf + m_start_line;}

	//called by process_write() to write http response
	bool add_response(const char* format, ...);
	bool add_content(const char* content);
	bool add_status_line(int status, const char* title);
	void add_headers(int content_len);
	bool add_content_length(int content_len);
	bool add_linger();
	bool add_blank_line();
	void unmap();
//members
public:
	int m_state;	//For reactor: read(0) or write(1)
	int m_flag;		//For reactor: not set(0), refresh(1) or process(2) timer
	static int m_epollfd;
	static int m_user_count;
	util_timer* m_timer;
	MYSQL* m_sql_conn;
	int m_sockfd;
private:
	//connection socket and its client address
	sockaddr_in m_clnt_addr;

	//read buffer
	char m_read_buf[READ_BUF_SIZE];
	int m_start_line;
	int m_checked_idx;
	int m_read_idx;
	//write buffer
	char m_write_buf[WRITE_BUF_SIZE];
	int m_write_idx;

	CHECK_STATE m_check_state;	//state of main SM

	//http request component
	METHOD m_method;
	char m_real_file[FILENAME_LEN];
	char* m_url;
	char* m_version;
	char* m_host;
	char* m_content;
	int m_content_length;
	bool m_linger;				//whether keep connection

	//mmap requested file to this address
	char* m_file_addr;
	struct stat m_file_stat;

	//writev
	struct iovec m_iv[2];
	int m_iv_count;

	int m_cgi;
	int m_close_log;
	int m_trig_mode;
};

#endif