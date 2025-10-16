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

#include "mixer.h"

#ifdef WIN32
  #include "win32_mixer.h"
#else
  #ifdef ALSA
    #include "alsa_mixer.h"
  #endif
  #ifdef BLUETOOTH
    #include "bluetooth_mixer.h"
  #endif
  #include "oss_mixer.h"
#endif

#ifdef WIN32
mixer_ops_t *win32_mixer;
#else
mixer_ops_t *oss_mixer;
#endif

#ifdef ALSA
mixer_ops_t *alsa_mixer;
#endif

#ifdef BLUETOOTH
mixer_ops_t *bluetooth_mixer;
#endif


void init_mixer(void) {
#ifdef WIN32
  win32_mixer = init_win32_mixer();
#else
  oss_mixer = init_oss_mixer();
#endif

#ifdef ALSA
  alsa_mixer = init_alsa_mixer();
#endif

#ifdef BLUETOOTH
  bluetooth_mixer = init_bluetooth_mixer();
#endif
}
/* tries to open a mixer device, returns NULL on error or otherwise an mixer_t
 * struct */
mixer_t *mixer_open(char *id) {
  mixer_t *result = NULL;
#ifdef WIN32
  result = win32_mixer->mixer_open(id);
#else
  #ifdef BLUETOOTH
  /* Try Bluetooth first for BT devices */
  result = bluetooth_mixer->mixer_open(id);
  #endif
  /* Try ALSA if BT failed or not a BT device */
  #ifdef ALSA
  if (result == NULL)
    result = alsa_mixer->mixer_open(id);
  #endif
  /* either no alsa/bluetooth mixer or they failed */
  if (result == NULL) {
    result = oss_mixer->mixer_open(id);
  }
#endif
  return result;
}

void
mixer_close(mixer_t *mixer) {
  mixer->ops->mixer_close(mixer);
}

/* Returns a pointer to the name of the mixer */
/* Shouldn't be freed */
char *
mixer_get_name(mixer_t *mixer) {
  return mixer->name;
}

/* Returns the number of devices a mixer has */
int mixer_get_nr_devices(mixer_t *mixer) {
  return mixer->nrdevices;
}

/* devid is the number of a device in a mixer */
/* 0 <= devid < mixer_get_nr_devices(mixer) */

/* get the real name of a device */
char *
mixer_get_device_real_name(mixer_t *mixer,int devid) {
  return mixer->dev_realnames[devid];
}
/* get and set the user specified name of a device */
char *
mixer_get_device_name(mixer_t *mixer, int devid) {
  return mixer->dev_names[devid] != NULL ?
           mixer->dev_names[devid] : mixer->dev_realnames[devid];
}

void  mixer_set_device_name(mixer_t *mixer, int devid, char *name) {
  g_free(mixer->dev_names[devid]);
  mixer->dev_names[devid] = g_strdup(name);
}

/* get the full scale of a device and get/set the volume */
long mixer_get_device_fullscale(mixer_t *mixer, int devid) {
  return mixer->ops->mixer_device_get_fullscale(mixer, devid);
}
void
mixer_get_device_volume(mixer_t *mixer, int devid, int *left, int *right) {
  mixer->ops->mixer_device_get_volume(mixer, devid, left, right);
}

void
mixer_set_device_volume(mixer_t *mixer, int devid,int left,int right) {
  mixer->ops->mixer_device_set_volume(mixer, devid, left, right);
}

/* get an linked list of usable mixer devices */
mixer_idz_t *
mixer_get_id_list(void) {
  mixer_idz_t *result = NULL;
#ifdef WIN32
  result = win32_mixer->mixer_get_id_list();
#else
  #ifdef BLUETOOTH
  /* Get Bluetooth devices */
  result = bluetooth_mixer->mixer_get_id_list();
  #endif
  #ifdef ALSA
  /* Add ALSA devices */
  mixer_idz_t *alsa_list = alsa_mixer->mixer_get_id_list();
  if (alsa_list != NULL) {
    if (result == NULL) {
      result = alsa_list;
    } else {
      /* Append ALSA list to result */
      mixer_idz_t *tmp = result;
      while (tmp->next != NULL) tmp = tmp->next;
      tmp->next = alsa_list;
    }
  }
  #endif
  /* Add OSS devices */
  mixer_idz_t *oss_list = oss_mixer->mixer_get_id_list();
  if (oss_list != NULL) {
    if (result == NULL) {
      result = oss_list;
    } else {
      /* Append OSS list to result */
      mixer_idz_t *tmp = result;
      while (tmp->next != NULL) tmp = tmp->next;
      tmp->next = oss_list;
    }
  }
#endif
  return result;
}

/* adds an id to the mixer list */
mixer_idz_t *
mixer_id_list_add(char *id, mixer_idz_t *list) {
  mixer_idz_t *new = g_new(mixer_idz_t,1);
  mixer_idz_t *n;

  new->id = g_strdup(id);
  new->next = NULL;
  if (list == NULL)
    return new;

  for (n = list; n->next != NULL; n = n->next);
  n->next = new;
  return list;
}

void mixer_free_idz(mixer_idz_t *idz) {
  mixer_idz_t *tmp;
  mixer_idz_t *next = idz;

  while (next != NULL) {
    tmp = next; next = next->next;
    g_free(tmp->id);
    g_free(tmp);
  }
}
