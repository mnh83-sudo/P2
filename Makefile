CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lm

compare: compare.c
	$(CC) $(CFLAGS) -o compare compare.c $(LDFLAGS)

clean:
	rm -f compare

.PHONY: clean