dict_rx.fdx: dict_rx.o
	gcc -shared dict_rx.o -o dict_rx.fdx
dict_rx.o: dict_rx.c
	gcc -fPIC -c -Wall -Wextra dict_rx.c -lfdproto -lfdcore
install:
	sudo cp dict_rx.fdx /usr/local/lib/freeDiameter/
clean:
	rm -rf *.o *.fdx
