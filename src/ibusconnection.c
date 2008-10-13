/* vim:set et sts=4: */
/* ibus - The Input Bus
 * Copyright (C) 2008-2009 Huang Peng <shawn.p.huang@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "ibusconnection.h"
#include "ibusinternal.h"

#define IBUS_CONNECTION_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), IBUS_TYPE_CONNECTION, IBusConnectionPrivate))
#define DECLARE_PRIV IBusConnectionPrivate *priv = IBUS_CONNECTION_GET_PRIVATE(connection)

enum {
    DBUS_SIGNAL,
    DBUS_MESSAGE,
    DISCONNECTED,
    LAST_SIGNAL,
};


/* IBusConnectionPriv */
struct _IBusConnectionPrivate {
    DBusConnection *connection;
    gboolean shared;
};
typedef struct _IBusConnectionPrivate IBusConnectionPrivate;

static guint            _signals[LAST_SIGNAL] = { 0 };

/* functions prototype */
static void     ibus_connection_class_init  (IBusConnectionClass    *klass);
static void     ibus_connection_init        (IBusConnection         *connection);
static void     ibus_connection_dispose    (IBusConnection         *connection);

static gboolean ibus_connection_dbus_message(IBusConnection         *connection,
                                             DBusMessage            *message);
static gboolean ibus_connection_dbus_signal (IBusConnection         *connection,
                                             DBusMessage            *message);
static void     ibus_connection_disconnected(IBusConnection         *connection);

static IBusObjectClass  *_parent_class = NULL;
static GHashTable       *_connections = NULL;

GType
ibus_connection_get_type (void)
{
    static GType type = 0;

    static const GTypeInfo type_info = {
        sizeof (IBusConnectionClass),
        (GBaseInitFunc)     NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc)    ibus_connection_class_init,
        NULL,               /* class finalize */
        NULL,               /* class data */
        sizeof (IBusConnection),
        0,
        (GInstanceInitFunc) ibus_connection_init,
    };

    if (type == 0) {
        type = g_type_register_static (IBUS_TYPE_OBJECT,
                    "IBusConnection",
                    &type_info,
                    (GTypeFlags)0);
    }

    return type;
}

IBusConnection *
ibus_connection_new (void)
{
    IBusConnection *connection = IBUS_CONNECTION (g_object_new (IBUS_TYPE_CONNECTION, NULL));
    return connection;
}

static void
ibus_connection_class_init (IBusConnectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    _parent_class = (IBusObjectClass *) g_type_class_peek_parent (klass);

    g_type_class_add_private (klass, sizeof (IBusConnectionPrivate));

    gobject_class->dispose = (GObjectFinalizeFunc) ibus_connection_dispose;

    klass->dbus_message = ibus_connection_dbus_message;
    klass->dbus_signal  = ibus_connection_dbus_signal;
    klass->disconnected = ibus_connection_disconnected;

    _signals[DBUS_MESSAGE] =
        g_signal_new (I_("dbus-message"),
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (IBusConnectionClass, dbus_message),
            NULL, NULL,
            ibus_marshal_BOOLEAN__POINTER,
            G_TYPE_BOOLEAN, 1,
            G_TYPE_POINTER);

    _signals[DBUS_SIGNAL] =
        g_signal_new (I_("dbus-signal"),
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (IBusConnectionClass, dbus_signal),
            NULL, NULL,
            ibus_marshal_BOOL__POINTER,
            G_TYPE_NONE, 1,
            G_TYPE_POINTER);

    _signals[DISCONNECTED] =
        g_signal_new (I_("disconnected"),
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_FIRST,
            G_STRUCT_OFFSET (IBusConnectionClass, disconnected),
            NULL, NULL,
            ibus_marshal_VOID__VOID,
            G_TYPE_NONE, 0);

}

static void
ibus_connection_init (IBusConnection *connection)
{
    DECLARE_PRIV;
    priv->connection = NULL;
    priv->shared = FALSE;
}

static void
ibus_connection_dispose (IBusConnection *connection)
{
    IBusConnectionPrivate *priv;
    priv = IBUS_CONNECTION_GET_PRIVATE (connection);

    if (!priv->shared && priv->connection) {
        dbus_connection_close (priv->connection);
        dbus_connection_unref (priv->connection);
        priv->connection = NULL;
        goto _out;
    }

    if (priv->shared && priv->connection) {
        g_warn_if_fail (_connections != NULL);
        if (_connections != NULL) {
            g_hash_table_remove (_connections, priv->connection);
        }
        dbus_connection_unref (priv->connection);
        priv->connection = NULL;
        goto _out;
    }
_out:
    G_OBJECT_CLASS(_parent_class)->dispose (G_OBJECT (connection));
}

