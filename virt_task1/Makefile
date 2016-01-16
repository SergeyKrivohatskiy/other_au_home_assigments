CC=g++
CFLAGS=-c -Wall --std=c++11
LDFLAGS=
SOURCES=main.cpp aucont.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=aucont

all: $(SOURCES) $(EXECUTABLE)
    
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@
