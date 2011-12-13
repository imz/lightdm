/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "greeter.h"
#include "ldm-marshal.h"

enum {
    CONNECTED,
    START_AUTHENTICATION,
    START_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
{
    /* Session running on */
    Session *session;

    /* Buffer for data read from greeter */
    guint8 *read_buffer;
    gsize n_read;
  
    /* Hints for the greeter */
    GHashTable *hints;

    /* Default session to use */   
    gchar *default_session;

    /* Sequence number of current PAM session */
    guint32 authentication_sequence_number;

    /* PAM session being constructed by the greeter */
    PAMSession *authentication;

    /* TRUE if can log into guest accounts */
    gboolean allow_guest;

    /* TRUE if logging into guest session */
    gboolean guest_account_authenticated;

    /* Communication channels to communicate with */
    GIOChannel *to_greeter_channel;
    GIOChannel *from_greeter_channel;
};

G_DEFINE_TYPE (Greeter, greeter, G_TYPE_OBJECT);

/* Messages from the greeter to the server */
typedef enum
{
    GREETER_MESSAGE_CONNECT = 0,
    GREETER_MESSAGE_AUTHENTICATE,
    GREETER_MESSAGE_AUTHENTICATE_AS_GUEST,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION,
    GREETER_MESSAGE_START_SESSION,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION,
    GREETER_MESSAGE_SET_LANGUAGE
} GreeterMessage;

/* Messages from the server to the greeter */
typedef enum
{
    SERVER_MESSAGE_CONNECTED = 0,
    SERVER_MESSAGE_PROMPT_AUTHENTICATION,
    SERVER_MESSAGE_END_AUTHENTICATION,
    SERVER_MESSAGE_SESSION_RESULT
} ServerMessage;

Greeter *
greeter_new (Session *session)
{
    Greeter *greeter = g_object_new (GREETER_TYPE, NULL);

    greeter->priv->session = g_object_ref (session);

    return greeter;
}

void
greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest)
{
    greeter->priv->allow_guest = allow_guest;
}

void
greeter_set_hint (Greeter *greeter, const gchar *name, const gchar *value)
{
    g_hash_table_insert (greeter->priv->hints, g_strdup (name), g_strdup (value));
}

static guint32
int_length ()
{
    return 4;
}

#define HEADER_SIZE (sizeof (guint32) * 2)
#define MAX_MESSAGE_LENGTH 1024

static void
write_message (Greeter *greeter, guint8 *message, gsize message_length)
{
    GIOStatus status;
    GError *error = NULL;

    status = g_io_channel_write_chars (greeter->priv->to_greeter_channel, (gchar *) message, message_length, NULL, &error);
    if (error)
        g_warning ("Error writing to greeter: %s", error->message);
    g_clear_error (&error);
    if (status == G_IO_STATUS_NORMAL)
        g_debug ("Wrote %zi bytes to greeter", message_length);
    g_io_channel_flush (greeter->priv->to_greeter_channel, NULL);
}

static void
write_int (guint8 *buffer, gint buffer_length, guint32 value, gsize *offset)
{
    if (*offset + 4 >= buffer_length)
        return;
    buffer[*offset] = value >> 24;
    buffer[*offset+1] = (value >> 16) & 0xFF;
    buffer[*offset+2] = (value >> 8) & 0xFF;
    buffer[*offset+3] = value & 0xFF;
    *offset += 4;
}

static void
write_string (guint8 *buffer, gint buffer_length, const gchar *value, gsize *offset)
{
    gint length;
  
    if (value)
        length = strlen (value);
    else
        length = 0;
    write_int (buffer, buffer_length, length, offset);
    if (*offset + length >= buffer_length)
        return;
    if (length > 0)
    {
        memcpy (buffer + *offset, value, length);
        *offset += length;
    }
}