static gboolean
ibus_connection_dbus_message (IBusConnection *connection, DBusMessage *message)
{
    gboolean retval = FALSE;
    if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_SIGNAL)
        g_signal_emit (connection, _signals[DBUS_SIGNAL], 0, message, &retval);

    return retval;
}

static gboolean
ibus_connection_dbus_signal (IBusConnection *connection, DBusMessage *message)
{
    DECLARE_PRIV;

    if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
        dbus_connection_unref (priv->connection);
        priv->connection = NULL;
        priv->shared = FALSE;
        g_signal_emit (connection, _signals[DISCONNECTED], 0);
        return FALSE;
    }
    return FALSE;
}

static void
ibus_connection_disconnected (IBusConnection         *connection)
{
    ibus_object_destroy (IBUS_OBJECT (connection));
}

static DBusHandlerResult
_connection_handle_message_cb (DBusConnection *dbus_connection, DBusMessage *message, IBusConnection *connection)
{
    gboolean retval = FALSE;
    g_signal_emit (connection, _signals[DBUS_MESSAGE], 0, message, &retval);
    return retval ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gint
_get_slot ()
{
    static gint slot = -1;
    if (slot == -1) {
        dbus_connection_allocate_data_slot (&slot);
    }
    return slot;
}

void
ibus_connection_set_connection (IBusConnection *connection, DBusConnection *dbus_connection, gboolean shared)
{
    gboolean result;
    DECLARE_PRIV;

    g_assert (priv->connection == NULL);
    g_assert (dbus_connection != NULL);
    g_assert (dbus_connection_get_is_connected (dbus_connection));

    priv->connection = dbus_connection_ref (dbus_connection);
    priv->shared = shared;
    
    dbus_setup_connection (priv->connection);

    result = dbus_connection_add_filter (priv->connection,
                    (DBusHandleMessageFunction) _connection_handle_message_cb,
                    connection, NULL);
    g_warn_if_fail (result);
    dbus_connection_set_data (priv->connection, _get_slot(), connection, NULL);
}

IBusConnection *
ibus_connection_open (const gchar *address)
{
    g_return_val_if_fail (address != NULL, NULL);

    if (_connections == NULL) {
        _connections = g_hash_table_new (g_direct_hash, g_direct_equal);
    }

    DBusError error;
    DBusConnection *dbus_connection;

    dbus_error_init (&error);
    dbus_connection = dbus_connection_open (address, &error);
    if (dbus_connection == NULL) {
        g_warning ("Connect to %s failed. %s.", address, error.message);
        dbus_error_free (&error);
        return NULL;
    }

    IBusConnection *connection;
    connection = g_hash_table_lookup (_connections, dbus_connection);

    if (connection) {
        dbus_connection_unref (dbus_connection);
        g_object_ref (connection);
        return connection;
    }

    connection = ibus_connection_new ();
    ibus_connection_set_connection (connection, dbus_connection, TRUE);
    g_hash_table_insert (_connections, dbus_connection, connection);

    return connection;
}

IBusConnection *
ibus_connection_open_private (const gchar *address)
{
    g_return_val_if_fail (address != NULL, NULL);

    DBusError error;
    DBusConnection *dbus_connection;

    dbus_error_init (&error);
    dbus_connection = dbus_connection_open_private (address, &error);
    if (dbus_connection == NULL) {
        g_warning ("Connect to %s failed. %s.", address, error.message);
        dbus_error_free (&error);
        return NULL;
    }

    IBusConnection *connection;
    connection = ibus_connection_new ();
    ibus_connection_set_connection (connection, dbus_connection, FALSE);

    return connection;
}

void ibus_connection_close (IBusConnection     *connection)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    IBusConnectionPrivate *priv;
    priv = IBUS_CONNECTION_GET_PRIVATE (connection);

    dbus_connection_close (priv->connection);
}

gboolean
ibus_connection_get_is_connected (IBusConnection *connection)
{
    DECLARE_PRIV;
    if (priv->connection == NULL) {
        return FALSE;
    }
    return dbus_connection_get_is_connected (priv->connection);
}

