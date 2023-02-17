CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: config.cpp main.cpp webserver.cpp ./httpconn/http_conn.cpp ./CGImysql/sql_connection_pool.cpp ./timer/timer.cpp ./log/log.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
