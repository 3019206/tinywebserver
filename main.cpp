#include "config.h"

int main(int argc, char *argv[])
{
	string uname = "root";
	string passwd = "1234";
	string dbname = "test";
	char* ip = "192.168.2.196";
	char* ip1 = "127.0.0.1";

	Config config;
	config.parse_arg(argc, argv);

	WebServer server;
	server.init(config.PORT,
				uname, passwd, dbname,
				config.thread_num, config.sql_num, ip1,
				config.close_log, config.LOGWrite, config.OPT_LINGER, config.TRIGMode, config.actor_model);
	printf("Server init successfully...\n");

	server.set_mode();
	printf("Mode set successfully...\n");

	server.log_write();
	printf("Log prepared successfully...\n");

	server.init_sql_pool();
	printf("Sql pool init successfully...\n");
	
	server.init_thread_pool();
	printf("Thread pool init successfully...\n");
	
	server.event_listen();
	printf("Socket prepared successfully...\nStart loop...\n");
	
	server.event_loop();
	printf("Good bye~\n");
	return 0;
}