static void
write_header (guint8 *buffer, gint buffer_length, guint32 id, guint32 length, gsize *offset)
{
    write_int (buffer, buffer_length, id, offset);
    write_int (buffer, buffer_length, length, offset);
}

static guint32
string_length (const gchar *value)
{
    if (value == NULL)
        return int_length ();
    else
        return int_length () + strlen (value);
}

static void
handle_connect (Greeter *greeter, const gchar *version)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    guint32 length;
    GHashTableIter iter;
    gpointer key, value;

    g_debug ("Greeter connected version=%s", version);

    length = string_length (VERSION);
    g_hash_table_iter_init (&iter, greeter->priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
        length += string_length (key) + string_length (value);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_CONNECTED, length, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, VERSION, &offset);
    g_hash_table_iter_init (&iter, greeter->priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        write_string (message, MAX_MESSAGE_LENGTH, key, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, value, &offset);
    }
    write_message (greeter, message, offset);

    g_signal_emit (greeter, signals[CONNECTED], 0);
}

static void
pam_messages_cb (PAMSession *authentication, int num_msg, const struct pam_message **msg, Greeter *greeter)
{
    int i;
    guint32 size;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    int n_prompts = 0;

    /* Respond to d-bus query with messages */
    g_debug ("Prompt greeter with %d message(s)", num_msg);
    size = int_length () + string_length (pam_session_get_username (authentication)) + int_length ();
    for (i = 0; i < num_msg; i++)
        size += int_length () + string_length (msg[i]->msg);
  
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_PROMPT_AUTHENTICATION, size, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, greeter->priv->authentication_sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, pam_session_get_username (authentication), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, num_msg, &offset);
    for (i = 0; i < num_msg; i++)
    {
        write_int (message, MAX_MESSAGE_LENGTH, msg[i]->msg_style, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, msg[i]->msg, &offset);

        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            n_prompts++;
    }
    write_message (greeter, message, offset);

    /* Continue immediately if nothing to respond with */
    // FIXME: Should probably give the greeter a chance to ack the message
    if (n_prompts == 0)
    {
        struct pam_response *response;
        response = calloc (num_msg, sizeof (struct pam_response));
        pam_session_respond (greeter->priv->authentication, response);
    }
}

static void
send_end_authentication (Greeter *greeter, guint32 sequence_number, const gchar *username, int result)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_END_AUTHENTICATION, int_length () + string_length (username) + int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, username, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, result, &offset);
    write_message (greeter, message, offset); 
}

static void
authentication_result_cb (PAMSession *authentication, int result, Greeter *greeter)
{
    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (authentication), pam_session_strerror (authentication, result));

    if (result == PAM_SUCCESS)
        g_debug ("User %s authorized", pam_session_get_username (authentication));

    send_end_authentication (greeter, greeter->priv->authentication_sequence_number, pam_session_get_username (authentication), result);
}

