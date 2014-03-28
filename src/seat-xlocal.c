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

#include <string.h>

#include "seat-xlocal.h"
#include "configuration.h"
#include "x-server-local.h"
#include "plymouth.h"
#include "vt.h"

G_DEFINE_TYPE (SeatXLocal, seat_xlocal, SEAT_TYPE);

static gboolean
seat_xlocal_get_start_local_sessions (Seat *seat)
{
    return !seat_get_string_property (seat, "xdmcp-manager");
}

static void
seat_xlocal_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    seat_set_share_display_server (seat, seat_get_boolean_property (seat, "xserver-share"));
    SEAT_CLASS (seat_xlocal_parent_class)->setup (seat);
}

static gboolean
seat_xlocal_start (Seat *seat)
{
   
    return SEAT_CLASS (seat_xlocal_parent_class)->start (seat);
}

static void
x_server_ready_cb (XServerLocal *x_server, Seat *seat)
{
    /* Quit Plymouth */
    plymouth_quit (TRUE);
}

static void
x_server_transition_plymouth_cb (XServerLocal *x_server, Seat *seat)
{
    /* Quit Plymouth if we didn't do the transition */
    if (plymouth_get_is_running ())
        plymouth_quit (FALSE);

    g_signal_handlers_disconnect_matched (x_server, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, x_server_transition_plymouth_cb, NULL);
}

static DisplayServer *
seat_xlocal_create_display_server (Seat *seat, const gchar *session_type)
{  
    if (strcmp (session_type, "x") != 0)
        return NULL;

    XServerLocal *x_server;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL, *xdg_seat = NULL;
    gboolean allow_tcp;
    gint vt = -1, port = 0;

    if (vt > 0)
        l_debug (seat, "Starting local X display on VT %d", vt);
    else
        l_debug (seat, "Starting local X display");
  
    x_server = x_server_local_new ();

    /* If running inside an X server use Xephyr instead */
    if (g_getenv ("DISPLAY"))
        command = "Xephyr";
    if (!command)
        command = seat_get_string_property (seat, "xserver-command");
    if (command)
        x_server_local_set_command (x_server, command);

    /* If Plymouth is running, stop it */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            vt = active_vt;
            g_signal_connect (x_server, "ready", G_CALLBACK (x_server_ready_cb), seat);
            g_signal_connect (x_server, "stopped", G_CALLBACK (x_server_transition_plymouth_cb), seat);
            plymouth_deactivate ();
        }
        else
            l_debug (seat, "Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (plymouth_get_is_active ())
        plymouth_quit (FALSE);
    if (vt < 0)
        vt = vt_get_unused ();
    if (vt >= 0)
        x_server_local_set_vt (x_server, vt);

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);
        
    xdg_seat = seat_get_string_property (seat, "xdg-seat");
    if (xdg_seat)
        x_server_local_set_xdg_seat (x_server, xdg_seat);

    config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        x_server_local_set_config (x_server, config_file);
  
    allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    x_server_local_set_allow_tcp (x_server, allow_tcp);    

    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
        x_server_local_set_xdmcp_server (x_server, xdmcp_manager);

    port = seat_get_integer_property (seat, "xdmcp-port");
    if (port > 0)
        x_server_local_set_xdmcp_port (x_server, port);

    key_name = seat_get_string_property (seat, "xdmcp-key");
    if (key_name)
    {
        gchar *dir, *path;
        GKeyFile *keys;
        gboolean result;
        GError *error = NULL;

        dir = config_get_string (config_get_instance (), "LightDM", "config-directory");
        path = g_build_filename (dir, "keys.conf", NULL);
        g_free (dir);

        keys = g_key_file_new ();
        result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
        if (error)
            l_debug (seat, "Error getting key %s", error->message);
        g_clear_error (&error);      

        if (result)
        {
            gchar *key = NULL;

            if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                key = g_key_file_get_string (keys, "keyring", key_name, NULL);
            else
                l_debug (seat, "Key %s not defined", key_name);

            if (key)
                x_server_local_set_xdmcp_key (x_server, key);
            g_free (key);
        }

        g_free (path);
        g_key_file_free (keys);
    }

    return DISPLAY_SERVER (x_server);
}

static Greeter *
seat_xlocal_create_greeter_session (Seat *seat)
{
    Greeter *greeter_session;
    const gchar *xdg_seat;

    greeter_session = SEAT_CLASS (seat_xlocal_parent_class)->create_greeter_session (seat);
    xdg_seat = seat_get_string_property (seat, "xdg-seat");
    if (!xdg_seat)
        xdg_seat = "seat0";
    l_debug (seat, "Setting XDG_SEAT=%s", xdg_seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", xdg_seat);

    return greeter_session;
}

static Session *
seat_xlocal_create_session (Seat *seat)
{
    Session *session;
    const gchar *xdg_seat;

    session = SEAT_CLASS (seat_xlocal_parent_class)->create_session (seat);
    xdg_seat = seat_get_string_property (seat, "xdg-seat");
    if (!xdg_seat)
        xdg_seat = "seat0";
    l_debug (seat, "Setting XDG_SEAT=%s", xdg_seat);
    session_set_env (SESSION (session), "XDG_SEAT", xdg_seat);

    return session;
}

static void
seat_xlocal_set_active_session (Seat *seat, Session *session)
{
    gint vt = display_server_get_vt (session_get_display_server (session));
    if (vt >= 0)
        vt_set_active (vt);

    SEAT_CLASS (seat_xlocal_parent_class)->set_active_session (seat, session);
}

static Session *
seat_xlocal_get_active_session (Seat *seat)
{
    gint vt;
    GList *link;

    vt = vt_get_active ();
    if (vt < 0)
        return NULL;

    for (link = seat_get_sessions (seat); link; link = link->next)
    {
        Session *session = link->data;
        DisplayServer *display_server;

        display_server = session_get_display_server (session);
        if (display_server && display_server_get_vt (display_server) == vt)
            return session;
    }

    return NULL;
}

static void
seat_xlocal_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    const gchar *path;
    XServerLocal *x_server;

    x_server = X_SERVER_LOCAL (display_server);
    path = x_server_local_get_authority_file_path (x_server);
    process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
    process_set_env (script, "XAUTHORITY", path);

    SEAT_CLASS (seat_xlocal_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xlocal_init (SeatXLocal *seat)
{
}

static void
seat_xlocal_class_init (SeatXLocalClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->get_start_local_sessions = seat_xlocal_get_start_local_sessions;
    seat_class->setup = seat_xlocal_setup;
    seat_class->start = seat_xlocal_start;
    seat_class->create_display_server = seat_xlocal_create_display_server;
    seat_class->create_greeter_session = seat_xlocal_create_greeter_session;
    seat_class->create_session = seat_xlocal_create_session;
    seat_class->set_active_session = seat_xlocal_set_active_session;
    seat_class->get_active_session = seat_xlocal_get_active_session;
    seat_class->run_script = seat_xlocal_run_script;
}
