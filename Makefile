# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude

# Conditionally apply sanitizers if SAN=1 is passed
ifeq ($(SAN), 1)
    CFLAGS += -fsanitize=address,undefined -fno-sanitize-recover=undefined
endif
export CFLAGS # Export so bash scripts can inherit it

# --- SQLite Vendoring & Libraries ---
SQLITE_FLAGS := -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_DQS=0 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
                -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0

CFLAGS += -Ithird_party/sqlite
LDLIBS := -lcrypto -lpthread -lm
# ------------------------------------

# Default concurrency model (can be overridden via command line)
CONCURRENCY ?= fork

# Directories
SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/tourneyd
CTL_TARGET = $(BUILD_DIR)/tourneyctl

# --- Main Server Source Resolution ---
# Find all .c files in src/ and its subfolders dynamically
ALL_SRCS = $(shell find $(SRC_DIR) -name '*.c')

# Find all concurrency models
MODELS = $(shell find $(SRC_DIR)/concurrency -name '*_model.c')

# Remove all models from the main source list
BASE_SRCS = $(filter-out $(MODELS), $(ALL_SRCS))

# Add only the selected concurrency model back into the source list
SRCS = $(BASE_SRCS) $(SRC_DIR)/concurrency/$(CONCURRENCY)_model.c

# Convert src/path/to/file.c to build/path/to/file.o
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Add the compiled sqlite object to our server OBJS list
OBJS += $(BUILD_DIR)/obj/sqlite3.o

# --- CLI Tool Source Resolution ---
CTL_SRCS_SRC = src/db/db.c src/db/migrate.c src/core/config.c \
               src/util/log.c src/util/slug.c src/util/utf8.c src/util/hash.c \
               src/db/models/challenge_model.c src/db/models/writeup_model.c

# Map tools/ to build/tools/, but map the src/ files just like the main server to avoid recompiling
CTL_OBJS = $(BUILD_DIR)/tools/tourneyctl.o $(CTL_SRCS_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Default target runs when you just type 'make'
all: $(TARGET) $(CTL_TARGET)

# Alias to build just the CLI
ctl: $(CTL_TARGET)

# Standalone compilation rule for the massive SQLite file
$(BUILD_DIR)/obj/sqlite3.o: third_party/sqlite/sqlite3.c
	@mkdir -p $(dir $@)
	$(CC) $(SQLITE_FLAGS) -w -c $< -o $@

# Link all object files into the final server executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Link all object files into the final CLI executable
$(CTL_TARGET): $(CTL_OBJS) $(BUILD_DIR)/obj/sqlite3.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Compile tools files (e.g., tools/tourneyctl.c -> build/tools/tourneyctl.o)
$(BUILD_DIR)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile src files (e.g., src/main.c -> build/main.o)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Delete the build directory contents
clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean ctl

# For Fuzzer
FUZZ_FLAGS := -std=gnu11 -g -O1 -Iinclude -fsanitize=address,undefined -fno-sanitize-recover=undefined
FUZZ_SRC   := src/http/parser.c src/http/status.c

build/fuzz_driver: tests/fuzz/fuzz_driver.c tests/fuzz/fuzz_parser.c $(FUZZ_SRC)
	$(CC) $(FUZZ_FLAGS) $^ -o $@

fuzz: build/fuzz_driver
	ASAN_OPTIONS=abort_on_error=1 ./build/fuzz_driver tests/fuzz/corpus $(or $(ITER),2000000)

fuzz-clang:
	@mkdir -p build/corpus
	clang $(FUZZ_FLAGS) -fsanitize=fuzzer tests/fuzz/fuzz_parser.c $(FUZZ_SRC) -o build/fuzz_clang
	./build/fuzz_clang build/corpus tests/fuzz/corpus -dict=tests/fuzz/parser.dict \
	    -max_len=16384 -max_total_time=$(or $(SECS),600) -artifact_prefix=build/
.PHONY: fuzz fuzz-clang

# tests
test: all
	@echo "=== RUNNING UNIT TESTS ==="
	@bash scripts/run_unit_tests.sh
	@echo "\n=== RUNNING INTEGRATION TESTS ==="
	@bash tests/integration/run_all.sh
.PHONY: test