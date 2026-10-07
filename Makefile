# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude

# Directories
SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/tourneyd

# Find all .c files in src/ and its subfolders dynamically
SRCS = $(shell find $(SRC_DIR) -name '*.c')

# Convert src/path/to/file.c to build/path/to/file.o
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Default target runs when you just type 'make'
all: $(TARGET)

# Link all object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile each .c file into a .o file
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Delete the build directory contents
clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean