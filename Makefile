all:
	gcc -Wall -ggdb -D_XOPEN_SOURCE -W -O -o webbench webbench.c
clean:
	rm -rf webbench