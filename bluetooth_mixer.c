/* GKrellM Volume plugin - Bluetooth mixer support
 |  Copyright (C) 2025
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gio/gio.h>

#include "mixer.h"
#include "bluetooth_mixer.h"

#define BTMIXER(x) ((bluetooth_mixer_t *)x->priv)

#define BLUEZ_SERVICE "org.bluez"
#define BLUEZ_MEDIA_INTERFACE "org.bluez.MediaTransport1"

static mixer_ops_t *get_mixer_ops(void);

/* Bluetooth device structure */
typedef struct {
    gchar *address;
    gchar *name;
    gchar *path;
    guint16 volume;
} bt_device_t;

static void
bt_error(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    fprintf(stderr, "gkrellm-volume bluetooth: ");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
    va_end(va);
}

/* Get list of connected Bluetooth audio devices */
static GSList *
bt_get_connected_devices(GDBusConnection *connection) {
    GSList *devices = NULL;
    GError *error = NULL;
    GVariant *result;
    GVariantIter *iter;
    const gchar *object_path;
    GVariant *interfaces;

    /* Get all managed objects from BlueZ */
    result = g_dbus_connection_call_sync(connection,
                                        BLUEZ_SERVICE,
                                        "/",
                                        "org.freedesktop.DBus.ObjectManager",
                                        "GetManagedObjects",
                                        NULL,
                                        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);

    if (error) {
        bt_error("Failed to get managed objects: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    g_variant_get(result, "(a{oa{sa{sv}}})", &iter);

    while (g_variant_iter_loop(iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariantIter *interface_iter;
        const gchar *interface_name;
        GVariant *properties;

        g_variant_get(interfaces, "a{sa{sv}}", &interface_iter);

        gboolean is_device = FALSE;
        gboolean is_connected = FALSE;
        gboolean has_audio = FALSE;
        gchar *name = NULL;
        gchar *address = NULL;

        while (g_variant_iter_loop(interface_iter, "{&s@a{sv}}", &interface_name, &properties)) {
            if (g_strcmp0(interface_name, "org.bluez.Device1") == 0) {
                is_device = TRUE;
                GVariantIter *prop_iter;
                const gchar *prop_name;
                GVariant *prop_value;

                g_variant_get(properties, "a{sv}", &prop_iter);
                while (g_variant_iter_loop(prop_iter, "{&sv}", &prop_name, &prop_value)) {
                    if (g_strcmp0(prop_name, "Connected") == 0) {
                        is_connected = g_variant_get_boolean(prop_value);
                    } else if (g_strcmp0(prop_name, "Name") == 0) {
                        name = g_variant_dup_string(prop_value, NULL);
                    } else if (g_strcmp0(prop_name, "Address") == 0) {
                        address = g_variant_dup_string(prop_value, NULL);
                    } else if (g_strcmp0(prop_name, "UUIDs") == 0) {
                        /* Check for audio UUIDs */
                        GVariantIter *uuid_iter;
                        const gchar *uuid;
                        g_variant_get(prop_value, "as", &uuid_iter);
                        while (g_variant_iter_loop(uuid_iter, "&s", &uuid)) {
                            /* A2DP Sink, Headset, or Handsfree UUIDs */
                            if (g_str_has_prefix(uuid, "0000110b") || /* A2DP Sink */
                                g_str_has_prefix(uuid, "00001108") || /* Headset */
                                g_str_has_prefix(uuid, "0000111e")) { /* Handsfree */
                                has_audio = TRUE;
                                break;
                            }
                        }
                        g_variant_iter_free(uuid_iter);
                    }
                }
                g_variant_iter_free(prop_iter);
            }
        }
        g_variant_iter_free(interface_iter);

        if (is_device && is_connected && has_audio && name && address) {
            bt_device_t *device = g_new0(bt_device_t, 1);
            device->path = g_strdup(object_path);
            device->name = name;
            device->address = address;
            device->volume = 127; /* Default volume */
            devices = g_slist_append(devices, device);
        } else {
            g_free(name);
            g_free(address);
        }
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);

    return devices;
}

static gchar *
bt_find_transport_path(GDBusConnection *connection, const gchar *device_path) {
    GError *error = NULL;
    GVariant *result;
    GVariantIter *iter;
    const gchar *object_path;
    GVariant *interfaces;
    gchar *transport_path = NULL;

    result = g_dbus_connection_call_sync(connection,
                                        BLUEZ_SERVICE,
                                        "/",
                                        "org.freedesktop.DBus.ObjectManager",
                                        "GetManagedObjects",
                                        NULL,
                                        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);

    if (error) {
        bt_error("Failed to find transport: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    g_variant_get(result, "(a{oa{sa{sv}}})", &iter);

    while (g_variant_iter_loop(iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariantIter *interface_iter;
        const gchar *interface_name;
        GVariant *properties;

        g_variant_get(interfaces, "a{sa{sv}}", &interface_iter);

        while (g_variant_iter_loop(interface_iter, "{&s@a{sv}}", &interface_name, &properties)) {
            if (g_strcmp0(interface_name, BLUEZ_MEDIA_INTERFACE) == 0) {
                GVariantIter *prop_iter;
                const gchar *prop_name;
                GVariant *prop_value;

                g_variant_get(properties, "a{sv}", &prop_iter);
                while (g_variant_iter_loop(prop_iter, "{&sv}", &prop_name, &prop_value)) {
                    if (g_strcmp0(prop_name, "Device") == 0) {
                        const gchar *dev_path = g_variant_get_string(prop_value, NULL);
                        if (g_strcmp0(dev_path, device_path) == 0) {
                            transport_path = g_strdup(object_path);
                            g_variant_iter_free(prop_iter);
                            g_variant_iter_free(interface_iter);
                            goto done;
                        }
                    }
                }
                g_variant_iter_free(prop_iter);
            }
        }
        g_variant_iter_free(interface_iter);
    }

done:
    g_variant_iter_free(iter);
    g_variant_unref(result);

    return transport_path;
}

static mixer_idz_t *
bluetooth_mixer_get_id_list(void) {
    mixer_idz_t *result = NULL;
    GError *error = NULL;
    GDBusConnection *connection;
    GSList *devices, *iter;

    connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        bt_error("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    devices = bt_get_connected_devices(connection);

    for (iter = devices; iter != NULL; iter = iter->next) {
        bt_device_t *device = (bt_device_t *)iter->data;
        result = mixer_id_list_add(device->path, result);
        g_free(device->path);
        g_free(device->name);
        g_free(device->address);
        g_free(device);
    }

    g_slist_free(devices);
    g_object_unref(connection);

    return result;
}

static mixer_t *
bluetooth_mixer_open(char *device_path) {
    mixer_t *result;
    bluetooth_mixer_t *bt_mixer;
    GError *error = NULL;
    GDBusConnection *connection;
    gchar *transport_path;

    connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        bt_error("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    /* Find the media transport for this device */
    transport_path = bt_find_transport_path(connection, device_path);
    if (!transport_path) {
        bt_error("No media transport found for device %s", device_path);
        g_object_unref(connection);
        return NULL;
    }

    result = g_new0(mixer_t, 1);
    bt_mixer = g_new0(bluetooth_mixer_t, 1);

    bt_mixer->connection = connection;
    bt_mixer->device_path = g_strdup(device_path);
    bt_mixer->transport_path = transport_path;
    bt_mixer->changed_state = 0;

    /* Create proxy for media transport */
    bt_mixer->media_proxy = g_dbus_proxy_new_sync(connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  BLUEZ_SERVICE,
                                                  transport_path,
                                                  BLUEZ_MEDIA_INTERFACE,
                                                  NULL,
                                                  &error);

    if (error) {
        bt_error("Failed to create proxy: %s", error->message);
        g_error_free(error);
        g_free(bt_mixer->device_path);
        g_free(bt_mixer->transport_path);
        g_free(bt_mixer);
        g_object_unref(connection);
        g_free(result);
        return NULL;
    }

    result->priv = bt_mixer;
    result->ops = get_mixer_ops();

    /* Get device name */
    GVariant *name_variant = g_dbus_connection_call_sync(connection,
                                                         BLUEZ_SERVICE,
                                                         device_path,
                                                         "org.freedesktop.DBus.Properties",
                                                         "Get",
                                                         g_variant_new("(ss)",
                                                                     "org.bluez.Device1",
                                                                     "Name"),
                                                         G_VARIANT_TYPE("(v)"),
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         -1,
                                                         NULL,
                                                         NULL);

    if (name_variant) {
        GVariant *value;
        g_variant_get(name_variant, "(v)", &value);
        result->name = g_variant_dup_string(value, NULL);
        g_variant_unref(value);
        g_variant_unref(name_variant);
    } else {
        result->name = g_strdup("Bluetooth Device");
    }

    /* We expose a single "Volume" device for BT headsets */
    result->nrdevices = 1;
    result->dev_names = g_new0(gchar *, 1);
    result->dev_realnames = g_new0(gchar *, 1);
    result->dev_realnames[0] = g_strdup("Volume");
    result->dev_names[0] = NULL;

    return result;
}

static void
bluetooth_mixer_close(mixer_t *mixer) {
    bluetooth_mixer_t *bt_mixer = BTMIXER(mixer);

    if (bt_mixer->media_proxy)
        g_object_unref(bt_mixer->media_proxy);

    if (bt_mixer->connection)
        g_object_unref(bt_mixer->connection);

    g_free(bt_mixer->device_path);
    g_free(bt_mixer->transport_path);
    g_free(bt_mixer);

    g_free(mixer->name);
    g_free(mixer->dev_realnames[0]);
    g_free(mixer->dev_realnames);
    g_free(mixer->dev_names);
    g_free(mixer);
}

static long
bluetooth_device_get_fullscale(mixer_t *mixer, int devid) {
    /* Bluetooth volume range is 0-127 */
    return 127;
}

static void
bluetooth_device_get_volume(mixer_t *mixer, int devid, int *left, int *right) {
    bluetooth_mixer_t *bt_mixer = BTMIXER(mixer);
    GError *error = NULL;
    GVariant *result;

    if (!bt_mixer->media_proxy) {
        *left = *right = 0;
        return;
    }

    result = g_dbus_proxy_call_sync(bt_mixer->media_proxy,
                                   "org.freedesktop.DBus.Properties.Get",
                                   g_variant_new("(ss)",
                                               BLUEZ_MEDIA_INTERFACE,
                                               "Volume"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

    if (error) {
        bt_error("Failed to get volume: %s", error->message);
        g_error_free(error);
        *left = *right = 64; /* Default middle volume */
        return;
    }

    if (result) {
        GVariant *value;
        g_variant_get(result, "(v)", &value);
        guint16 volume = g_variant_get_uint16(value);
        *left = *right = volume;
        g_variant_unref(value);
        g_variant_unref(result);
    } else {
        *left = *right = 64;
    }
}

static void
bluetooth_device_set_volume(mixer_t *mixer, int devid, int left, int right) {
    bluetooth_mixer_t *bt_mixer = BTMIXER(mixer);
    GError *error = NULL;
    guint16 volume;

    if (!bt_mixer->media_proxy)
        return;

    /* Use the higher of left/right for stereo devices */
    volume = (left > right) ? left : right;

    /* Clamp to valid range */
    if (volume > 127)
        volume = 127;

    g_dbus_proxy_call_sync(bt_mixer->media_proxy,
                          "org.freedesktop.DBus.Properties.Set",
                          g_variant_new("(ssv)",
                                      BLUEZ_MEDIA_INTERFACE,
                                      "Volume",
                                      g_variant_new_uint16(volume)),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          &error);

    if (error) {
        bt_error("Failed to set volume: %s", error->message);
        g_error_free(error);
    }
}

static mixer_ops_t bluetooth_ops = {
    bluetooth_mixer_get_id_list,
    bluetooth_mixer_open,
    bluetooth_mixer_close,
    bluetooth_device_get_fullscale,
    bluetooth_device_get_volume,
    bluetooth_device_set_volume
};

static mixer_ops_t *
get_mixer_ops(void) {
    return &bluetooth_ops;
}

mixer_ops_t *
init_bluetooth_mixer(void) {
    return &bluetooth_ops;
}
