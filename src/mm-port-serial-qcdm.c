/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-serial-qcdm.h"
#include "libqcdm/src/com.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPortSerialQcdm, mm_port_serial_qcdm, MM_TYPE_PORT_SERIAL)

#define MM_PORT_SERIAL_QCDM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PORT_SERIAL_QCDM, MMPortSerialQcdmPrivate))

typedef struct {
    gboolean foo;
} MMPortSerialQcdmPrivate;


/*****************************************************************************/

static gboolean
find_qcdm_start (GByteArray *response, gsize *start)
{
    int i, last = -1;

    /* Look for 3 bytes and a QCDM frame marker, ie enough data for a valid
     * frame.  There will usually be three cases here; (1) a QCDM frame
     * starting with data and terminated by 0x7E, and (2) a QCDM frame starting
     * with 0x7E and ending with 0x7E, and (3) a non-QCDM frame that still
     * uses HDLC framing (like Sierra CnS) that starts and ends with 0x7E.
     */
    for (i = 0; i < response->len; i++) {
        if (response->data[i] == 0x7E) {
            if (i > last + 3) {
                /* Got a full QCDM frame; 3 non-0x7E bytes and a terminator */
                if (start)
                    *start = last + 1;
                return TRUE;
            }

            /* Save position of the last QCDM frame marker */
            last = i;
        }
    }
    return FALSE;
}

static gboolean
parse_response (MMPortSerial *port, GByteArray *response, GError **error)
{
    return find_qcdm_start (response, NULL);
}

static gsize
handle_response (MMPortSerial *port,
                 GByteArray *response,
                 GError *error,
                 GCallback callback,
                 gpointer callback_data)
{
    MMPortSerialQcdmResponseFn response_callback = (MMPortSerialQcdmResponseFn) callback;
    GByteArray *unescaped = NULL;
    guint8 *unescaped_buffer;
    GError *dm_error = NULL;
    gsize used = 0;
    gsize start = 0;
    gboolean success = FALSE;
    qcdmbool more = FALSE;
    gsize unescaped_len = 0;

    if (error)
        goto callback;

    /* Get the offset into the buffer of where the QCDM frame starts */
    if (!find_qcdm_start (response, &start)) {
        g_set_error_literal (&dm_error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Failed to parse QCDM packet.");
        /* Discard the unparsable data */
        used = response->len;
        goto callback;
    }

    unescaped_buffer = g_malloc (1024);
    success = dm_decapsulate_buffer ((const char *) (response->data + start),
                                     response->len - start,
                                     (char *) unescaped_buffer,
                                     1024,
                                     &unescaped_len,
                                     &used,
                                     &more);
    if (!success) {
        g_set_error_literal (&dm_error,
                             MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Failed to unescape QCDM packet.");
        g_free (unescaped_buffer);
        unescaped_buffer = NULL;
    } else if (more) {
        /* Need more data; we shouldn't have gotten here since the parse
         * function checks for the end-of-frame marker, but whatever.
         */
        g_free (unescaped_buffer);
        return 0;
    } else {
        /* Successfully decapsulated the DM command */
        g_assert (unescaped_len <= 1024);
        unescaped_buffer = g_realloc (unescaped_buffer, unescaped_len);
        unescaped = g_byte_array_new_take (unescaped_buffer, unescaped_len);
    }

callback:
    response_callback (MM_PORT_SERIAL_QCDM (port),
                       unescaped,
                       dm_error ? dm_error : error,
                       callback_data);

    if (unescaped)
        g_byte_array_unref (unescaped);
    g_clear_error (&dm_error);

    return start + used;
}

/*****************************************************************************/

void
mm_port_serial_qcdm_queue_command (MMPortSerialQcdm *self,
                                   GByteArray *command,
                                   guint32 timeout_seconds,
                                   GCancellable *cancellable,
                                   MMPortSerialQcdmResponseFn callback,
                                   gpointer user_data)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT_SERIAL_QCDM (self));
    g_return_if_fail (command != NULL);

    /* 'command' is expected to be already CRC-ed and escaped */
    mm_port_serial_queue_command (MM_PORT_SERIAL (self),
                                  command,
                                  TRUE,
                                  timeout_seconds,
                                  cancellable,
                                  (MMSerialResponseFn) callback,
                                  user_data);
}

void
mm_port_serial_qcdm_queue_command_cached (MMPortSerialQcdm *self,
                                          GByteArray *command,
                                          guint32 timeout_seconds,
                                          GCancellable *cancellable,
                                          MMPortSerialQcdmResponseFn callback,
                                          gpointer user_data)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT_SERIAL_QCDM (self));
    g_return_if_fail (command != NULL);

    /* 'command' is expected to be already CRC-ed and escaped */
    mm_port_serial_queue_command_cached (MM_PORT_SERIAL (self),
                                         command,
                                         TRUE,
                                         timeout_seconds,
                                         cancellable,
                                         (MMSerialResponseFn) callback,
                                         user_data);
}

static void
debug_log (MMPortSerial *port, const char *prefix, const char *buf, gsize len)
{
    static GString *debug = NULL;
    const char *s = buf;

    if (!debug)
        debug = g_string_sized_new (512);

    g_string_append (debug, prefix);

    while (len--)
        g_string_append_printf (debug, " %02x", (guint8) (*s++ & 0xFF));

    mm_dbg ("(%s): %s", mm_port_get_device (MM_PORT (port)), debug->str);
    g_string_truncate (debug, 0);
}

/*****************************************************************************/

static gboolean
config_fd (MMPortSerial *port, int fd, GError **error)
{
    int err;

    err = qcdm_port_setup (fd);
    if (err != QCDM_SUCCESS) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_OPEN_FAILED,
                     "Failed to open QCDM port: %d", err);
        return FALSE;
    }
    return TRUE;
}

/*****************************************************************************/

MMPortSerialQcdm *
mm_port_serial_qcdm_new (const char *name)
{
    return MM_PORT_SERIAL_QCDM (g_object_new (MM_TYPE_PORT_SERIAL_QCDM,
                                              MM_PORT_DEVICE, name,
                                              MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                              MM_PORT_TYPE, MM_PORT_TYPE_QCDM,
                                              MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                                              NULL));
}

MMPortSerialQcdm *
mm_port_serial_qcdm_new_fd (int fd)
{
    MMPortSerialQcdm *port;
    char *name;

    name = g_strdup_printf ("port%d", fd);
    port = MM_PORT_SERIAL_QCDM (g_object_new (MM_TYPE_PORT_SERIAL_QCDM,
                                              MM_PORT_DEVICE, name,
                                              MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                              MM_PORT_TYPE, MM_PORT_TYPE_QCDM,
                                              MM_PORT_SERIAL_FD, fd,
                                              MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                                              NULL));
    g_free (name);
    return port;
}

static void
mm_port_serial_qcdm_init (MMPortSerialQcdm *self)
{
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (mm_port_serial_qcdm_parent_class)->finalize (object);
}

static void
mm_port_serial_qcdm_class_init (MMPortSerialQcdmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMPortSerialClass *port_class = MM_PORT_SERIAL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortSerialQcdmPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    port_class->parse_response = parse_response;
    port_class->handle_response = handle_response;
    port_class->config_fd = config_fd;
    port_class->debug_log = debug_log;
}