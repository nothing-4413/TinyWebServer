CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -std=c++11
else
    CXXFLAGS += -O2 -std=c++11

endif

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -L/usr/lib64/mysql -lpthread -lmysqlclient

clean:
	rm -f server
