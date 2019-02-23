OBJS	+= mkjbf.o

CFLAGS	+= -O3
CFLAGS	+= -Wall
CFLAGS	+= -fopenmp
CFLAGS	+= $(shell pkg-config --cflags MagickWand)

LIBS	+= -lm
LIBS	+= -lgomp
LIBS	+= $(shell pkg-config --libs MagickWand)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

mkjbf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

clean:
	$(RM) $(OBJS) mkjbf.exe mkjbf
