CC = gcc

CFLAGS = -std=c2x -D_XOPEN_SOURCE -Wpedantic -Wall -Wextra -Wconversion -Werror -fstack-protector-all -fpie -pie -O2 -D_FORTIFY_SOURCE=2 -MMD -Iinclude -D_POSIX_C_SOURCE=200809L

LDFLAGS = -pthread -lrt

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c, build/%.o, $(SRC))
DEP = $(OBJ:.o=.d)

TARGET_CLIENT = build/client
TARGET_SERVEUR = build/serveur

all: build $(TARGET_CLIENT) $(TARGET_SERVEUR)

build:
	mkdir -p build

$(TARGET_CLIENT): build/client.o build/write_read.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(TARGET_SERVEUR): build/serveur.o build/write_read.o
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

-include $(DEP)

.PHONY: all clean build

clean:
	rm -rf build
