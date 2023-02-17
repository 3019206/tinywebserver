#ifndef SQLCONNECTIONPOLL_H
#define SQLCONNECTIONPOLL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>

#include "../lock/locker.h"
//#include "../log/log.h"

using namespace std;

class sql_connection_pool
{
private:
	sql_connection_pool();
	~sql_connection_pool();

public:
	static sql_connection_pool* get_instance();
	void init(string ip, int port, string username, string password, string dbname, int maxconn, int close_log);
	MYSQL* get_conn();
	bool release_conn(MYSQL* conn);
	int get_free_conn();
	void destroy_pool();

//members
private:
	//static pthread_mutex_t m_mutex;
	//static sql_connection_pool* m_instance;
	int m_maxconn;
	int m_curconn;
	int m_freeconn;
	locker m_lock;
	list<MYSQL*> m_connlist;
	sem m_sem;

public:
	static pthread_mutex_t m_mutex;
	string m_ip;
	int m_port;
	string m_username;
	string m_password;
	string m_dbname;
	int m_close_log;
};

class connectionRAII
{
public:
	connectionRAII(MYSQL* &conn, sql_connection_pool* conn_pool);
	~connectionRAII();
private:
	MYSQL* connRAII;
	sql_connection_pool* poolRAII;
};

#endif