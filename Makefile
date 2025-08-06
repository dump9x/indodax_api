all:
	gcc -o indodax_api main.c -lcurl -lssl -lcrypto -ljansson

clean:
	rm -f indodax_api

