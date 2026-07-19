CC := $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Werror

TARGET := aesdsocket
SOURCES := aesdsocket.c
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all default clean

all: $(TARGET)
default: all

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(TARGET) *.o
