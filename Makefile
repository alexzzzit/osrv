TARGET = otp
SRC = otp.c
RUN_ARGS = -i input.txt -o output.txt -x 4212 -a 84589 -c 45989 -m 217728

build: $(SRC)
	@gcc $(SRC) -o $(TARGET) -lpthread

run: $(TARGET)
	@./$(TARGET) $(RUN_ARGS)

test: $(TARGET)
	@chmod +x test.sh
	@./test.sh

clean:
	@rm -f $(TARGET)