static void
reset_session (Greeter *greeter)
{
    if (greeter->priv->authentication)
    {
        g_signal_handlers_disconnect_matched (greeter->priv->authentication, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
        pam_session_cancel (greeter->priv->authentication);
        g_object_unref (greeter->priv->authentication);
        greeter->priv->authentication = NULL;
    }

    greeter->priv->guest_account_authenticated = FALSE;
}

static void
handle_login (Greeter *greeter, guint32 sequence_number, const gchar *username)
{
    gboolean result;
    GError *error = NULL;

    if (username[0] == '\0')
    {
        g_debug ("Greeter start authentication");
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for %s", username);

    reset_session (greeter);

    greeter->priv->authentication_sequence_number = sequence_number;
    g_signal_emit (greeter, signals[START_AUTHENTICATION], 0, username, &greeter->priv->authentication);
    if (!greeter->priv->authentication)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    g_signal_connect (G_OBJECT (greeter->priv->authentication), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
    g_signal_connect (G_OBJECT (greeter->priv->authentication), "authentication-result", G_CALLBACK (authentication_result_cb), greeter);
    result = pam_session_authenticate (greeter->priv->authentication, &error);
    if (error)
        g_debug ("Failed to start authentication: %s", error->message);
    if (!result)
        send_end_authentication (greeter, sequence_number, "", PAM_SYSTEM_ERR);
    g_clear_error (&error);
}

static void
handle_login_as_guest (Greeter *greeter, guint32 sequence_number)
{
    g_debug ("Greeter start authentication for guest account");

    reset_session (greeter);

    if (!greeter->priv->allow_guest)
    {
        g_debug ("Guest account is disabled");
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    greeter->priv->guest_account_authenticated = TRUE;  
    send_end_authentication (greeter, sequence_number, "", PAM_SUCCESS);
}

static void
handle_continue_authentication (Greeter *greeter, gchar **secrets)
{
    int num_messages;
    const struct pam_message **messages;
    struct pam_response *response;
    int i, j, n_prompts = 0;

    /* Not in authentication */
    if (greeter->priv->authentication == NULL)
        return;

    num_messages = pam_session_get_num_messages (greeter->priv->authentication);
    messages = pam_session_get_messages (greeter->priv->authentication);

    /* Check correct number of responses */
    for (i = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_prompts++;
    }
    if (g_strv_length (secrets) != n_prompts)
    {
        pam_session_cancel (greeter->priv->authentication);
        return;
    }

    g_debug ("Continue authentication");

    /* Build response */
    response = calloc (num_messages, sizeof (struct pam_response));
    for (i = 0, j = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
        {
            response[i].resp = strdup (secrets[j]); // FIXME: Need to convert from UTF-8
            j++;
        }
    }

    pam_session_respond (greeter->priv->authentication, response);
}

static void
handle_cancel_authentication (Greeter *greeter)
{
    /* Not in authentication */
    if (greeter->priv->authentication == NULL)
        return;

    g_debug ("Cancel authentication");

    pam_session_cancel (greeter->priv->authentication);
}

static void
handle_start_session (Greeter *greeter, const gchar *session)
{
    gboolean result;

    if (strcmp (session, "") == 0)
        session = NULL;

    if (greeter->priv->guest_account_authenticated || pam_session_get_is_authenticated (greeter->priv->authentication))
    {
        if (session)
            g_debug ("Greeter requests session %s", session);
        else
            g_debug ("Greeter requests default session");     
        g_signal_emit (greeter, signals[START_SESSION], 0, session, &result);
    }
    else
    {
        g_debug ("Ignoring start session request, user is not authorized");
        result = FALSE;
    }

    if (!result)
    {
        guint8 message[MAX_MESSAGE_LENGTH];
        gsize offset = 0;

        write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SESSION_RESULT, int_length (), &offset);
        write_int (message, MAX_MESSAGE_LENGTH, 1, &offset);
        write_message (greeter, message, offset);
    }
}

static void
handle_set_language (Greeter *greeter, const gchar *language)
{
    User *user;

    if (!greeter->priv->guest_account_authenticated && !pam_session_get_is_authenticated (greeter->priv->authentication))
    {
        g_debug ("Ignoring set language request, user is not authorized");
        return;
    }

    // FIXME: Could use this
    if (greeter->priv->guest_account_authenticated)
    {
        g_debug ("Ignoring set language request for guest user");
        return;
    }

    g_debug ("Greeter sets language %s", language);
    user = pam_session_get_user (greeter->priv->authentication);
    user_set_locale (user, language);
}

static guint32
read_int (Greeter *greeter, gsize *offset)
{
    guint32 value;
    guint8 *buffer;
    if (greeter->priv->n_read - *offset < sizeof (guint32))
    {
        g_warning ("Not enough space for int, need %zu, got %zu", sizeof (guint32), greeter->priv->n_read - *offset);
        return 0;
    }
    buffer = greeter->priv->read_buffer + *offset;
    value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += int_length ();
    return value;
}

static gchar *
read_string (Greeter *greeter, gsize *offset)
{
    guint32 length;
    gchar *value;

    length = read_int (greeter, offset);
    if (greeter->priv->n_read - *offset < length)
    {
        g_warning ("Not enough space for string, need %u, got %zu", length, greeter->priv->n_read - *offset);
        return g_strdup ("");
    }

    value = g_malloc (sizeof (gchar *) * (length + 1));
    memcpy (value, greeter->priv->read_buffer + *offset, length);
    value[length] = '\0';
    *offset += length;

    return value;
}

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    Greeter *greeter = data;
    gsize n_to_read, n_read, offset;
    GIOStatus status;
    int id, n_secrets, i;
    guint32 sequence_number;
    gchar *version, *username, *session_name, *language;
    gchar **secrets;
    GError *error = NULL;

    if (condition == G_IO_HUP)
    {
        g_debug ("Greeter closed communication channel");
        return FALSE;
    }
  
    n_to_read = HEADER_SIZE;
    if (greeter->priv->n_read >= HEADER_SIZE)
    {
        offset = int_length ();
        n_to_read += read_int (greeter, &offset);
    }

    status = g_io_channel_read_chars (greeter->priv->from_greeter_channel,
                                      (gchar *) greeter->priv->read_buffer + greeter->priv->n_read,
                                      n_to_read - greeter->priv->n_read,
                                      &n_read,
                                      &error);
    if (error)
        g_warning ("Error reading from greeter: %s", error->message);
    g_clear_error (&error);
    if (status != G_IO_STATUS_NORMAL)
        return TRUE;

    g_debug ("Read %zi bytes from greeter", n_read);
    /*for (i = 0; i < n_read; i++)
       g_print ("%02X ", greeter->priv->read_buffer[greeter->priv->n_read+i]);
    g_print ("\n");*/

    greeter->priv->n_read += n_read;
    if (greeter->priv->n_read != n_to_read)
        return TRUE;

    /* If have header, rerun for content */
    if (greeter->priv->n_read == HEADER_SIZE)
    {
        gsize offset = int_length ();
        n_to_read = read_int (greeter, &offset);
        if (n_to_read > 0)
        {
            greeter->priv->read_buffer = g_realloc (greeter->priv->read_buffer, HEADER_SIZE + n_to_read);
            read_cb (source, condition, greeter);
            return TRUE;
        }
    }
  
    offset = 0;
    id = read_int (greeter, &offset);
    read_int (greeter, &offset);
    switch (id)
    {
    case GREETER_MESSAGE_CONNECT:
        version = read_string (greeter, &offset);
        handle_connect (greeter, version);
        g_free (version);
        break;
    case GREETER_MESSAGE_AUTHENTICATE:
        sequence_number = read_int (greeter, &offset);
        username = read_string (greeter, &offset);
        handle_login (greeter, sequence_number, username);
        g_free (username);
        break;
    case GREETER_MESSAGE_AUTHENTICATE_AS_GUEST:
        sequence_number = read_int (greeter, &offset);
        handle_login_as_guest (greeter, sequence_number);
        break;
    case GREETER_MESSAGE_CONTINUE_AUTHENTICATION:
        n_secrets = read_int (greeter, &offset);
        secrets = g_malloc (sizeof (gchar *) * (n_secrets + 1));
        for (i = 0; i < n_secrets; i++)
            secrets[i] = read_string (greeter, &offset);
        secrets[i] = NULL;
        handle_continue_authentication (greeter, secrets);
        g_strfreev (secrets);
        break;
    case GREETER_MESSAGE_CANCEL_AUTHENTICATION:
        handle_cancel_authentication (greeter);
        break;
    case GREETER_MESSAGE_START_SESSION:
        session_name = read_string (greeter, &offset);
        handle_start_session (greeter, session_name);
        g_free (session_name);
        break;
    case GREETER_MESSAGE_SET_LANGUAGE:
        language = read_string (greeter, &offset);
        handle_set_language (greeter, language);
        g_free (language);
        break;
    default:
        g_warning ("Unknown message from greeter: %d", id);
        break;
    }

    greeter->priv->n_read = 0;

    return TRUE;
}

gboolean
greeter_start (Greeter *greeter)
{
    int to_greeter_pipe[2], from_greeter_pipe[2];
    gint fd;
    gchar *value;

    if (pipe (to_greeter_pipe) != 0 || 
        pipe (from_greeter_pipe) != 0)
    {
        g_warning ("Failed to create pipes: %s", strerror (errno));            
        return FALSE;
    }

    greeter->priv->to_greeter_channel = g_io_channel_unix_new (to_greeter_pipe[1]);
    g_io_channel_set_encoding (greeter->priv->to_greeter_channel, NULL, NULL);
    greeter->priv->from_greeter_channel = g_io_channel_unix_new (from_greeter_pipe[0]);
    g_io_channel_set_encoding (greeter->priv->from_greeter_channel, NULL, NULL);
    g_io_channel_set_buffered (greeter->priv->from_greeter_channel, FALSE);
    g_io_add_watch (greeter->priv->from_greeter_channel, G_IO_IN | G_IO_HUP, read_cb, greeter);

    fd = from_greeter_pipe[1];
    value = g_strdup_printf ("%d", fd);
    session_set_env (greeter->priv->session, "LIGHTDM_TO_SERVER_FD", value);
    g_free (value);

    fd = to_greeter_pipe[0];
    value = g_strdup_printf ("%d", fd);
    session_set_env (greeter->priv->session, "LIGHTDM_FROM_SERVER_FD", value);
    g_free (value);

    return TRUE;
}

gboolean
greeter_get_guest_authenticated (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->guest_account_authenticated;
}

PAMSession *
greeter_get_authentication (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->authentication;
}

void
greeter_quit (Greeter *greeter)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (greeter != NULL);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SESSION_RESULT, int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, 0, &offset);
    write_message (greeter, message, offset);
}

