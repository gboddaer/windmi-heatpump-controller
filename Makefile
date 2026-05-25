# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lpthread

# Mongoose library (single-file v7.x)
MONGOOSE_SRC = mongoose/mongoose.c
MONGOOSE_INC = mongoose

SRC_DIR = src
STATIC_DIR = static
TEST_DIR = tests

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/web_server.c \
          $(SRC_DIR)/modbus_client.c \
          $(SRC_DIR)/control_loop.c \
          $(SRC_DIR)/selftest.c \
          $(SRC_DIR)/crc16.c

OBJECTS = $(SOURCES:.c=.o)
MONGOOSE_OBJ = mongoose/mongoose.o
TARGET = windmi-control

.PHONY: all clean run setup mongoose test

all: $(TARGET)

$(TARGET): $(OBJECTS) $(MONGOOSE_OBJ)
	$(CC) $(OBJECTS) $(MONGOOSE_OBJ) $(LDFLAGS) -o $(TARGET)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(MONGOOSE_INC) -I$(SRC_DIR) -c $< -o $@

mongoose/mongoose.o: mongoose/mongoose.c
	$(CC) $(CFLAGS) -I$(MONGOOSE_INC) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(MONGOOSE_OBJ) $(TARGET)
	rm -f $(TEST_DIR)/*.o $(TEST_DIR)/test_crc16 $(TEST_DIR)/test_modbus_frames $(TEST_DIR)/test_control $(TEST_DIR)/test_spsc

run: $(TARGET)
	./$(TARGET)

# Download mongoose
mongoose:
	git clone https://github.com/cesanta/mongoose.git

setup: mongoose
	@echo "Setup complete. Run 'make' to build."

# --- Unit Tests ---
# Build modbus_client without static functions for frame testing
$(TEST_DIR)/modbus_client_test.o: $(SRC_DIR)/modbus_client.c
	$(CC) $(CFLAGS) -I$(MONGOOSE_INC) -I$(SRC_DIR) -DTEST_BUILD -c $< -o $@

test_crc16: $(SRC_DIR)/crc16.c $(TEST_DIR)/test_crc16.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(SRC_DIR)/crc16.c $(TEST_DIR)/test_crc16.c -o $(TEST_DIR)/test_crc16
	@echo "Running test_crc16..."
	@$(TEST_DIR)/test_crc16

test_modbus_frames: $(SRC_DIR)/crc16.c $(TEST_DIR)/modbus_client_test.o $(TEST_DIR)/test_modbus_frames.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(SRC_DIR)/crc16.c $(TEST_DIR)/modbus_client_test.o $(TEST_DIR)/test_modbus_frames.c -o $(TEST_DIR)/test_modbus_frames
	@echo "Running test_modbus_frames..."
	@$(TEST_DIR)/test_modbus_frames

test_control: $(SRC_DIR)/crc16.o $(TEST_DIR)/test_control_logic.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(TEST_DIR)/test_control_logic.c -o $(TEST_DIR)/test_control
	@echo "Running test_control..."
	@$(TEST_DIR)/test_control

test_spsc: $(TEST_DIR)/test_spsc_queue.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(TEST_DIR)/test_spsc_queue.c -o $(TEST_DIR)/test_spsc
	@echo "Running test_spsc..."
	@$(TEST_DIR)/test_spsc

test: test_crc16 test_control test_spsc
	@echo ""
	@echo "All tests complete."
