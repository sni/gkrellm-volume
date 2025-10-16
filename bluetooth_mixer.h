#ifndef VOLUME_BLUETOOTH_MIXER_H
#define VOLUME_BLUETOOTH_MIXER_H

#include <gio/gio.h>
#include "mixer.h"

typedef struct {
    GDBusConnection *connection;
    GDBusProxy *media_proxy;
    gchar *device_path;
    gchar *transport_path;
    int changed_state;
} bluetooth_mixer_t;

mixer_ops_t *init_bluetooth_mixer(void);

#endif /* VOLUME_BLUETOOTH_MIXER_H */
