CC           = gcc
CFLAGS       = -std=gnu99 -O2 -Wall -Wextra -isystem miniz
MINIZ_CFLAGS = -std=gnu99 -O2 -w

SRC   = src/main.c src/apks.c src/install.c src/adb_win.c src/adb_posix.c
OBJ   = $(SRC:.c=.o)
MINIZ_OBJ = miniz/miniz.o

ifeq ($(OS),Windows_NT)
    TARGET = apks_installer.exe
    LDLIBS = -lws2_32
else
    TARGET = apks_installer
    LDLIBS =
endif

$(TARGET): $(OBJ) $(MINIZ_OBJ)
	$(CC) $(OBJ) $(MINIZ_OBJ) -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

miniz/miniz.o: miniz/miniz.c
	$(CC) $(MINIZ_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(MINIZ_OBJ) $(TARGET)
