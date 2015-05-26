CC=gcc
CFLAGS=-Wall
INCLUDES=-I/opt/vc/include/interface/vcos/pthreads \
	-I/opt/vc/include/interface/vmcs_host/linux \
	-I/opt/vc/include \
	-I/include/SDL
LDFLAGS=-lSDL -lbcm_host -lEGL -lGLESv2 -lpthread -lm -lpng \
	-L/usr/X11R6/lib \
	-L/opt/vc/lib
OBJS=cjson/cJSON.o threadqueue.o \
	phl_matrix.o phl_gles.o \
	config.o shader.o quad.o \
	common.o sprite.o threads.o pimenu.o
EXE=pim

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(INCLUDES)

$(EXE): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o $(EXE)
