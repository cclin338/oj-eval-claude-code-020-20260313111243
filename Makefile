.PHONY: all
all:
	gcc -o code main.c buddy.c -std=c99

test:
	gcc -o test main.c buddy.c -std=c99