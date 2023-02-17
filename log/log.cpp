#include "log.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
using namespace std;

Log::Log() {
	m_line_count = 0;
	m_is_async = false;
}
Log::~Log() {
	if(m_fp != NULL)
		fclose(m_fp);
	if(!m_log_queue)
		delete m_log_queue;
}

bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
	if(max_queue_size > 0) {
		m_is_async = true;
		m_log_queue = new block_queue<string>(max_queue_size);
		if(!m_log_queue)
			throw std::exception();
		pthread_t tid;
		if(pthread_create(&tid, NULL, flush_log_thread, NULL) != 0) {
			delete m_log_queue;
			throw std::exception();
		}
		if(pthread_detach(tid) != 0) {
			delete m_log_queue;
			throw std::exception();
		}
	}

	m_close_log = close_log;
	m_log_buf_size = log_buf_size;
	m_split_lines = split_lines;
	m_buf = new char[m_log_buf_size];
	memset(m_buf, 0, m_log_buf_size);

	const char* p = strrchr(file_name, '/');
	char log_full_name[256] = {0};
	time_t t = time(NULL);
	struct tm *systm = localtime(&t);
	struct tm mytm = *systm;
	m_today = mytm.tm_mday;
	if(p != NULL) {
		strcpy(log_name, p+1);
		strncpy(dir_name, file_name, p - file_name + 1);
		snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", 
			dir_name, mytm.tm_year+1900, mytm.tm_mon+1, mytm.tm_mday, log_name);
	}
	else {
		snprintf(log_full_name, 255, "%d_%02d_%02d_%s", 
			mytm.tm_year+1900, mytm.tm_mon+1, mytm.tm_mday, file_name);
	}

	m_fp = fopen(log_full_name, "a");
	if(m_fp == NULL){
		return false;
	}

	return true;
}

void Log::write_log(int level, const char* format, ...) {
	char tap[16] = {0};
	switch(level) {
		case 0:
		{
			strcpy(tap, "[debug]: ");
			break;
		}
		case 1:
		{
			strcpy(tap, "[info]: ");
			break;
		}
		case 2:
		{
			strcpy(tap, "[warn]: ");
			break;
		}
		case 3:
		{
			strcpy(tap, "[error]: ");
			break;
		}
		default:
		{
			strcpy(tap, "[debug]: ");
			break;
		}
	}

	time_t t = time(NULL);
	struct tm *systm = localtime(&t);
	struct tm mytm = *systm;

	m_mutex.lock();

	m_line_count++;
	if(m_today != mytm.tm_mday || m_line_count % m_split_lines == 0) {
		char new_log[256] = {0};
		fflush(m_fp);
		fclose(m_fp);

		char tail[16] = {0};
		snprintf(tail, 16, "%d_%02d_%02d_", mytm.tm_year+1900, mytm.tm_mon+1, mytm.tm_mday);
		if(m_today != mytm.tm_mday) {
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
			m_today = mytm.tm_mday;
			m_line_count = 0;
		}
		else {
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_line_count/m_split_lines);
		}
		m_fp = fopen(new_log, "a");
	}

	m_mutex.unlock();

	va_list valst;
	va_start(valst, format);
	
	struct timeval now = {0,0};
	gettimeofday(&now, NULL);

	m_mutex.lock();
	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     mytm.tm_year + 1900, mytm.tm_mon + 1, mytm.tm_mday,
                     mytm.tm_hour, mytm.tm_min, mytm.tm_sec, now.tv_usec, tap);
	int m = vsnprintf(m_buf+n, m_log_buf_size-n-1, format, valst);
	m_buf[m+n] = '\n';
	m_buf[m+n+1] = '\0';
	string log_str = m_buf;
	m_mutex.unlock();

	if(m_is_async && !m_log_queue->full()) {
		m_log_queue->push(log_str);
	}
	else {
		m_mutex.lock();
		fputs(log_str.c_str(), m_fp);
		m_mutex.unlock();
	}

	va_end(valst);
}

void Log::flush() {
	m_mutex.lock();
	fflush(m_fp);
	m_mutex.unlock();
}