CC = gcc
CFLAGS = -Iinclude

all: fss_manager fss_console worker

fss_manager: src/fss_manager.c src/Deque_Implementation.c
	$(CC) $(CFLAGS) -o $@ $^

fss_console: src/fss_console.c
	$(CC) $(CFLAGS) -o $@ $^

worker: src/worker.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f fss_manager fss_console worker fss_in fss_out

.PHONY: all clean
