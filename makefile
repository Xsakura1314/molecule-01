server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h 
	g++ -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h -lpthread -lmysqlclient

debug: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h 
	g++ -g -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h -lpthread -lmysqlclient

clean:
	rm  -r server
