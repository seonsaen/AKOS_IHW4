all: rps rps_alt
rps: rps.c
	gcc -std=c11 -pthread rps.c -o rps

rps_alt: rps_alt.c
	gcc -std=c11 -pthread rps_alt.c -o rps_alt