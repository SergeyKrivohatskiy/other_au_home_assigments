CC=g++
CFLAGS=-c -Wall --std=c++11
LDFLAGS=-lpthread
SOURCES=main.cpp aucont.cpp
OBJDIR=obj
OBJECTS=$(patsubst %.cpp, $(OBJDIR)/%.o, $(SOURCES)) 
EXECUTABLE=bin/aucont

all: $(SOURCES) $(EXECUTABLE)
    
$(OBJECTS): $(OBJDIR)

$(OBJDIR):
	mkdir -p $@

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)


$(OBJDIR)/%.o : %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJDIR)
	rm -f $(EXECUTABLE)
