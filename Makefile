CFLAGS=-fobjc-arc -funroll-loops -Ofast -arch arm64 -mmacosx-version-min=11.0
FRAMEWORKS =-lIOReport -framework IOKit -framework CoreFoundation -framework SystemConfiguration


all:
	gcc utils.c $(CFLAGS) $(FRAMEWORKS) -g

clean:
	rm -f a.out
