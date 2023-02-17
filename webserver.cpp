#include "webserver.h"

WebServer::WebServer() {
	m_users = new http_conn[MAX_FD];
}
WebServer::~WebServer() {
	close(m_epollfd);
	close(m_listenfd);
	close(m_sig_pipefd[1]);
	close(m_sig_pipefd[0]);
	delete[] m_users;
	delete m_thread_pool;
}

void WebServer::init(int port,
					string uname, string passwd, string dbname,
					int thread_num, int sql_num,
					char* ip, 
					int close_log, int log_write, int opt_linger, int trig_mode, int actor_model)
{
	m_port = port;
	m_uname = uname;
	m_passwd = passwd;
	m_dbname = dbname;
	m_thread_num = thread_num;
	m_sql_num = sql_num;
	m_ip = ip;
	m_close_log = close_log;
	m_log_write = log_write;
	m_opt_linger = opt_linger;
	m_trig_mode = trig_mode;
	m_actor_model = actor_model;
}

void WebServer::set_mode() {
	if(m_trig_mode == 0) {
		m_listen_mode = 0;
		m_connect_mode = 0;
		printf("Mode: listen-LT, connect-LT, ");
	}
	else if(m_trig_mode == 1) {
		m_listen_mode = 0;
		m_connect_mode = 1;
		printf("Mode: listen-LT, connect-ET, ");
	}
	else if(m_trig_mode == 2) {
		m_listen_mode = 1;
		m_connect_mode = 0;
		printf("Mode: listen-ET, connect-LT, ");
	}
	else if(m_trig_mode == 3) {
		m_listen_mode = 1;
		m_connect_mode = 1;
		printf("Mode: listen-ET, connect-ET, ");
	}
	if(m_actor_model == 0)
		printf("Proactor.\n");
	else
		printf("Reactor.\n");
}

void WebServer::log_write() {
	if(m_close_log == 0) {
		if(m_log_write == 1)
			Log::get_instance()->init("./ServerLog/log", m_close_log, 2000, 800000, 800);
		else
			Log::get_instance()->init("./ServerLog/log", m_close_log, 2000, 800000, 0);
		LOG_INFO("%s", "***LOG Begin***");
	}
	//LOG_INFO("%s", "This line shows the log is ok.");
}


void WebServer::init_sql_pool() {
	m_sql_pool = sql_connection_pool::get_instance();
	m_sql_pool->init(m_ip, 3306, m_uname, m_passwd, m_dbname, m_sql_num, m_close_log);
	m_users->init_sqlresult(m_sql_pool);
}

void WebServer::init_thread_pool() {
	m_thread_pool = new threadpool<http_conn>(m_actor_model, m_sql_pool, m_thread_num);
}

void WebServer::init_clnt_conn(int connfd, struct sockaddr_in clnt_addr) {
	util_timer* timer = new util_timer;
	timer->userfd = connfd;
	timer->expire = time(NULL) + 3*TIMESLOT;
	timer->cb_func = cb_func;

	m_utils.timers.add_timer(timer);
	m_users[connfd].init(connfd, clnt_addr, timer, m_close_log, m_connect_mode);
}

