all: lotto_client lotto_server files

lotto_client: lotto_client.o lotto_utility.o
	gcc -Wall lotto_client.o lotto_utility.o -o lotto_client
	
lotto_client.o: costanti.h lotto.h lotto_client.c
	gcc -c -Wall lotto_client.c
	
lotto_server: lotto_server.o lotto_utility.o
	gcc -Wall lotto_server.o lotto_utility.o -o lotto_server

lotto_server.o: costanti.h lotto.h lotto_server.c
	gcc -c -Wall lotto_server.c

lotto_utility.o: costanti.h lotto.h lotto_utility.c
	gcc -c -Wall lotto_utility.c

files: files/utenti.txt files/client_bloccati.bin files/estrazioni.bin

files/:
	mkdir files

files/utenti.txt: files/
	touch files/utenti.txt

files/client_bloccati.bin: files/
	touch files/client_bloccati.bin

files/estrazioni.bin: files/
	touch files/estrazioni.bin

clean:
	rm *.o lotto_client lotto_server files/*
	rmdir files/
