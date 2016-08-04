CC = gcc
CFLAGS ?= -O2 -Wall

teensy_loader_cli: teensy_loader_cli.c
	$(CC) $(CFLAGS) -s -o teensy_loader_cli teensy_loader_cli.c -lusb

clean:
	rm -f teensy_loader_cli teensy_loader_cli.exe
