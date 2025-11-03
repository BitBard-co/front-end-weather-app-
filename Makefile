CC      := gcc
CFLAGS  := -Wall -Wextra -O2
LDFLAGS := -lm
TARGET  := server
SRC     := src/server.c

.PHONY: all clean run run-bg stop demo

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Run the server in the foreground (Ctrl+C to stop)
run: $(TARGET)
	./$(TARGET)

# Run the server in background and write logs to /tmp/server.log
run-bg: $(TARGET)
	setsid ./$(TARGET) >/tmp/server.log 2>&1 </dev/null &
	@pgrep -a $(TARGET) || true
	@echo "Logs: tail -f /tmp/server.log"

# Stop background server if running
stop:
	pkill $(TARGET) || true

# Quick demo calls (starts server in background if not running)
demo:
	@pgrep $(TARGET) >/dev/null 2>&1 || (echo "Starting server..." && setsid ./$(TARGET) >/tmp/server.log 2>&1 </dev/null & sleep 0.2)
	@echo "City → Coordinates (Malmo)" && curl -sS 'http://127.0.0.1:8080/api/v1/geo?city=Malmo' || true
	@echo
	@echo "Coordinates → Weather (55.6050, 13.0038)" && curl -sS 'http://127.0.0.1:8080/api/v1/weather?lat=55.6050&lon=13.0038' || true

clean:
	rm -f $(TARGET)
