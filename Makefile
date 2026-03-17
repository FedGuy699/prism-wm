CXX = g++
CXXFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11

TARGET = prismwm

SHARE = share/prismwm.desktop
SRC = src/main.cc

all: build

build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

install:
	cp $(TARGET) /usr/bin/
	cp $(SHARE) /usr/share/xsessions/

test: build
	Xephyr :1 -screen 800x600 & \
	PID=$$!; \
	sleep 1; \
	DISPLAY=:1 ./$(TARGET) & \
	WM_PID=$$!; \
	sleep 1; \
	wait $$PID; \
	kill $$WM_PID

clean:
	rm -f $(TARGET)
