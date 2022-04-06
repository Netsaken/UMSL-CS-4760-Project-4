CC = gcc
CFLAGS = -g -Wshadow -std=gnu99

all: oss child

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

oss: oss.o
	$(CC) $(CFLAGS) -o $@ $^

child: child.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	/bin/rm -f *.o