DBusConnection *
ibus_connection_get_connection (IBusConnection *connection)
{
    DECLARE_PRIV;
    return priv->connection;
}

typedef struct _VTableCallData {
    IBusMessageFunc message_func;
    gpointer user_data;
}VTableCallData;

void
_unregister_function (DBusConnection *dbus_connection, VTableCallData *data)
{
    g_slice_free (VTableCallData, data);
}

DBusHandlerResult
_message_function (DBusConnection *dbus_connection, DBusMessage *message, VTableCallData *data)
{
    gboolean retval;
    IBusConnection *connection;

    connection = IBUS_CONNECTION (dbus_connection_get_data (dbus_connection, _get_slot()));
    retval = data->message_func (connection, message, data->user_data);

    return retval ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

gboolean
ibus_connection_register_object_path (IBusConnection *connection,
        const gchar *path, IBusMessageFunc message_func, gpointer user_data)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (path != NULL);
    g_assert (message_func != NULL);

    DECLARE_PRIV;
    gboolean retval;
    DBusObjectPathVTable vtable = {0};
    VTableCallData *data;

    vtable.unregister_function = (DBusObjectPathUnregisterFunction) _unregister_function;
    vtable.message_function = (DBusObjectPathMessageFunction) _message_function;

    data = g_slice_new (VTableCallData);
    data->message_func = message_func;
    data->user_data = user_data;

    retval = dbus_connection_register_object_path (priv->connection, path, &vtable, data);
    if (!retval) {
        g_warning ("Out of memory!");
        return FALSE;
    }
    return TRUE;
}

gboolean
ibus_connection_unregister_object_path (IBusConnection *connection, const gchar *path)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (path != NULL);

    DECLARE_PRIV;
    gboolean retval;

    retval = dbus_connection_unregister_object_path (priv->connection, path);
    if (!retval) {
        g_warning ("Out of memory!");
        return FALSE;
    }

    return TRUE;
}


gboolean
ibus_connection_send (IBusConnection *connection, DBusMessage *message)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (message != NULL);

    IBusConnectionPrivate *priv;

    priv = IBUS_CONNECTION_GET_PRIVATE (connection);
    return dbus_connection_send (priv->connection, message, NULL);
}


gboolean
ibus_connection_send_signal (IBusConnection     *connection,
                             const gchar        *path,
                             const gchar        *interface,
                             const gchar        *name,
                             gint                first_arg_type,
                             ...)
{
    va_list args;
    gboolean retval;

    va_start (args, first_arg_type);
    retval = ibus_connection_send_signal_valist (connection,
                path, interface, name,
                first_arg_type, args);
    va_end (args);
    return retval;
}

gboolean
ibus_connection_send_signal_valist (IBusConnection  *connection,
                                    const gchar     *path,
                                    const gchar     *interface,
                                    const gchar     *name,
                                    gint             first_arg_type,
                                    va_list          args)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (interface != NULL);
    g_assert (name != NULL);

    gboolean retval;
    DBusMessage *message;

    message = dbus_message_new_signal (path, interface, name);

    dbus_message_append_args_valist (message, first_arg_type, args);
    retval = ibus_connection_send (connection, message);
    dbus_message_unref (message);

    return retval;
}

gboolean
ibus_connection_send_valist (IBusConnection  *connection,
                             gint             message_type,
                             const gchar     *path,
                             const gchar     *interface,
                             const gchar     *name,
                             gint             first_arg_type,
                             va_list          args)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (interface != NULL);
    g_assert (name != NULL);

    gboolean retval;
    DBusMessage *message;

    message = dbus_message_new (message_type);
    dbus_message_set_path (message, path);
    dbus_message_set_interface (message, interface);
    dbus_message_set_member (message, name);

    dbus_message_append_args_valist (message, first_arg_type, args);
    retval = ibus_connection_send (connection, message);
    dbus_message_unref (message);

    return retval;
}

void
ibus_connection_flush (IBusConnection *connection)
{
    g_assert (IBUS_IS_CONNECTION (connection));
    g_assert (ibus_connection_get_is_connected (connection));

    IBusConnectionPrivate *priv;

    priv = IBUS_CONNECTION_GET_PRIVATE (connection);

    dbus_connection_flush (priv->connection);
}
