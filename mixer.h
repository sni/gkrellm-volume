#ifndef VOLUME_MIXER_H
#define VOLUME_MIXER_H
/* GKrellM Volume plugin
 |  Copyright (C) 1999-2000 Sjoerd Simons
 |
 |  Author:  Sjoerd Simons  sjoerd@luon.net
 |
 |  This program is free software which I release under the GNU General Public
 |  License. You may redistribute and/or modify this program under the terms
 |  of that license as published by the Free Software Foundation; either
 |  version 2 of the License, or (at your option) any later version.
 |
 |  This program is distributed in the hope that it will be useful,
 |  but WITHOUT ANY WARRANTY; without even the implied warranty of
 |  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 |  GNU General Public License for more details.
 |
 |  To get a copy of the GNU General Puplic License,  write to the
 |  Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <glib.h>

typedef struct _mixer_idz_t mixer_idz_t;
struct _mixer_idz_t {
    char *id;
      mixer_idz_t *next;
};

typedef struct _mixer_t mixer_t; 
typedef struct {
  mixer_idz_t *(*mixer_get_id_list)(void);
  mixer_t *(*mixer_open)(char *id);
  void (*mixer_close)(mixer_t *mixer);
  long (*mixer_device_get_fullscale)(mixer_t *mixer, int devid);
  void (*mixer_device_get_volume)(mixer_t *mixer, int devid, 
                                  int *left, int *right);
  void    (*mixer_device_set_volume)(mixer_t *mixer, int devid, 
                                     int left, int right);
} mixer_ops_t;

struct _mixer_t {
  /* mixer name */
  gchar *name; 
  /* nr of device in this mixer */
  int nrdevices;
  /* table with devid to names of the device mapping */
  gchar **dev_names;
  gchar **dev_realnames;

  mixer_ops_t *ops;
  void *priv;
}; 

void init_mixer(void);
/* tries to open a mixer device, returns NULL on error or otherwise an mixer_t
 * struct */
mixer_t *mixer_open(char *id);
void mixer_close(mixer_t *mixer);

/* Returns a pointer to the name of the mixer */
/* Shouldn't be freed */
char *mixer_get_name(mixer_t *mixer);
/* Returns the number of devices a mixer has */
int   mixer_get_nr_devices(mixer_t *mixer);

/* devid is the number of a device in a mixer */
/* 0 <= devid < mixer_get_nr_devices(mixer) */

/* get the real name of a device */
char *mixer_get_device_real_name(mixer_t *mixer,int devid);
/* get and set the user specified name of a device */
char *mixer_get_device_name(mixer_t *mixer,int devid);
void  mixer_set_device_name(mixer_t *mixer,int devid,char *name);

/* get the full scale of a device and get/set the volume */
long   mixer_get_device_fullscale(mixer_t *mixer,int devid);
void  mixer_get_device_volume(mixer_t *mixer, int devid,int *left,int *right);
void mixer_set_device_volume(mixer_t *mixer, int devid,int left,int right);

/* get an linked list of usable mixer devices */
mixer_idz_t *mixer_get_id_list();
mixer_idz_t *mixer_id_list_add(char *id,mixer_idz_t *list);
void mixer_free_idz(mixer_idz_t *idz);

#endif /* VOLUME_MIXER_H */
