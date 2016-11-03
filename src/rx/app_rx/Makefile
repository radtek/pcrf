app_rx.fdx: app_rx.o app_rx_dict_cache.o
	gcc -shared app_rx.o app_rx_dict_cache.o -o app_rx.fdx
app_rx.o: app_rx.c
	gcc -fPIC -c -Wall -Wextra app_rx.c -lfdproto -lfdcore -o app_rx.o
app_rx_dict_cache.o: app_rx_dict_cache.c
	gcc -fPIC -c -Wall -Wextra app_rx_dict_cache.c -lfdproto -lfdcore -o app_rx_dict_cache.o
install:
	sudo cp app_rx.fdx /usr/local/lib/freeDiameter/
clean:
	rm -rf *.o *.fdx
