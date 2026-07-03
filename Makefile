CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = -static -static-libgcc

.PHONY: release clean

release:
	mkdir -p release
	$(CC) $(CFLAGS) $(LDFLAGS) -mwindows -o release/mvp2005fix.exe src/launcher.c -lcomdlg32
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o release/mvp2005fix.dll src/runtime.c

clean:
	rm -f release/mvp2005fix.exe release/mvp2005fix.dll
