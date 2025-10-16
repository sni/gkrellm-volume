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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glob.h>

#include <sys/param.h>
#if defined(__FreeBSD__) && __FreeBSD_version < 500000
  #include <machine/soundcard.h>
#else
  #include <sys/soundcard.h>
#endif

#include "mixer.h"
#include "oss_mixer.h"

#define OSSMIXER(x) ((oss_mixer_t *)x->priv)
static mixer_ops_t * get_mixer_ops(void);


/* tries to open a mixer device, returns NULL on error or otherwise an mixer_t
 * struct */
static mixer_t *
oss_mixer_open(char *id) {
  mixer_t *result;
  oss_mixer_t *ossresult;
  int fd,devices,nr,i;
#ifdef SOUND_MIXER_INFO
  mixer_info minfo;
#endif
  char *sound_labels[] = SOUND_DEVICE_LABELS;

  if ((fd = open(id,O_RDWR)) == -1) return NULL;
  if ( (ioctl(fd,SOUND_MIXER_READ_DEVMASK,&devices) < 0)) {
    close(fd);
    return NULL;
  }

#ifdef SOUND_MIXER_INFO
  if ((ioctl(fd,SOUND_MIXER_INFO,&minfo) < 0)) {
    close(fd);
    return NULL;
  }
#endif

  result = malloc(sizeof(mixer_t));
#ifdef SOUND_MIXER_INFO
  result->name = strdup(minfo.name);
#else
  result->name = strdup(id);
#endif

  /* get the nr of devices */
  nr = 0;
  for (i = 0 ; i < SOUND_MIXER_NRDEVICES; i++) { if (devices & (1<<i)) nr++; }

  result->nrdevices = nr;

  result->dev_realnames = malloc(nr * sizeof(char *));

  result->dev_names = malloc(nr * sizeof(char*));
  memset(result->dev_names,0,nr * sizeof(char *));

  ossresult = malloc(sizeof(oss_mixer_t));
  ossresult->fd = fd;
  ossresult->table = malloc(nr * sizeof(int));

  result->priv = ossresult;
  result->ops = get_mixer_ops();

  nr = 0;
  for (i = 0 ; i < SOUND_MIXER_NRDEVICES; i++)
    if (devices & (1<<i)) {
      ossresult->table[nr] = i;
      result->dev_realnames[nr] = strdup(sound_labels[i]);
      nr++;
    }
  return result;
}

static void 
oss_mixer_close(mixer_t *mixer) {
  int i;
  close(OSSMIXER(mixer)->fd);
  for (i=0;i < mixer->nrdevices ; i++) {
    free(mixer->dev_names[i]);
    free(mixer->dev_realnames[i]);
  }
  free(mixer->dev_names);
  free(mixer->dev_realnames);
  free(OSSMIXER(mixer)->table);
  free(mixer->priv);
  free(mixer);
}

/* get the full scale of a device and get/set the volume */
static long
oss_mixer_device_get_fullscale(mixer_t *mixer,int devid) {
  return 100;
}

static void 
oss_mixer_device_get_volume(mixer_t *mixer, int devid,int *left,int *right) {
  long amount;
  ioctl(OSSMIXER(mixer)->fd,MIXER_READ(OSSMIXER(mixer)->table[devid]),&amount);
  *left = amount & 0xff;
  *right = amount >> 8;
}

static void  
oss_mixer_device_set_volume(mixer_t *mixer, int devid,int left,int right) {
  long amount = (right << 8) + (left & 0xff);
  ioctl(OSSMIXER(mixer)->fd,MIXER_WRITE(OSSMIXER(mixer)->table[devid]),&amount);
}

static mixer_idz_t *
oss_mixer_get_id_list(void) {
  mixer_idz_t *result = NULL;
  char *device[] = { "/dev/mixer*","/dev/sound/mixer*"};
  glob_t pglob;
  int i,n;

  for (n = 0; n < (sizeof(device)/sizeof(char *)); n++) {
    if (glob(device[n],0,NULL,&pglob) == 0) {
      for (i = 0; i < pglob.gl_pathc; i++) { 
        char *rc,buffer[PATH_MAX];
        rc = realpath(pglob.gl_pathv[i],buffer);

        if (rc == NULL) continue;
        result = mixer_id_list_add(rc,result); 
      }
      globfree(&pglob);
    }
  }
  return result;
}

static mixer_ops_t oss_mixer_ops = {
  .mixer_get_id_list = oss_mixer_get_id_list,
  .mixer_open = oss_mixer_open,
  .mixer_close = oss_mixer_close,
  .mixer_device_get_fullscale = oss_mixer_device_get_fullscale,
  .mixer_device_get_volume = oss_mixer_device_get_volume,
  .mixer_device_set_volume = oss_mixer_device_set_volume
};

static mixer_ops_t *
get_mixer_ops(void) {
  return &oss_mixer_ops;
}

mixer_ops_t *
init_oss_mixer(void) {
  return get_mixer_ops();
}
