#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <xcb/xcb.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "status.h"

static gchar *session_id;

static GMainLoop *loop;

static GString *open_fds;

static GKeyFile *config;

static xcb_connection_t *connection;

static void
quit_cb (int signum)
{
    status_notify ("%s TERMINATE SIGNAL=%d", session_id, signum);
    exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *request)
{
    gchar *r;

    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }
  
    r = g_strdup_printf ("%s LOGOUT", session_id);
    if (strcmp (request, r) == 0)
        exit (EXIT_SUCCESS);
    g_free (r);
  
    r = g_strdup_printf ("%s CRASH", session_id);
    if (strcmp (request, r) == 0)
        kill (getpid (), SIGSEGV);
    g_free (r);

    r = g_strdup_printf ("%s LOCK-SEAT", session_id);
    if (strcmp (request, r) == 0)
    {
        status_notify ("%s LOCK-SEAT", session_id);
        g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                     "org.freedesktop.DisplayManager",
                                     getenv ("XDG_SEAT_PATH"),
                                     "org.freedesktop.DisplayManager.Seat",
                                     "Lock",
                                     g_variant_new ("()"),
                                     G_VARIANT_TYPE ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     1000,
                                     NULL,
                                     NULL);
    }
    g_free (r);

    r = g_strdup_printf ("%s LOCK-SESSION", session_id);
    if (strcmp (request, r) == 0)
    {
        status_notify ("%s LOCK-SESSION", session_id);
        g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                     "org.freedesktop.DisplayManager",
                                     getenv ("XDG_SESSION_PATH"),
                                     "org.freedesktop.DisplayManager.Session",
                                     "Lock",
                                     g_variant_new ("()"),
                                     G_VARIANT_TYPE ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     1000,
                                     NULL,
                                     NULL);
    }
    g_free (r);

    r = g_strdup_printf ("%s LIST-GROUPS", session_id);
    if (strcmp (request, r) == 0)
    {
        int n_groups, i;
        gid_t *groups;
        GString *group_list;

        n_groups = getgroups (0, NULL);
        groups = malloc (sizeof (gid_t) * n_groups);
        n_groups = getgroups (n_groups, groups);
        group_list = g_string_new ("");
        for (i = 0; i < n_groups; i++)
        {
            struct group *group;

            if (i != 0)
                g_string_append (group_list, ",");
            group = getgrgid (groups[i]);
            if (group)
                g_string_append (group_list, group->gr_name);
            else
                g_string_append_printf (group_list, "%d", groups[i]);
        }
        status_notify ("%s LIST-GROUPS GROUPS=%s", session_id, group_list->str);
        g_string_free (group_list, TRUE);
        free (groups);
    }

    r = g_strdup_printf ("%s READ-ENV NAME=", session_id);
    if (g_str_has_prefix (request, r))
    {
        const gchar *name = request + strlen (r);
        const gchar *value = g_getenv (name);
        status_notify ("%s READ-ENV NAME=%s VALUE=%s", session_id, name, value ? value : "");
    }
    g_free (r);

    r = g_strdup_printf ("%s WRITE-STDOUT TEXT=", session_id);
    if (g_str_has_prefix (request, r))
        g_print ("%s", request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("%s WRITE-STDERR TEXT=", session_id);
    if (g_str_has_prefix (request, r))
        g_printerr ("%s", request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("%s READ FILE=", session_id);
    if (g_str_has_prefix (request, r))
    {
        const gchar *name = request + strlen (r);
        gchar *contents;
        GError *error = NULL;

        if (g_file_get_contents (name, &contents, NULL, &error))
            status_notify ("%s READ FILE=%s TEXT=%s", session_id, name, contents);
        else
            status_notify ("%s READ FILE=%s ERROR=%s", session_id, name, error->message);
        g_clear_error (&error);
    }
    g_free (r);

    r = g_strdup_printf ("%s LIST-UNKNOWN-FILE-DESCRIPTORS", session_id);
    if (strcmp (request, r) == 0)
        status_notify ("%s LIST-UNKNOWN-FILE-DESCRIPTORS FDS=%s", session_id, open_fds->str);
    g_free (r);
}

int
main (int argc, char **argv)
{
    gchar *display;
    int fd, open_max;

    display = getenv ("DISPLAY");
    if (display == NULL)
        session_id = g_strdup ("SESSION-?");
    else if (display[0] == ':')
        session_id = g_strdup_printf ("SESSION-X-%s", display + 1);
    else
        session_id = g_strdup_printf ("SESSION-X-%s", display);

    open_fds = g_string_new ("");
    open_max = sysconf (_SC_OPEN_MAX);
    for (fd = STDERR_FILENO + 1; fd < open_max; fd++)
    {
        if (fcntl (fd, F_GETFD) >= 0)
            g_string_append_printf (open_fds, "%d,", fd);
    }
    if (g_str_has_suffix (open_fds->str, ","))
        open_fds->str[strlen (open_fds->str) - 1] = '\0';

    signal (SIGINT, quit_cb);
    signal (SIGTERM, quit_cb);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    if (argc > 1)
        status_notify ("%s START NAME=%s USER=%s", session_id, argv[1], getenv ("USER"));
    else
        status_notify ("%s START USER=%s", session_id, getenv ("USER"));

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        status_notify ("%s CONNECT-XSERVER-ERROR", session_id);
        return EXIT_FAILURE;
    }

    status_notify ("%s CONNECT-XSERVER", session_id);

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
