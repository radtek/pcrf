app_rx.fdx: app_rx.o app_rx_dict_cache.o app_rx_aa.o app_rx_data_conv.o
	gcc -shared app_rx.o app_rx_dict_cache.o app_rx_aa.o app_rx_data_conv.o -o app_rx.fdx
app_rx.o: app_rx.c
	gcc -fPIC -c -Wall -Wextra app_rx.c -lfdproto -lfdcore -o app_rx.o -I ..
app_rx_dict_cache.o: app_rx_dict_cache.c
	gcc -fPIC -c -Wall -Wextra app_rx_dict_cache.c -lfdproto -lfdcore -o app_rx_dict_cache.o -I ..
app_rx_aa.o: app_rx_aa.cpp
	g++ -fPIC -c -Wall -Wextra -I/u01/app/oracle/product/11.2.0/dbhome_1/rdbms/public -I/home/fd/project/utils/src app_rx_aa.cpp -lfdproto -lfdcore -o app_rx_aa.o -I ..
app_rx_data_conv.o: app_rx_data_conv.cpp
	g++ -fPIC -c -Wall -Wextra -I/u01/app/oracle/product/11.2.0/dbhome_1/rdbms/public -I/home/fd/project/utils/src app_rx_data_conv.cpp -lfdproto -lfdcore -o app_rx_data_conv.o -I ..
install:
	sudo cp app_rx.fdx /usr/local/lib/freeDiameter/
clean:
	rm -rf *.o *.fdx
