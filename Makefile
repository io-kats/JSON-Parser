# https://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/
# https://www.cs.swarthmore.edu/~newhall/unixhelp/howto_makefiles.html

# Includes directory
IDIR = 

# Object file directory
ODIR = ./build

CC = g++
CFLAGS = -g -I$(IDIR) -Wall -Wpedantic

# Source files
SRCC = 
SRCCPP = example.cpp
SRC = $(SRCC) $(SRCCPP)

OBJ = $(SRC:%=$(ODIR)/%.o)

LIBS = 

TARGET = example.out

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	
$(ODIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@  
	
$(ODIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@  

	
.PHONY: clean

clean:
	rm -r $(ODIR)

