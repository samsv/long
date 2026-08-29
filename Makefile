CC ?= cc
STD = -std=c17
WARN = -Wall -Wextra -Werror -Wswitch-enum -pedantic
SAN ?= -fsanitize=address,undefined
TARGET ?= long

RELEASE_FLAGS = $(STD) $(WARN) -O3
TEST_FLAGS = $(STD) $(WARN) -O0 -g -fno-omit-frame-pointer $(SAN)
DEBUG_FLAGS = $(STD) $(WARN) -O0 -g -fno-omit-frame-pointer $(SAN)
DEPFLAGS = -MMD -MP

LIB_SRC = $(wildcard src/*.c) $(wildcard src/obj/*.c) $(wildcard src/deps/*.c)
TEST_SRC = $(wildcard tests/*.c)

OBJ = $(LIB_SRC:src/%.c=build/release/%.o) build/release/main.o
TEST_OBJ = $(LIB_SRC:src/%.c=build/test/%.o) \
       $(patsubst tests/%.c,build/test/tests_%.o,$(TEST_SRC))
DEBUG_OBJ = $(LIB_SRC:src/%.c=build/debug/%.o) build/debug/main.o

BIN = build/$(TARGET)
TEST_BIN = build/$(TARGET)-test
DEBUG_BIN = build/$(TARGET)-debug

.PHONY: build run test debug clean

build: $(BIN)

run: $(BIN)
	./$(BIN) $(ARGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

debug: $(DEBUG_BIN)

clean:
	rm -rf build

$(BIN): $(OBJ)
	$(CC) $(RELEASE_FLAGS) $^ -o $@ -lm

$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(TEST_FLAGS) $^ -o $@ -lm

$(DEBUG_BIN): $(DEBUG_OBJ)
	$(CC) $(DEBUG_FLAGS) $^ -o $@ -lm

build/release/main.o: main.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_FLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

build/release/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_FLAGS) $(DEPFLAGS) -c $< -o $@

build/test/tests_%.o: tests/%.c
	@mkdir -p $(@D)
	$(CC) $(TEST_FLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

build/test/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(TEST_FLAGS) $(DEPFLAGS) -c $< -o $@

build/debug/main.o: main.c
	@mkdir -p $(@D)
	$(CC) $(DEBUG_FLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

build/debug/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(DEBUG_FLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d)
