keypace: main.c
	$(CC) main.c -o keypace -Wall -Wextra -Werror -O2 -std=c11

.PHONY: clean
clean:
	rm -f keypace
