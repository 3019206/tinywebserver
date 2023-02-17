#include "timer.h"
#include "../httpconn/http_conn.h"

timer_container::timer_container() {
	dummy = new util_timer();
	tail = dummy;
}
timer_container::~timer_container() {
	util_timer* tmp = dummy;
	while(tmp) {
		dummy = tmp->next;
		delete tmp;
		tmp = dummy;
	}
}

void timer_container::add_timer(util_timer* timer, util_timer* start) {
	util_timer* prenode = start;
	util_timer* curnode = prenode->next;
	while(curnode) {
		if(timer->expire < curnode->expire) {
			prenode->next = timer;
			curnode->prev = timer;
			timer->next = curnode;
			timer->prev = prenode;
			return;
		}
		prenode = curnode;
		curnode = curnode->next;
	}
	if(!curnode) {
		prenode->next = timer;
		timer->prev = prenode;
		timer->next = NULL;
		tail = timer;
	}
}

void timer_container::add_timer(util_timer* timer) {
	if(!timer)
		return;
	add_timer(timer, dummy);
}

void timer_container::del_timer(util_timer* timer) {
	if(!timer)
		return;

	if(timer == tail) {
		tail->prev->next = NULL;
		tail = tail->prev;
		delete timer;
		return;
	}
	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	
	delete timer;
}

void timer_container::mod_timer(util_timer* timer) {
	if(!timer)
		return;
	if(timer == tail || timer->expire < timer->next->expire) {
		return;
	}

	util_timer *tmp = timer->next;
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    timer->prev = NULL;
    timer->next = NULL;
    add_timer(timer, tmp);
}

void timer_container::tick() {
	time_t cur = time(NULL);

	util_timer* tmp = dummy->next;
	while(tmp) {
		if(tmp->expire > cur)
			break;
//printf("Timeout: ");
		tmp->cb_func(tmp->userfd);
		del_timer(tmp);
		tmp = dummy->next;
	}
}

void Utils::init(int tslot) {
	timeslot = tslot;
}

int Utils::setnonblocking(int fd) {
	int oopt = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, oopt|O_NONBLOCK);
	return oopt;
}
void Utils::addfd(int epollfd, int fd, bool one_shot, int trig_mode) {
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
/*
void Utils::removefd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}
*/
void Utils::modfd(int epollfd, int fd, int ev, int trig_mode) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
	if(trig_mode == 1) {
		event.events |= EPOLLET;
	}
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void Utils::addsig(int sig, void (*handler)(int), bool restart) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}
void Utils::setup_sig(int epollfd, int* sig_pipefd) {
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	assert(ret != -1);

	setnonblocking(sig_pipefd[1]);
	addfd(epollfd, sig_pipefd[0], false, 0);

	addsig(SIGALRM, sig_handler);
	addsig(SIGTERM, sig_handler);
	addsig(SIGINT, sig_handler);
	addsig(SIGPIPE, SIG_IGN);
}
void Utils::show_error(int connfd, const char* info) {
	//printf("Error: %s\n", info);
	send(connfd, info, sizeof(info), 0);
	close(connfd);
}
void Utils::timer_handler() {
//printf("Tick...\n");
	timers.tick();
	alarm(timeslot);
}
void Utils::sig_handler(int sig) {
	int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

int* Utils::sig_pipefd = NULL;
int Utils::epollfd = -1;

void cb_func(int fd) {
	assert(fd >= 0);
	epoll_ctl(Utils::epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
	http_conn::m_user_count--;
//printf("Call back function is called to close %d\n", fd);
}