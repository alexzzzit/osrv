CC = gcc
LDFLAGS = -lpthread
TARGET = otp
SRC = otp.c
RUN_ARGS = -i input.txt -o output.txt -x 4212 -a 84589 -c 45989 -m 217728

build: $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) $(RUN_ARGS)

clean:
	rm -f $(TARGET)


