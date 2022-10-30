server: main.cpp ./conf/config.cpp ./core/lock/locker.h ./core/threadpool/threadpool.h ./core/timer/lst_timer.cpp ./http/http_conn.cpp ./os/unix/webserver.cpp
	g++ -o server $^ -lpthread -lmysqlclient

debug: main.cpp ./conf/config.cpp ./core/lock/locker.h ./core/threadpool/threadpool.h ./core/timer/lst_timer.cpp ./http/http_conn.cpp ./os/unix/webserver.cpp
	g++ -g -o server $^ -lpthread -lmysqlclient

clean:
	rm  -r server
