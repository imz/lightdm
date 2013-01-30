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

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <glib-object.h>

#include "display-server.h"
#include "session.h"
#include "accounts.h"

G_BEGIN_DECLS

#define DISPLAY_TYPE           (display_get_type())
#define DISPLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_TYPE, Display))
#define DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DISPLAY_TYPE, DisplayClass))
#define DISPLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DISPLAY_TYPE, DisplayClass))

typedef struct DisplayPrivate DisplayPrivate;

typedef struct
{
    GObject         parent_instance;
    DisplayPrivate *priv;
} Display;

typedef struct
{
    GObjectClass parent_class;

    gboolean (*display_server_ready)(Display *display);
    gboolean (*start_greeter)(Display *display);
    gboolean (*start_session)(Display *display);
    Session *(*create_session) (Display *display);
    void (*ready)(Display *display);
    gboolean (*switch_to_user)(Display *display, User *user);
    gboolean (*switch_to_guest)(Display *display);
    gchar *(*get_guest_username)(Display *display);
    void (*stopped)(Display *display);
} DisplayClass;

GType display_get_type (void);

Display *display_new (DisplayServer *display_server);

DisplayServer *display_get_display_server (Display *display);

const gchar *display_get_username (Display *display);

Session *display_get_session (Display *display);

void display_set_greeter_session (Display *display, const gchar *greeter_session);

void display_set_session_wrapper (Display *display, const gchar *session_wrapper);

void display_set_allow_guest (Display *display, gboolean allow_guest);

void display_set_greeter_allow_guest (Display *display, gboolean greeter_allow_guest);

void display_set_autologin_user (Display *display, const gchar *username, gboolean is_guest, gint timeout);

void display_set_select_user_hint (Display *display, const gchar *username, gboolean is_guest);

void display_set_hide_users_hint (Display *display, gboolean hide_users);

void display_set_show_manual_login_hint (Display *display, gboolean show_manual);

void display_set_show_remote_login_hint (Display *display, gboolean show_remote);

void display_set_lock_hint (Display *display, gboolean is_lock);

void display_set_user_session (Display *display, SessionType type, const gchar *session_name);

gboolean display_start (Display *display);

gboolean display_get_is_ready (Display *display);

void display_lock (Display *display);

void display_unlock (Display *display);

void display_stop (Display *display);

gboolean display_get_is_stopped (Display *display);

G_END_DECLS

#endif /* _DISPLAY_H_ */