void WebServer::event_listen() {
printf("Establishing listen socket...\n");
	m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(m_listenfd >= 0);


	if(m_opt_linger == 0) {
		struct linger tmp = {0,1};
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}
	else {
		struct linger tmp = {1,1};
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}
	int flag = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(m_ip);
	serv_addr.sin_port = htons(m_port);

	int ret = 0;
	ret = bind(m_listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	if(ret == -1) {
		printf("bind() Error!\n");
		exit(1);
	}
	ret = listen(m_listenfd, 5);
	if(ret == -1) {
		printf("listen() Error!\n");
		exit(1);
	}
printf("Successfully established!\n");

	m_utils.init(TIMESLOT);

	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	m_utils.addfd(m_epollfd, m_listenfd, false, m_listen_mode);

	m_utils.setup_sig(m_epollfd, m_sig_pipefd);
	alarm(TIMESLOT);

	http_conn::m_epollfd = m_epollfd;
	Utils::epollfd = m_epollfd;
	Utils::sig_pipefd = m_sig_pipefd;
}

void WebServer::event_loop() {
	bool timeout = false;
	bool stop_server = false;

	while(!stop_server) {
		int number = epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);
		if(number < 0 && errno != EINTR) {
			//printf("epoll_wait Error!\n");
			LOG_ERROR("epoll_wait() Error:errno %d", errno);
			break;
		}

		for (int i = 0; i < number; ++i)
		{
			int sockfd = m_events[i].data.fd;

			if(sockfd == m_listenfd) {
				if(!process_conn())
					continue;
			}
			else if(m_events[i].events & (EPOLLHUP)) {
				//printf("EPOLLHUP: %d\n", sockfd);
				LOG_ERROR("EPOLLHUP:fileno %d\n", sockfd);
				process_timer(m_users[sockfd].m_timer, sockfd);
			}
			else if(m_events[i].events & (EPOLLRDHUP)) {
				//printf("EPOLLRDHUP: %d\n", sockfd);
				LOG_ERROR("EPOLLRDHUP:fileno %d\n", sockfd);
				process_timer(m_users[sockfd].m_timer, sockfd);
			}
			else if(m_events[i].events & (EPOLLERR)) {
				//printf("EPOLLERR: %d\n", sockfd);
				LOG_ERROR("EPOLLERR:fileno %d\n", sockfd);
				process_timer(m_users[sockfd].m_timer, sockfd);
			}
			else if(sockfd == m_sig_pipefd[0] && (m_events[i].events & EPOLLIN)) {
				if(!process_signal(timeout, stop_server)){
					//printf("process_signal Error.\n");
					LOG_ERROR("%s", "process_signal() Error");
				}
			}
			else if(m_events[i].events & EPOLLIN) {
				process_read(sockfd);
			}
			else if(m_events[i].events & EPOLLOUT) {
				process_write(sockfd);
			}
		}

		if(timeout) {
			//printf("[Current client: %d] ", http_conn::m_user_count);
			m_utils.timer_handler();
			timeout = false;
		}
	}
}
bool WebServer::process_conn() {
	struct sockaddr_in clnt_addr;
	socklen_t clnt_addr_size = sizeof(clnt_addr);

	if(m_listen_mode == 0) {
		int connfd = accept(m_listenfd, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
		if(connfd < 0) {
			LOG_ERROR("accept() Error:errno %d", errno);
			return false;
		}
		if(http_conn::m_user_count >= MAX_FD) {
			m_utils.show_error(connfd, "Internal server busy");
			LOG_ERROR("%s", "Internal server busy");
			return false;
		}
		init_clnt_conn(connfd, clnt_addr);
	}
	else {
		while(1){
			int connfd = accept(m_listenfd, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
			if(connfd < 0) {
				LOG_ERROR("accept() Error:errno %d", errno);
				return false;
			}
			if(http_conn::m_user_count >= MAX_FD) {
				m_utils.show_error(connfd, "Internal server busy");
				LOG_ERROR("%s", "Internal server busy");
				return false;
			}
			init_clnt_conn(connfd, clnt_addr);
			//printf("Connect new client %d\n", connfd);
		}
	}
	return true;
}
void WebServer::process_timer(util_timer* timer, int sockfd) {
	if(!timer)
		return;
//printf("Start close connection of client %d\n", sockfd);
	timer->cb_func(sockfd);
	m_utils.timers.del_timer(timer);
//printf("Client %d closed.\n", sockfd);
}
bool WebServer::process_signal(bool& timeout, bool& stop_server) {
	int ret;
	int sig;
	char signals[1024];

	ret = recv(m_sig_pipefd[0], signals, sizeof(signals), 0);
	if(ret <= 0) {
		return false;
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
	return true;
}
void WebServer::process_read(int sockfd) {
	util_timer* timer = m_users[sockfd].m_timer;
	if(m_actor_model == 0) {	//proactor
		if(m_users[sockfd].read()) {
			m_thread_pool->append(&m_users[sockfd]);
			if(timer) {
				//printf("Refresh %d : read.\n", sockfd);
				LOG_INFO("Refresh %d : read.\n", sockfd)
				refresh_timer(timer);
			}
		}
		else {
			//printf("Connection done %d: read.\n", sockfd);
			LOG_INFO("Connection done %d: read.\n", sockfd);
			process_timer(timer, sockfd);
		}
	}
	else {	//reactor
		m_thread_pool->append(&m_users[sockfd], 0);
		while(1) {
			if(m_users[sockfd].m_flag == 1) {
				if(timer) {
					LOG_INFO("Refresh %d : read.\n", sockfd)
					refresh_timer(timer);
				}
				m_users[sockfd].m_flag = 0;
				break;
			}
			else if(m_users[sockfd].m_flag == 2) {
				LOG_INFO("Connection done %d: read.\n", sockfd);
				process_timer(timer, sockfd);
				m_users[sockfd].m_flag = 0;
				break;
			}
		}
	}
}
void WebServer::process_write(int sockfd) {
	util_timer* timer = m_users[sockfd].m_timer;
	if(m_actor_model == 0) {
		if(m_users[sockfd].write()) {
			if(timer) {
				//printf("Refresh %d : write.\n", sockfd);
				LOG_INFO("Refresh %d : write.\n", sockfd)
				refresh_timer(timer);
			}
		}
		else {
			//printf("Connection done %d: write.\n", sockfd);
			LOG_INFO("Connection done %d: write.\n", sockfd);
			process_timer(timer, sockfd);
		}
	}
	else {
		m_thread_pool->append(&m_users[sockfd], 1);
		while(1) {
			if(m_users[sockfd].m_flag == 1) {
				if(timer) {
					LOG_INFO("Refresh %d : read.\n", sockfd)
					refresh_timer(timer);
				}
				m_users[sockfd].m_flag = 0;
				break;
			}
			else if(m_users[sockfd].m_flag == 2) {
				LOG_INFO("Connection done %d: read.\n", sockfd);
				process_timer(timer, sockfd);
				m_users[sockfd].m_flag = 0;
				break;
			}
		}
	}
}
void WebServer::refresh_timer(util_timer* timer) {
	timer->expire = time(NULL) + 3*TIMESLOT;
	m_utils.timers.mod_timer(timer);
}