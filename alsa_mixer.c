#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <glib.h>
#include <math.h>

#include "mixer.h"
#include "alsa_mixer.h"

#define ALSAMIXER(x) ((alsa_mixer_t *)x->priv)
static mixer_ops_t *get_mixer_ops(void);

enum {
  CTL_PLAYBACK = 0,
  CTL_CAPTURE,
  CTL_PLAYBACK_SWITCH
};

static void
error(const char *fmt, ...) {
  va_list va;

  va_start(va, fmt);
  fprintf(stderr, "gkrellm-volume amixer: ");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
}

static int
mixer_event(snd_mixer_t * m, unsigned int mask, snd_mixer_elem_t * elem) {
  mixer_t *mixer = (mixer_t *) snd_mixer_get_callback_private(m);

  ALSAMIXER(mixer)->changed_state = 1;
  return 0;
}

static mixer_t *
alsa_mixer_open(char *card) {
  mixer_t *result;
  alsa_mixer_t *alsaresult;
  int err;
  snd_mixer_t *handle;
  snd_mixer_selem_id_t *sid;
  snd_mixer_elem_t *elem;

  snd_ctl_card_info_t *hw_info;
  snd_ctl_t *ctl_handle;

  int count, i;

  snd_mixer_selem_id_alloca(&sid);
  snd_ctl_card_info_alloca(&hw_info);

  if ((err = snd_ctl_open(&ctl_handle, card, 0)) < 0) {
    error("Control info %s error: %s", card, snd_strerror(err));
//    snd_ctl_close(ctl_handle);
    return NULL;
  }

  if ((err = snd_ctl_card_info(ctl_handle, hw_info)) < 0) {
    error("Control info %s error: %s", card, snd_strerror(err));
    snd_ctl_close(ctl_handle);
    return NULL;
  }
  snd_ctl_close(ctl_handle);

  if ((err = snd_mixer_open(&handle, 0)) < 0) {
    error("Mixer %s open error: %s", card, snd_strerror(err));
    return NULL;
  }
  if ((err = snd_mixer_attach(handle, card)) < 0) {
    error("Mixer attach %s error: %s", card, snd_strerror(err));
    snd_mixer_close(handle);
    return NULL;
  }
  if ((err = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
    error("Mixer register error: %s", snd_strerror(err));
    snd_mixer_close(handle);
    return NULL;
  }
  err = snd_mixer_load(handle);
  if (err < 0) {
    error("Mixer %s load error: %s", card, snd_strerror(err));
    snd_mixer_close(handle);
    return NULL;
  }

  count = 0;

  for (elem = snd_mixer_first_elem(handle); elem;
       elem = snd_mixer_elem_next(elem)) {
    if (!snd_mixer_selem_is_active(elem))
      continue;
    if (snd_mixer_selem_has_playback_volume(elem)) {
      count++;
    }
    if (snd_mixer_selem_has_playback_switch(elem)) {
      count++;
    }
    if (snd_mixer_selem_has_capture_volume(elem)) {
      count++;
    }
  }

  result = (mixer_t *) malloc(sizeof(mixer_t));
  alsaresult = (alsa_mixer_t *)malloc(sizeof(mixer_t));

  result->priv = (void *)alsaresult;
  result->ops = get_mixer_ops();

  result->name = g_strdup_printf("%s", snd_ctl_card_info_get_name(hw_info));
  result->nrdevices = count;
  result->dev_names = (char **) malloc(sizeof(char *) * count);
  result->dev_realnames = (char **) malloc(sizeof(char *) * count);

  alsaresult->handle = handle;
  alsaresult->sids =
    (snd_mixer_selem_id_t **) malloc(sizeof(snd_mixer_selem_id_t *) * count);
  alsaresult->ctltype = (int *) malloc(sizeof(int) * count);


  for (elem = snd_mixer_first_elem(handle), i = 0; elem;
       elem = snd_mixer_elem_next(elem)) {
    snd_mixer_selem_get_id(elem, sid);
    if (!snd_mixer_selem_is_active(elem))
      continue;

    if (snd_mixer_selem_has_playback_volume(elem)) {
      result->dev_realnames[i] = strdup(snd_mixer_selem_id_get_name(sid));
      result->dev_names[i] =
        g_strdup_printf("%s %s", snd_mixer_selem_id_get_name(sid),
            snd_mixer_selem_has_capture_volume(elem) ? "playback" : "");
      alsaresult->ctltype[i] = CTL_PLAYBACK;
      snd_mixer_selem_id_malloc(&alsaresult->sids[i]);
      snd_mixer_selem_get_id(elem, alsaresult->sids[i]);
      i++;
    } 
    if (snd_mixer_selem_has_capture_volume(elem)) {
      result->dev_realnames[i] = strdup(snd_mixer_selem_id_get_name(sid));
      result->dev_names[i] =
        g_strdup_printf("%s %s", snd_mixer_selem_id_get_name(sid),
            snd_mixer_selem_has_playback_volume(elem) ? "capture" : "");
      alsaresult->ctltype[i] = CTL_CAPTURE;
      snd_mixer_selem_id_malloc(&alsaresult->sids[i]);
      snd_mixer_selem_get_id(elem, alsaresult->sids[i]);
      i++;
    } 
    if (snd_mixer_selem_has_playback_switch(elem)) {
      result->dev_realnames[i] = strdup(snd_mixer_selem_id_get_name(sid));
      result->dev_names[i] =
        g_strdup_printf("%s", snd_mixer_selem_id_get_name(sid));
      alsaresult->ctltype[i] = CTL_PLAYBACK_SWITCH;
      snd_mixer_selem_id_malloc(&alsaresult->sids[i]);
      snd_mixer_selem_get_id(elem, alsaresult->sids[i]);
      i++;
    }
  }

  snd_mixer_set_callback(handle, mixer_event);
  snd_mixer_set_callback_private(handle, result);

  return result;
}

static void
alsa_mixer_close(mixer_t * mixer) {
  int i;

  snd_mixer_close(ALSAMIXER(mixer)->handle);
  for (i = 0; i < mixer->nrdevices; i++) {
    free(mixer->dev_names[i]);
    free(mixer->dev_realnames[i]);
    snd_mixer_selem_id_free(ALSAMIXER(mixer)->sids[i]);
  }
  free(mixer->dev_names);
  free(mixer->dev_realnames);
  free(ALSAMIXER(mixer)->ctltype);
  free(ALSAMIXER(mixer)->sids);
  free(mixer->priv);
  free(mixer);
}

/* get the full scale of a device and get/set the volume */
static long
alsa_mixer_device_get_fullscale(mixer_t * mixer, int devid) {
  if (ALSAMIXER(mixer)->ctltype[devid] == CTL_PLAYBACK_SWITCH) {
    return 1;
  }
  return 100;
}

/* Fuction to convert from volume to percentage. val = volume */

static int
convert_prange(int val, int min, int max) {
  int range = max - min;
  int tmp;

  if (range == 0)
    return 0;
  val -= min;
  tmp = rint((double) val / (double) range * 100);
  return tmp;
}

/* Function to convert from percentage to volume. val = percentage */

static int
convert_prange1(int val, int min, int max) {
  int range = max - min;
  int tmp;

  if (range == 0)
    return 0;

  tmp = rint((double) range * ((double) val * .01)) + min;
  return tmp;
}

void
alsa_mixer_device_get_volume(mixer_t * mixer, int devid, 
                             int *left, int *right) {
  long min, max, lvol, rvol;
  int sw;
  int err;
  alsa_mixer_t *alsamixer = ALSAMIXER(mixer);
  snd_mixer_elem_t *elem;

  snd_mixer_handle_events(alsamixer->handle);

  if (alsamixer->changed_state) {
    snd_mixer_free(alsamixer->handle);

    err = snd_mixer_load(alsamixer->handle);
    if (err < 0) {
      error("Mixer load error: %s", snd_strerror(err));
      snd_mixer_close(alsamixer->handle);
      return;
    }
    alsamixer->changed_state = 0;
  }

  elem = snd_mixer_find_selem(alsamixer->handle, alsamixer->sids[devid]);

  switch (alsamixer->ctltype[devid]) {
    case CTL_PLAYBACK:
      snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
      snd_mixer_selem_get_playback_volume(elem, 0, &lvol);
      if (snd_mixer_selem_is_playback_mono(elem))  {
          rvol = lvol;
      } else {
        snd_mixer_selem_get_playback_volume(elem, 1, &rvol);
      }
      break;
    case CTL_CAPTURE:
      snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
      snd_mixer_selem_get_capture_volume(elem, 0, &lvol);
      if (snd_mixer_selem_is_capture_mono(elem))  {
          rvol = lvol;
      } else {
        snd_mixer_selem_get_capture_volume(elem, 1, &rvol);
      }
      break;
    case CTL_PLAYBACK_SWITCH:
      snd_mixer_selem_get_playback_switch(elem, 0, &sw);
      *left = sw;
      *right = sw;
      return;
      break;
    default:
      g_assert_not_reached();
      break;
  }

  *left = convert_prange(lvol, min, max);
  *right = convert_prange(rvol, min, max);
}

static void
alsa_mixer_device_set_volume(mixer_t * mixer, int devid, int left, int right) {
  long min, max, lvol, rvol;
  alsa_mixer_t *alsamixer = ALSAMIXER(mixer);
  snd_mixer_elem_t *elem;

  elem = snd_mixer_find_selem(alsamixer->handle, alsamixer->sids[devid]);
  
  switch (alsamixer->ctltype[devid]) {
    case CTL_PLAYBACK:
      snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
      lvol = convert_prange1(left, min, max);
      rvol = convert_prange1(right, min, max);
      snd_mixer_selem_set_playback_volume(elem, 0, lvol);
      if (left == 0)
        snd_mixer_selem_set_playback_switch(elem, 0, 0);
      else
        snd_mixer_selem_set_playback_switch(elem, 0, 1);
      snd_mixer_selem_set_playback_volume(elem, 1, rvol);
      if (right == 0)
        snd_mixer_selem_set_playback_switch(elem, 1, 0);
      else
        snd_mixer_selem_set_playback_switch(elem, 1, 1);
      break;
    case CTL_CAPTURE:
      snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
      lvol = convert_prange1(left, min, max);
      rvol = convert_prange1(right, min, max);
      snd_mixer_selem_set_capture_volume(elem, 0, lvol);
      if (left == 0)
        snd_mixer_selem_set_capture_switch(elem, 0, 0);
      else
        snd_mixer_selem_set_capture_switch(elem, 0, 1);
      snd_mixer_selem_set_capture_volume(elem, 1, rvol);
      if (right == 0)
        snd_mixer_selem_set_capture_switch(elem, 1, 0);
      else
      snd_mixer_selem_set_capture_switch(elem, 1, 1);
      break;
    case CTL_PLAYBACK_SWITCH:
      snd_mixer_selem_set_playback_switch(elem, 0, left);
      break;
    default:
      g_assert_not_reached();
      break;
  }
}

mixer_idz_t *
alsa_mixer_get_id_list(void) {
  mixer_idz_t *result = NULL;
  snd_mixer_t *handle;
  int err;
  char name[64];
  int i;

  if ((err = snd_mixer_open(&handle, 0)) < 0) {
    return NULL;
  }

  for (i = 0; i < 64; i++) {
    sprintf(name, "hw:%d", i);
    if ((err = snd_mixer_attach(handle, name)) < 0) {
      break;
      snd_mixer_close(handle);
    }
    if ((err = snd_mixer_detach(handle, name)) < 0) {
      snd_mixer_close(handle);
      break;
    }
    result = mixer_id_list_add(name, result);
  }

  return result;
}

static mixer_ops_t alsa_mixer_ops = {
  .mixer_get_id_list = alsa_mixer_get_id_list,
  .mixer_open = alsa_mixer_open,
  .mixer_close = alsa_mixer_close,
  .mixer_device_get_fullscale = alsa_mixer_device_get_fullscale,
  .mixer_device_get_volume = alsa_mixer_device_get_volume,
  .mixer_device_set_volume = alsa_mixer_device_set_volume
};

static mixer_ops_t *
get_mixer_ops(void) {
  return &alsa_mixer_ops;
}

mixer_ops_t *
init_alsa_mixer(void) {
  return get_mixer_ops();
}
