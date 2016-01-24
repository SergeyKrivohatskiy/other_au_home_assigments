CC=g++
CFLAGS=-c -Wall --std=c++11
LDFLAGS=-lpthread
SOURCES=main.cpp aucont.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=aucont

all: $(SOURCES) $(EXECUTABLE)
    
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@
