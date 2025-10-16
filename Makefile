# Makefile for  GKrellM volume plugin

PACKAGE ?= gkrellm-volume
LOCALEDIR ?= /usr/local/share/locale

FLAGS += -DPACKAGE="\"$(PACKAGE)\"" 
export PACKAGE LOCALEDIR

GTK_CONFIG = pkg-config gtk+-2.0

PLUGIN_DIR ?= /usr/local/lib/gkrellm2/plugins
GKRELLM_INCLUDE = -I/usr/local/include

GTK_CFLAGS = `$(GTK_CONFIG) --cflags` 
GTK_LIB = `$(GTK_CONFIG) --libs`

FLAGS = -O2 -Wall -fPIC $(GTK_CFLAGS) $(GKRELLM_INCLUDE)
LIBS = $(GTK_LIB)
LFLAGS = -shared

OBJS = volume.o mixer.o oss_mixer.o

ifeq ($(enable_alsa),1)
  FLAGS += -DALSA
  LIBS += -lasound
  OBJS += alsa_mixer.o 
endif

ifeq ($(enable_nls),1)
    FLAGS += -DENABLE_NLS -DLOCALEDIR=\"$(LOCALEDIR)\"
    export enable_nls
endif

CC = gcc $(CFLAGS) $(FLAGS)

INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -s

all:	volume.so
	(cd po && ${MAKE} all )

volume.so: $(OBJS)
	$(CC) $(OBJS) -o volume.so $(LIBS) $(LFLAGS)

clean:
	rm -f *.o core *.so* *.bak *~
	(cd po && ${MAKE} clean)

install: 
	(cd po && ${MAKE} install)
	$(INSTALL_PROGRAM) volume.so $(PLUGIN_DIR)

%.c.o: %.c

