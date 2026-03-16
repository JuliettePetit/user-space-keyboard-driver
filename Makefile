EXE = driver
CFLAGS = -Wall -Wextra -std=c11 -O3
CPPFLAGS =
LDFLAGS =

all: driver

driver: driver.o
	$(CC) $(CFLAGS) -o $(EXE) $^ $(LDFLAGS)

driver.o: driver.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

clean:
	@rm -f $(EXE).o
	@rm -f $(EXE)
