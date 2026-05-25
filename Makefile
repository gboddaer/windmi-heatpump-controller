# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread

# Mongoose library
MONGOOSE_LIB = mongoose/libmongoose.a
MONGOOSE_INC = mongoose

SRC_DIR = src
STATIC_DIR = static

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/web_server.c \
          $(SRC_DIR)/modbus_client.c \
          $(SRC_DIR)/control_loop.c \
          $(SRC_DIR)/selftest.c \
          $(SRC_DIR)/crc16.c

OBJECTS = $(SOURCES:.c=.o)
TARGET = windmi-control

.PHONY: all clean run setup mongoose

all: $(TARGET)

$(TARGET): $(OBJECTS) $(MONGOOSE_LIB)
	$(CC) $(OBJECTS) -L$(dir $(MONGOOSE_LIB)) -lmongoose $(LDFLAGS) -o $(TARGET)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(MONGOOSE_INC) -I$(SRC_DIR) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Download mongoose
mongoose:
	git clone https://github.com/cesanta/mongoose.git

setup: mongoose
	@echo "Setup complete. Run 'make' to build."