static PAMSession *
greeter_real_start_authentication (Greeter *greeter, const gchar *username)
{
    return NULL;
}

static gboolean
greeter_real_start_session (Greeter *greeter, const gchar *session, gboolean is_guest)
{
    return FALSE;
}

static void
greeter_init (Greeter *greeter)
{
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, GREETER_TYPE, GreeterPrivate);
    greeter->priv->read_buffer = g_malloc (HEADER_SIZE);
    greeter->priv->hints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
greeter_finalize (GObject *object)
{
    Greeter *self;

    self = GREETER (object);

    g_object_unref (self->priv->session);
    g_free (self->priv->read_buffer);
    g_hash_table_unref (self->priv->hints);
    if (self->priv->authentication)
    {
        g_signal_handlers_disconnect_matched (self->priv->authentication, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        pam_session_cancel (self->priv->authentication);
        g_object_unref (self->priv->authentication);
    }
    if (self->priv->to_greeter_channel)
        g_io_channel_unref (self->priv->to_greeter_channel);
    if (self->priv->from_greeter_channel)
        g_io_channel_unref (self->priv->from_greeter_channel);

    G_OBJECT_CLASS (greeter_parent_class)->finalize (object);
}

static void
greeter_class_init (GreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start_authentication = greeter_real_start_authentication;
    klass->start_session = greeter_real_start_session;
    object_class->finalize = greeter_finalize;

    signals[CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, connected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[START_AUTHENTICATION] =
        g_signal_new ("start-authentication",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_authentication),
                      g_signal_accumulator_first_wins,
                      NULL,
                      ldm_marshal_OBJECT__STRING,
                      PAM_SESSION_TYPE, 1, G_TYPE_STRING);

    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__STRING,
                      G_TYPE_BOOLEAN, 1, G_TYPE_STRING);

    g_type_class_add_private (klass, sizeof (GreeterPrivate));
}
