CC = gcc
CFLAGS = -Wall -std=c99 -O2
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

SRC = main.c
OUT = tower_defense

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT)
