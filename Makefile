all: rps
rps: main.c
	gcc -std=c11 -pthread main.c -o rps