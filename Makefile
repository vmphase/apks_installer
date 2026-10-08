CC     = gcc
CFLAGS = -std=gnu99 -O2 -Wall -Wextra
SRC   = src/main.c src/apks.c src/install.c src/adb_win.c src/adb_posix.c
MINIZ = miniz/miniz.c

ifeq ($(OS),Windows_NT)
    TARGET = apks_installer.exe
    LDLIBS = -lws2_32
else
    TARGET = apks_installer
    LDLIBS =
endif

$(TARGET): $(SRC) $(MINIZ)
	$(CC) $(CFLAGS) $(SRC) $(MINIZ) -o $@ $(LDLIBS)
