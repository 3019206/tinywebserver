#include "http_conn.h"

#define DEBUG true

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
	const char* error_400_form = "Your request had bad syntax.\n";
const char* error_403_title = "Forbidden";
	const char* error_403_form = "You do not hace permission.\n";
const char* error_404_title = "Not Found";
	const char* error_404_form = "The requested file was not found.\n";
const char* error_500_title = "Intrenal Error";
	const char* error_500_form = "There was an unusal problem.\n";

const char* doc_root = "./root";

map<string, string> users;
locker sql_lock;

int setnonblocking(int fd) {
	int oopt = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, oopt|O_NONBLOCK);
	return oopt;
}
void addfd(int epollfd, int fd, bool one_shot, int trig_mode) {
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLRDHUP;
	if(trig_mode == 1) {
		event.events |= EPOLLET;
	}
	if(one_shot) {
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

void modfd(int epollfd, int fd, int ev, int trig_mode) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
	if(trig_mode == 1) {
		event.events |= EPOLLET;
	}
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::init(int sockfd, const sockaddr_in& addr, util_timer* ut, int close_log, int trig_mode) {
	m_sockfd = sockfd;
	m_clnt_addr = addr;
	m_timer = ut;
	m_close_log = close_log;
	m_trig_mode = trig_mode;
	if(0) {
		int reuse = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	}
	addfd(m_epollfd, sockfd, true, m_trig_mode);
	++m_user_count;
	init();
}
void http_conn::init() {
	m_sql_conn = NULL;
	m_start_line = 0;
	m_checked_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = true;
	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_host = 0;
	m_content_length = 0;
	m_cgi = 0;
	
	memset(m_read_buf, 0, READ_BUF_SIZE);
	memset(m_write_buf, 0, WRITE_BUF_SIZE);
	memset(m_real_file, 0, FILENAME_LEN);
}

void http_conn::close_conn(bool real_close) {
	if(real_close == true && m_sockfd != -1) {
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		--m_user_count;
printf("[CHLD]Closed client %d\n", m_sockfd);
	}
}

//called by main thread
bool http_conn::read() { //ET
	if(m_read_idx >= READ_BUF_SIZE)
		return false;

	int cnt = 0;

	if(m_trig_mode == 0) {
		cnt = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
		if(cnt <= 0) {
			return false;
		}
		m_read_idx += cnt;
	}
	else {
		while(1) {
			cnt = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
			if(cnt == -1) {
				if(errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				return false;
			}
			else if(cnt == 0) {
				return false;
			}
			m_read_idx += cnt;
		}
	}
	return true;
}
bool http_conn::write() {
	int cnt = 0;
	int bytes_to_send = m_iv[0].iov_len + m_iv[1].iov_len;
	int bytes_have_sent = 0;
	if(bytes_to_send == 0) {
		modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
		init();
		return true;
	}
	/*
	while(1) {
		cnt = writev(m_sockfd, m_iv, m_iv_count);
		if(cnt <= -1) {
			if(errno == EAGAIN) {
				modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			unmap();
			return false;
		}

		bytes_to_send -= cnt;
		bytes_have_sent += cnt;
		if(bytes_to_send <= bytes_have_sent) {
			unmap();
			if(m_linger) {
				init();
				modfd(m_epollfd, m_sockfd, EPOLLIN);
				return true;
			}
			else {
				modfd(m_epollfd, m_sockfd, EPOLLIN);
				return false;
			}
		}
	}
	*/
	while(1) {
		cnt = writev(m_sockfd, m_iv, m_iv_count);
		
		if(cnt >= 0) {
			bytes_have_sent += cnt;
			bytes_to_send -= cnt;
			if(bytes_have_sent >= m_iv[0].iov_len) {
				m_iv[1].iov_base = m_file_addr + bytes_have_sent - m_write_idx;
				m_iv[1].iov_len = bytes_to_send;
				m_iv[0].iov_len = 0;
			}
			else {
				m_iv[0].iov_base = m_write_buf + bytes_have_sent;
				m_iv[0].iov_len = m_write_idx - bytes_have_sent;
			}
		}
		else {
			if(errno == EAGAIN) {
				modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
				return true;
			}
			unmap();
			return false;
		}

		if(bytes_to_send <= 0) {
			unmap();
			if(m_linger) {
				init();
				modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
				//printf("write ok, continue!\n");
				return true;
			}
			else {
				modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
				//printf("write ok, close!\n");
				return false;
			}
		}
	}
}
void http_conn::unmap() {
	if(m_file_addr) {
		munmap(m_file_addr, m_file_stat.st_size);
		m_file_addr = 0;
	}
}

//called by child threads
void http_conn::process() {
	HTTP_CODE read_ret = process_read();
	if(read_ret == NO_REQUEST) {
		modfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
		return;
	}
	bool write_ret = process_write(read_ret);
	if(!write_ret) {
		close_conn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
}

http_conn::HTTP_CODE http_conn::process_read() {	//master state machine
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char* linetext = 0;

	while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || 
		(line_status = parse_line()) == LINE_OK) {
		linetext = get_line();
		m_start_line = m_checked_idx;
	//printf("Got one line: %s\n", linetext);
		LOG_INFO("Get line: %s", linetext);
		switch(m_check_state) {
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_requestline(linetext);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers(linetext);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				else if(ret == GET_REQUEST)
					return do_request();
				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(linetext);
				if(ret == GET_REQUEST)
					return do_request();
				line_status = LINE_OPEN;	//so that can jump out while-loop
				//line_status == LINE_OPEN && parse_line() == LINE_OPEN
				break;
			}
			default:
			{
				return INTERNAL_ERROR;
			}
		}
	}
	return NO_REQUEST;
}
http_conn::LINE_STATUS http_conn::parse_line() {	//slaver state machine
	char tmpchr;
	for (; m_checked_idx < m_read_idx; ++m_checked_idx)
	{
		tmpchr = m_read_buf[m_checked_idx];
		if(tmpchr == '\r') {
			if(m_read_buf[m_checked_idx+1] == '\n'){
				m_read_buf[m_checked_idx] = '\0';
				m_read_buf[m_checked_idx+1] = '\0';
				m_checked_idx += 2;
				return LINE_OK;
			}
			else if(m_checked_idx+1 == m_read_idx)
				return LINE_OPEN;
			else
				return LINE_BAD;
		}
		else if(tmpchr == '\n') {
			if(m_read_buf[m_checked_idx-1] == '\r') {
				m_read_buf[m_checked_idx-1] = '\0';
				m_read_buf[m_checked_idx] = '\0';
				m_checked_idx += 1;
				return LINE_OK;
			}
			else
				return LINE_BAD;
		}
	}
	return LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::parse_requestline(char* linetext) { //get Method, URL, HTTPver
	const char* ws = " \t";

	m_url = strpbrk(linetext, ws);
	if(!m_url)	//no white space
		return BAD_REQUEST;
	*m_url++ = '\0';
	char* method = linetext;
	if(strcasecmp(method, "GET") == 0)
		m_method = GET;
	else if(strcasecmp(method, "POST") == 0) {
		m_method = POST;
		m_cgi = 1;
	}
	else
		return BAD_REQUEST;

	m_url += strspn(m_url, ws);	//strip white space before url
	m_version = strpbrk(m_url, ws);
	if(!m_version)
		return BAD_REQUEST;
	*m_version++ = '\0';		//get url

	m_version += strspn(m_version, ws);	//get version
	if(strcasecmp(m_version, "HTTP/1.1") != 0)
		return BAD_REQUEST;

	if(strncasecmp(m_url, "http://", 7) == 0) {	//strip url (http:// and hostname)
		m_url += 7;
		m_url = strchr(m_url, '/');
	}
	if(strncasecmp(m_url, "https://", 8) == 0) {
		m_url += 8;
		m_url = strchr(m_url, '/');
	}
	if(!m_url || m_url[0] != '/')
		return BAD_REQUEST;

	if(strlen(m_url) == 1) {	//default page
		strcat(m_url, "index.html");
	}
	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char* linetext) {
	if(linetext[0] == '\0') {
		if(m_content_length != 0) {
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		return GET_REQUEST;	//-> do_request()
	}
	else if(strncmp(linetext, "Connection:", 11) == 0) {
		linetext += 11;
		linetext += strspn(linetext, " \t");
		if(strcmp(linetext, "close") == 0) {
			m_linger = false;
		}
	}
	else if(strncmp(linetext, "Content-Length:", 15) == 0) {
		linetext += 15;
		linetext += strspn(linetext, " \t");
		m_content_length = atol(linetext);
	}
	else if(strncmp(linetext, "Host:", 5) == 0) {
		linetext += 5;
		linetext += strspn(linetext, " \t");
		m_host = linetext;
	}
	else {
		//printf("Server can't parse header: %s\n", linetext);
		LOG_INFO("Unknown header: %s", linetext);
	}
	return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char* linetext) {
	if(m_read_idx >= m_checked_idx + m_content_length) {
		linetext[m_content_length] = '\0';
		m_content = linetext;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request() {	//find target file
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);

	const char* p = strrchr(m_url, '/');
	char id = *(p+1);

	if(m_cgi == 1 && (id == '2' || id == '3')) {
		/*
		char* tmp = (char*)malloc(sizeof(char)*200);
		strcpy(tmp, "/");
		strcat(tmp, m_url+2);
		strncpy(m_real_file + len, tmp, FILENAME_LEN-1-len);
		free(tmp);
		*/

		char* name;
		char* passwd;
		name = strpbrk(m_content, "=");
		name++;
		passwd = strpbrk(m_content, "&");
		*passwd++ = '\0';
		passwd = strpbrk(passwd, "=");
		passwd++;

		if(id == '2') {
			if(users.find(name) != users.end() && users[name] == passwd) {
				strcpy(m_url, "/welcome.html");
			}
			else {
				strcpy(m_url, "/logError.html");
			}
		}
		else if(id == '3') {
			if(users.find(name) != users.end()) {
				strcpy(m_url, "/registerError.html");
			}
			else {
				char* sql_insert = (char*)malloc(sizeof(char)*200);
				strcpy(sql_insert, "INSERT INTO users(username, password) VALUES(");
	            strcat(sql_insert, "'");
	            strcat(sql_insert, name);
	            strcat(sql_insert, "', '");
	            strcat(sql_insert, passwd);
	            strcat(sql_insert, "')");

	            sql_lock.lock();
	            if(mysql_query(m_sql_conn, sql_insert)) {
	            	printf("query error:%s\n",mysql_error(m_sql_conn));
	            	sql_lock.unlock();
	            	strcpy(m_url, "/registerError.html");
	            }
	            else {
	            	users.insert(pair<string, string>(name, passwd));
	            	sql_lock.unlock();
	            	strcpy(m_url, "/log.html");
	            }
			}
		}
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}

	if(id == '0') {
		strcpy(m_url, "/register.html");
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	else if(id == '1') {
		strcpy(m_url, "/log.html");
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	else if(id == '5') {
		strcpy(m_url, "/picture.html");
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	else if(id == '6') {
		strcpy(m_url, "/video.html");
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	else if(id == '7') {
		strcpy(m_url, "/fans.html");
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	else {
		strncpy(m_real_file + len, m_url, FILENAME_LEN-1-len);
	}
	
	if(stat(m_real_file, &m_file_stat) < 0) {
		return NO_RESOURCE;
	}
	if(!(m_file_stat.st_mode & S_IROTH)) {	//other user's read-autho
		return FORBIDDEN_REQUEST;
	}
	if(S_ISDIR(m_file_stat.st_mode)) {
		return BAD_REQUEST;
	}
	//find file and mmap it
	int fd = open(m_real_file, O_RDONLY);
	m_file_addr = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
//printf("Response file: %s\n", m_real_file);
LOG_INFO("Response file: %s\n", m_real_file);
	return FILE_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret) {
	switch(ret) {
		case INTERNAL_ERROR:
		{
			add_status_line(500, error_500_title);
			add_headers(strlen(error_500_form));
			if(!add_content(error_500_form))
				return false;
			break;
		}
		case BAD_REQUEST:
		{
			add_status_line(400, error_400_title);
			add_headers(strlen(error_400_form));
			if(!add_content(error_400_form))
				return false;
			break;
		}
		case NO_RESOURCE:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if(!add_content(error_404_form))
				return false;
			break;
		}
		case FORBIDDEN_REQUEST:
		{
			add_status_line(403, error_403_title);
			add_headers(strlen(error_403_form));
			if(!add_content(error_403_form))
				return false;
			break;
		}
		case FILE_REQUEST:
		{
			add_status_line(200, ok_200_title);
			if(m_file_stat.st_size != 0) {
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_addr;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				return true;
			}
			else {
				const char* tmp = "<html><body></body></html>";
				add_headers(strlen(tmp));
				if(!add_content(tmp))
					return false;
			}
		}
		default:
		{
			return false;
		}
	}	
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;

	return true;
}
bool http_conn::add_response(const char* format, ...) { //format output
	if(m_write_idx >= WRITE_BUF_SIZE)
		return false;

	va_list arg_list;
	va_start(arg_list, format);
	int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUF_SIZE - 1 - m_write_idx, format, arg_list);
	if(len >= WRITE_BUF_SIZE - 1 - m_write_idx){
		va_end(arg_list);
		return false;
	}
	m_write_idx += len;
	va_end(arg_list);
	return true;
}
bool http_conn::add_status_line(int status, const char* title) {
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
void http_conn::add_headers(int content_len) {
	add_content_length(content_len);
	add_linger();
	add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
	return add_response("Content-Length: %d\r\n", content_len);
}
bool http_conn::add_linger() {
	return add_response("Connection: %s\r\n", m_linger == true ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
	return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char* content) {
	return add_response("%s", content);
}

void http_conn::init_sqlresult(sql_connection_pool* pool) {
	MYSQL* conn = NULL;
	connectionRAII connRAII(conn, pool);

	if(mysql_query(conn, "SELECT username,password FROM users")) {
		printf("SELECT Error: %s\n", mysql_error(conn));
		LOG_ERROR("SELECT Error: %s\n", mysql_error(conn));
	}
	MYSQL_RES* result = mysql_store_result(conn);
	int num_fields = mysql_num_fields(result);
	MYSQL_ROW row;
	while(row = mysql_fetch_row(result)) {
		users[string(row[0])] = string(row[1]);
	}
}