#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "blockqueue.h"

class Log
{
public:
	static Log* get_instance() {
		static Log m_instance;
		return &m_instance;
	}
	static void* flush_log_thread(void* arg) {
		Log::get_instance()->async_write_log();
	}
	bool init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
	void write_log(int level, const char* format, ...);
	void flush();
private:
	Log();
	~Log();
	void* async_write_log() { //exec by thread
		string log;
		while(m_log_queue->pop(log)) {
			m_mutex.lock();
			fputs(log.c_str(), m_fp);
			m_mutex.unlock();
		}
	}

private:
	block_queue<string>* m_log_queue;
	locker m_mutex;
	FILE* m_fp;
	int m_close_log;
	bool m_is_async;
	char dir_name[128];
	char log_name[128];
	int m_split_lines;
	int m_log_buf_size;
	int m_today;
	long long m_line_count;
	char* m_buf;
};

#define LOG_DEBUG(format,...) if(m_close_log == 0) {Log::get_instance()->write_log(0,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format,...) if(m_close_log == 0) {Log::get_instance()->write_log(1,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format,...) if(m_close_log == 0) {Log::get_instance()->write_log(2,format,##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format,...) if(m_close_log == 0) {Log::get_instance()->write_log(3,format,##__VA_ARGS__); Log::get_instance()->flush();}

#endif

