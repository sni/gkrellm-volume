#ifndef VOLUME_ALSA_MIXER_H
#define VOLUME_ALSA_MIXER_H
#include <alsa/asoundlib.h>

#include "mixer.h"

typedef struct {
    snd_mixer_t *handle;
    snd_mixer_selem_id_t **sids;
    int *ctltype;
    int changed_state;
} alsa_mixer_t;

mixer_ops_t *init_alsa_mixer(void);
#endif
