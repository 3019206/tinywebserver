#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>

#include "sql_connection_pool.h"

pthread_mutex_t sql_connection_pool::m_mutex = PTHREAD_MUTEX_INITIALIZER;
sql_connection_pool::sql_connection_pool() {
	pthread_mutex_init(&m_mutex, NULL);
	m_curconn = 0;
	m_freeconn = 0;
}
sql_connection_pool::~sql_connection_pool() {
	destroy_pool();
}

sql_connection_pool* sql_connection_pool::get_instance() {
	//pthread_mutex_lock(&m_mutex);
	static sql_connection_pool m_instance;
	//pthread_mutex_unlock(&m_mutex);
	return &m_instance;
}

void sql_connection_pool::init(string ip, int port, string username, string password, string dbname, int maxconn, int close_log) {
	m_ip = ip;
	m_port = port;
	m_password = password;
	m_username = username;
	m_dbname = dbname;
	m_close_log = close_log;

	for (int i = 0; i < maxconn; ++i)
	{
		MYSQL* conn = NULL;
		conn = mysql_init(conn);
		if(conn == NULL) {
			printf("MySQL init Error: %s\n", mysql_error(conn));
			exit(1);
		}
		conn = mysql_real_connect(conn, "localhost", m_username.c_str(), m_password.c_str(), m_dbname.c_str(), m_port, NULL, 0 );
		if(conn == NULL) {
			printf("MySQL init Error: %s\n", mysql_error(conn));
			exit(1);
		}
		m_connlist.push_back(conn);
		++m_freeconn;
	}
	m_sem = sem(m_freeconn);
	m_maxconn = m_freeconn;
}

MYSQL* sql_connection_pool::get_conn() {
	if(m_connlist.size() == 0)
		return NULL;

	m_sem.wait();
	m_lock.lock();
	MYSQL* conn = m_connlist.front();
	m_connlist.pop_front();
	--m_freeconn;
	++m_curconn;
	m_lock.unlock();
	return conn;
}

bool sql_connection_pool::release_conn(MYSQL* conn) {
	if(conn == NULL)
		return false;

	m_lock.lock();
	m_connlist.push_back(conn);
	++m_freeconn;
	--m_curconn;
	m_lock.unlock();
	m_sem.post();
	return true;
}

int sql_connection_pool::get_free_conn() {
	return m_freeconn;
}

void sql_connection_pool::destroy_pool() {
	m_lock.lock();
	if(m_connlist.size() > 0) {
		list<MYSQL*>::iterator it;
		for (it = m_connlist.begin(); it != m_connlist.end(); ++it)
		{
			mysql_close(*it);
		}
	}
	m_lock.unlock();
}

connectionRAII::connectionRAII(MYSQL* &conn, sql_connection_pool* conn_pool) {
	conn = conn_pool->get_conn();
	connRAII = conn;
	poolRAII = conn_pool;
}
connectionRAII::~connectionRAII() {
	poolRAII->release_conn(connRAII);
}