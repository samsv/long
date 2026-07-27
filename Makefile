CC ?= cc
STD = -std=c17
WARN = -Wall -Wextra -Werror -Wswitch-enum -pedantic
SAN ?= -fsanitize=address,undefined
TARGET ?= long

RELEASE_FLAGS = $(STD) $(WARN) -O3
TEST_FLAGS = $(STD) $(WARN) -O0 -g -fno-omit-frame-pointer $(SAN)
DEPFLAGS = -MMD -MP

LIB_SRC = $(wildcard src/*.c) $(wildcard src/obj/*.c)
TEST_SRC = $(wildcard tests/*.c)

OBJ = $(LIB_SRC:src/%.c=build/release/%.o) build/release/main.o
TEST_OBJ = $(LIB_SRC:src/%.c=build/test/%.o) \
	   $(patsubst tests/%.c,build/test/tests_%.o,$(TEST_SRC))

BIN = build/$(TARGET)
TEST_BIN = build/$(TARGET)-test

.PHONY: build run test clean

build: $(BIN)

run: $(BIN)
	./$(BIN) $(ARGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build

$(BIN): $(OBJ)
	$(CC) $(RELEASE_FLAGS) $^ -o $@ -lm

$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(TEST_FLAGS) $^ -o $@ -lm

build/release/main.o: main.c | build/release
	$(CC) $(RELEASE_FLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

build/release/%.o: src/%.c | build/release
	$(CC) $(RELEASE_FLAGS) $(DEPFLAGS) -c $< -o $@

build/test/tests_%.o: tests/%.c | build/test
	$(CC) $(TEST_FLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

build/test/%.o: src/%.c | build/test
	$(CC) $(TEST_FLAGS) $(DEPFLAGS) -c $< -o $@

build/release build/test:
	mkdir -p $@ $@/obj

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)
