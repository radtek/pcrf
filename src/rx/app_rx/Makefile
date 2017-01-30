app_rx.fdx: app_rx.o app_rx_dict_cache.o app_rx_aa.o app_rx_data_conv.o
	gcc -shared app_rx.o app_rx_dict_cache.o app_rx_aa.o app_rx_data_conv.o -o app_rx.fdx
app_rx.o: app_rx.c
	gcc -fPIC -c -Wall -Wextra -g3 app_rx.c -lfdproto -lfdcore -o app_rx.o -I ..
app_rx_dict_cache.o: app_rx_dict_cache.c
	gcc -fPIC -c -Wall -Wextra -g3 app_rx_dict_cache.c -lfdproto -lfdcore -o app_rx_dict_cache.o -I ..
app_rx_aa.o: app_rx_aa.cpp
	g++ -fPIC -c -Wall -Wextra -g3 -I /oracle/orahome/rdbms/public -I ~/project/utils/src app_rx_aa.cpp -lfdproto -lfdcore -o app_rx_aa.o -I ..
app_rx_data_conv.o: app_rx_data_conv.cpp
	g++ -fPIC -c -Wall -Wextra -g3 -I /oracle/orahome/rdbms/public -I ~/project/utils/src app_rx_data_conv.cpp -lfdproto -lfdcore -o app_rx_data_conv.o -I ..
install:
	sudo cp app_rx.fdx /usr/local/lib/freeDiameter/
clean:
	rm -rf *.o *.fdx
