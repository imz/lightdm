/* -*- Mode: C; indent-tabs-mode:nil; tab-width:4 -*-
 *
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <gio/gio.h>

#include "lightdm/user.h"

enum
{
    LIST_PROP_0,
    LIST_PROP_NUM_USERS,
    LIST_PROP_USERS,
};

enum
{
    USER_PROP_0,
    USER_PROP_NAME,
    USER_PROP_REAL_NAME,
    USER_PROP_DISPLAY_NAME,
    USER_PROP_HOME_DIRECTORY,
    USER_PROP_IMAGE,
    USER_PROP_BACKGROUND,
    USER_PROP_LANGUAGE,
    USER_PROP_LAYOUT,
    USER_PROP_LAYOUTS,
    USER_PROP_SESSION,
    USER_PROP_LOGGED_IN,
    USER_PROP_HAS_MESSAGES
};

enum
{
    USER_ADDED,
    USER_CHANGED,
    USER_REMOVED,
    LAST_LIST_SIGNAL
};
static guint list_signals[LAST_LIST_SIGNAL] = { 0 };

enum
{
    CHANGED,
    LAST_USER_SIGNAL
};
static guint user_signals[LAST_USER_SIGNAL] = { 0 };

typedef struct
{
    /* Bus connection being communicated on */
    GDBusConnection *bus;

    /* D-Bus signals for accounts service events */
    guint user_added_signal;
    guint user_removed_signal;

    /* D-Bus signals for display manager events */
    guint session_added_signal;
    guint session_removed_signal;

    /* File monitor for password file */
    GFileMonitor *passwd_monitor;

    /* TRUE if have scanned users */
    gboolean have_users;

    /* List of users */
    GList *users;

    /* List of sessions */
    GList *sessions;
} LightDMUserListPrivate;

typedef struct
{
    /* User list this user is part of */
    LightDMUserList *user_list;

    /* TRUE if have loaded user properties */
    gboolean loaded_values;

    /* Accounts service path */
    gchar *path;

    /* DMRC file */
    GKeyFile *dmrc_file;

    /* Update signal from accounts service */
    guint changed_signal;

    /* Username */
    gchar *name;

    /* Descriptive name for user */
    gchar *real_name;

    /* Home directory of user */
    gchar *home_directory;

    /* Image for user */
    gchar *image;

    /* Background image for users */
    gchar *background;

    /* TRUE if this user has messages available */
    gboolean has_messages;

    /* User chosen language */
    gchar *language;

    /* User layout preferences */
    gchar **layouts;

    /* User default session */
    gchar *session;
} LightDMUserPrivate;

typedef struct
{
    GObject parent_instance;
    gchar *path;
    gchar *username;
} Session;

typedef struct
{
    GObjectClass parent_class;
} SessionClass;

G_DEFINE_TYPE (LightDMUserList, lightdm_user_list, G_TYPE_OBJECT);
G_DEFINE_TYPE (LightDMUser, lightdm_user, G_TYPE_OBJECT);
#define SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), session_get_type (), Session))
GType session_get_type (void);
G_DEFINE_TYPE (Session, session, G_TYPE_OBJECT);

#define GET_LIST_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER_LIST, LightDMUserListPrivate)
#define GET_USER_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER, LightDMUserPrivate)

#define PASSWD_FILE      "/etc/passwd"
#define USER_CONFIG_FILE "/etc/lightdm/users.conf"

static LightDMUserList *singleton = NULL;

/**
 * lightdm_user_list_get_instance:
 *
 * Get the user list.
 *
 * Return value: (transfer none): the #LightDMUserList
 **/
LightDMUserList *
lightdm_user_list_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (LIGHTDM_TYPE_USER_LIST, NULL);
    return singleton;
}

static LightDMUser *
get_user_by_name (LightDMUserList *user_list, const gchar *username)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *link;
  
    for (link = priv->users; link; link = link->next)
    {
        LightDMUser *user = link->data;
        if (g_strcmp0 (lightdm_user_get_name (user), username) == 0)
            return user;
    }

    return NULL;
}

static LightDMUser *
get_user_by_path (LightDMUserList *user_list, const gchar *path)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *link;
  
    for (link = priv->users; link; link = link->next)
    {
        LightDMUser *user = link->data;
        if (g_strcmp0 (GET_USER_PRIVATE (user)->path, path) == 0)
            return user;
    }

    return NULL;
}
  
static gint
compare_user (gconstpointer a, gconstpointer b)
{
    LightDMUser *user_a = (LightDMUser *) a, *user_b = (LightDMUser *) b;
    return g_strcmp0 (lightdm_user_get_display_name (user_a), lightdm_user_get_display_name (user_b));
}

static gboolean
update_passwd_user (LightDMUser *user, const gchar *real_name, const gchar *home_directory, const gchar *image)
{
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);

    /* Skip if already set to this */
    if (g_strcmp0 (lightdm_user_get_real_name (user), real_name) == 0 &&
        g_strcmp0 (lightdm_user_get_home_directory (user), home_directory) == 0 &&
        g_strcmp0 (lightdm_user_get_image (user), image) == 0)
        return FALSE;

    g_free (priv->real_name);
    priv->real_name = g_strdup (real_name);
    g_free (priv->home_directory);
    priv->home_directory = g_strdup (home_directory);
    g_free (priv->image);
    priv->image = g_strdup (image);

    return TRUE;
}

static void
user_changed_cb (LightDMUser *user)
{
    g_signal_emit (GET_USER_PRIVATE (user)->user_list, list_signals[USER_CHANGED], 0, user);
}

static void
load_passwd_file (LightDMUserList *user_list, gboolean emit_add_signal)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GKeyFile *config;
    gchar *value;
    gint minimum_uid;
    gchar **hidden_users, **hidden_shells;
    GList *users = NULL, *old_users, *new_users = NULL, *changed_users = NULL, *link;
    GError *error = NULL;

    g_debug ("Loading user config from %s", USER_CONFIG_FILE);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, USER_CONFIG_FILE, G_KEY_FILE_NONE, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s", USER_CONFIG_FILE, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    if (g_key_file_has_key (config, "UserList", "minimum-uid", NULL))
        minimum_uid = g_key_file_get_integer (config, "UserList", "minimum-uid", NULL);
    else
        minimum_uid = 500;

    value = g_key_file_get_string (config, "UserList", "hidden-users", NULL);
    if (!value)
        value = g_strdup ("nobody nobody4 noaccess");
    hidden_users = g_strsplit (value, " ", -1);
    g_free (value);

    value = g_key_file_get_string (config, "UserList", "hidden-shells", NULL);
    if (!value)
        value = g_strdup ("/bin/false /usr/sbin/nologin");
    hidden_shells = g_strsplit (value, " ", -1);
    g_free (value);

    g_key_file_free (config);

    setpwent ();

    while (TRUE)
    {
        struct passwd *entry;
        LightDMUser *user;
        LightDMUserPrivate *user_priv;
        char **tokens;
        gchar *real_name, *image;
        int i;

        errno = 0;
        entry = getpwent ();
        if (!entry)
            break;

        /* Ignore system users */
        if (entry->pw_uid < minimum_uid)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            for (i = 0; hidden_shells[i] && strcmp (entry->pw_shell, hidden_shells[i]) != 0; i++);
            if (hidden_shells[i])
                continue;
        }

        /* Ignore certain users */
        for (i = 0; hidden_users[i] && strcmp (entry->pw_name, hidden_users[i]) != 0; i++);
        if (hidden_users[i])
            continue;

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            real_name = g_strdup (tokens[0]);
        else
            real_name = g_strdup ("");
        g_strfreev (tokens);

        image = g_build_filename (entry->pw_dir, ".face", NULL);
        if (!g_file_test (image, G_FILE_TEST_EXISTS))
        {
            g_free (image);
            image = g_build_filename (entry->pw_dir, ".face.icon", NULL);
            if (!g_file_test (image, G_FILE_TEST_EXISTS))
            {
                g_free (image);
                image = NULL;
            }
        }

        user = g_object_new (LIGHTDM_TYPE_USER, NULL);
        user_priv = GET_USER_PRIVATE (user);
        user_priv->user_list = user_list;
        g_free (user_priv->name);
        user_priv->name = g_strdup (entry->pw_name);
        g_free (user_priv->real_name);
        user_priv->real_name = real_name;
        g_free (user_priv->home_directory);
        user_priv->home_directory = g_strdup (entry->pw_dir);
        g_free (user_priv->image);
        user_priv->image = image;

        /* Update existing users if have them */
        for (link = priv->users; link; link = link->next)
        {
            LightDMUser *info = link->data;
            if (strcmp (lightdm_user_get_name (info), lightdm_user_get_name (user)) == 0)
            {
                if (update_passwd_user (info, lightdm_user_get_real_name (user), lightdm_user_get_home_directory (user), lightdm_user_get_image (user)))
                    changed_users = g_list_insert_sorted (changed_users, info, compare_user);
                g_object_unref (user);
                user = info;
                break;
            }
        }
        if (!link)
        {
            /* Only notify once we have loaded the user list */
            if (priv->have_users)
                new_users = g_list_insert_sorted (new_users, user, compare_user);
        }
        users = g_list_insert_sorted (users, user, compare_user);
    }
    g_strfreev (hidden_users);
    g_strfreev (hidden_shells);

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();

    /* Use new user list */
    old_users = priv->users;
    priv->users = users;
  
    /* Notify of changes */
    for (link = new_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s added", lightdm_user_get_name (info));
        g_signal_connect (info, "changed", G_CALLBACK (user_changed_cb), NULL);
        if (emit_add_signal)
            g_signal_emit (user_list, list_signals[USER_ADDED], 0, info);
    }
    g_list_free (new_users);
    for (link = changed_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s changed", lightdm_user_get_name (info));
        g_signal_emit (info, user_signals[CHANGED], 0);
    }
    g_list_free (changed_users);
    for (link = old_users; link; link = link->next)
    {
        GList *new_link;

        /* See if this user is in the current list */
        for (new_link = priv->users; new_link; new_link = new_link->next)
        {
            if (new_link->data == link->data)
                break;
        }

        if (!new_link)
        {
            LightDMUser *info = link->data;
            g_debug ("User %s removed", lightdm_user_get_name (info));
            g_signal_emit (user_list, list_signals[USER_REMOVED], 0, info);
            g_object_unref (info);
        }
    }
    g_list_free (old_users);
}

static void
passwd_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, LightDMUserList *user_list)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        g_debug ("%s changed, reloading user list", g_file_get_path (file));
        load_passwd_file (user_list, TRUE);
    }
}

static gboolean load_accounts_user (LightDMUser *user);

static void
accounts_user_changed_cb (GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer data)
{
    LightDMUser *user = data;
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);  

    g_debug ("User %s changed", priv->path);
    if (load_accounts_user (user))
        g_signal_emit (user, user_signals[CHANGED], 0);
}

static gboolean
load_accounts_user (LightDMUser *user)
{
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);
    GVariant *result, *value;
    GVariantIter *iter;
    gchar *name;
    gboolean system_account = FALSE;
    GError *error = NULL;

    /* Get the properties for this user */
    if (!priv->changed_signal)
        priv->changed_signal = g_dbus_connection_signal_subscribe (GET_LIST_PRIVATE (priv->user_list)->bus,
                                                                   "org.freedesktop.Accounts",
                                                                   "org.freedesktop.Accounts.User",
                                                                   "Changed",
                                                                   priv->path,
                                                                   NULL,
                                                                   G_DBUS_SIGNAL_FLAGS_NONE,
                                                                   accounts_user_changed_cb,
                                                                   user,
                                                                   NULL);
    result = g_dbus_connection_call_sync (GET_LIST_PRIVATE (priv->user_list)->bus,
                                          "org.freedesktop.Accounts",
                                          priv->path,
                                          "org.freedesktop.DBus.Properties",
                                          "GetAll",
                                          g_variant_new ("(s)", "org.freedesktop.Accounts.User"),
                                          G_VARIANT_TYPE ("(a{sv})"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
        g_warning ("Error updating user %s: %s", priv->path, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    /* Store the properties we need */
    g_variant_get (result, "(a{sv})", &iter);
    while (g_variant_iter_loop (iter, "{&sv}", &name, &value))
    {
        if (strcmp (name, "UserName") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->name);
            priv->name = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "RealName") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->real_name);
            priv->real_name = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "HomeDirectory") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->home_directory);
            priv->home_directory = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "SystemAccount") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            system_account = g_variant_get_boolean (value);
        else if (strcmp (name, "Language") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            if (priv->language)
                g_free (priv->language);
            priv->language = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "IconFile") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->image);
            priv->image = g_variant_dup_string (value, NULL);
            if (strcmp (priv->image, "") == 0)
            {
                g_free (priv->image);
                priv->image = NULL;
            }
        }
        else if (strcmp (name, "XSession") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->session);
            priv->session = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "BackgroundFile") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->background);
            priv->background = g_variant_dup_string (value, NULL);
            if (strcmp (priv->background, "") == 0)
            {
                g_free (priv->background);
                priv->background = NULL;
            }
        }
        else if (strcmp (name, "XKeyboardLayouts") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING_ARRAY))
        {
            g_strfreev (priv->layouts);
            priv->layouts = g_variant_dup_strv (value, NULL);
            if (!priv->layouts)
            {
                priv->layouts = g_malloc (sizeof (gchar *) * 1);
                priv->layouts[0] = NULL;
            }
        }
        else if (strcmp (name, "XHasMessages") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            priv->has_messages = g_variant_get_boolean (value);
    }
    g_variant_iter_free (iter);

    g_variant_unref (result);

    priv->loaded_values = TRUE;

    return !system_account;
}

static void
add_accounts_user (LightDMUserList *user_list, const gchar *path, gboolean emit_signal)
{
    LightDMUserListPrivate *list_priv = GET_LIST_PRIVATE (user_list);
    LightDMUser *user;
    LightDMUserPrivate *priv;

    user = g_object_new (LIGHTDM_TYPE_USER, NULL);
    priv = GET_USER_PRIVATE (user);

    g_debug ("User %s added", path);
    priv->user_list = user_list;
    priv->path = g_strdup (path);
    g_signal_connect (user, "changed", G_CALLBACK (user_changed_cb), NULL);
    if (load_accounts_user (user))
    {
        list_priv->users = g_list_insert_sorted (list_priv->users, user, compare_user);
        if (emit_signal)      
            g_signal_emit (user_list, list_signals[USER_ADDED], 0, user);
    }
    else
        g_object_unref (user);
}

static void
accounts_user_added_cb (GDBusConnection *connection,
                        const gchar *sender_name,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *signal_name,
                        GVariant *parameters,
                        gpointer data)
{
    LightDMUserList *user_list = data;
    gchar *path;
    LightDMUser *user;
  
    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got UserAccounts signal UserAdded with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    g_variant_get (parameters, "(&o)", &path);

    /* Add user if we haven't got them */
    user = get_user_by_path (user_list, path);
    if (!user)
        add_accounts_user (user_list, path, TRUE);
}

static void
accounts_user_deleted_cb (GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer data)
{
    LightDMUserList *user_list = data;
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    gchar *path;
    LightDMUser *user;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got UserAccounts signal UserDeleted with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    g_variant_get (parameters, "(&o)", &path);

    /* Delete user if we know of them */
    user = get_user_by_path (user_list, path);
    if (user)
    {
        g_debug ("User %s deleted", path);
        priv->users = g_list_remove (priv->users, user);

        g_signal_emit (user_list, list_signals[USER_REMOVED], 0, user);

        g_object_unref (user);
    }
}

static Session *
load_session (LightDMUserList *user_list, const gchar *path)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    Session *session = NULL;
    GVariant *result, *username;
    GError *error = NULL;

    result = g_dbus_connection_call_sync (priv->bus,
                                          "org.freedesktop.DisplayManager",
                                          path,
                                          "org.freedesktop.DBus.Properties",
                                          "Get",
                                          g_variant_new ("(ss)", "org.freedesktop.DisplayManager.Session", "UserName"),
                                          G_VARIANT_TYPE ("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
        g_warning ("Error getting UserName from org.freedesktop.DisplayManager.Session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    g_variant_get (result, "(v)", &username);
    if (g_variant_is_of_type (username, G_VARIANT_TYPE_STRING))
    {
        gchar *name;

        g_variant_get (username, "&s", &name);

        g_debug ("Loaded session %s (%s)", path, name);
        session = g_object_new (session_get_type (), NULL);
        session->username = g_strdup (name);
        session->path = g_strdup (path);
        priv->sessions = g_list_append (priv->sessions, session);
    }
    g_variant_unref (username);
    g_variant_unref (result);

    return session;
}

static void
session_added_cb (GDBusConnection *connection,
                  const gchar *sender_name,
                  const gchar *object_path,
                  const gchar *interface_name,
                  const gchar *signal_name,
                  GVariant *parameters,
                  gpointer data)
{
    LightDMUserList *user_list = data;
    gchar *path;
    Session *session;
    LightDMUser *user = NULL;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got DisplayManager signal SessionAdded with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    g_variant_get (parameters, "(&o)", &path);
    session = load_session (user_list, path);
    if (session)
        user = get_user_by_name (user_list, session->username);
    if (user)
        g_signal_emit (user, user_signals[CHANGED], 0);
}

static void
session_removed_cb (GDBusConnection *connection,
                    const gchar *sender_name,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *signal_name,
                    GVariant *parameters,
                    gpointer data)
{
    LightDMUserList *user_list = data;
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    gchar *path;
    GList *link;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got DisplayManager signal SessionRemoved with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    g_variant_get (parameters, "(&o)", &path);

    for (link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (strcmp (session->path, path) == 0)
        {
            LightDMUser *user;

            g_debug ("Session %s removed", path);
            priv->sessions = g_list_remove_link (priv->sessions, link);
            user = get_user_by_name (user_list, session->username);
            if (user)
                g_signal_emit (user, user_signals[CHANGED], 0);
            g_object_unref (session);
            break;
        }
    }
}

static void
load_users (LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GVariant *result;
    GError *error = NULL;

    if (priv->have_users)
        return;
    priv->have_users = TRUE;

    /* Get user list from accounts service and fall back to /etc/passwd if that fails */
    priv->user_added_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                  "org.freedesktop.Accounts",
                                                                  "org.freedesktop.Accounts",
                                                                  "UserAdded",
                                                                  "/org/freedesktop/Accounts",
                                                                  NULL,
                                                                  G_DBUS_SIGNAL_FLAGS_NONE,
                                                                  accounts_user_added_cb,
                                                                  user_list,
                                                                  NULL);
    priv->user_removed_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                    "org.freedesktop.Accounts",
                                                                    "org.freedesktop.Accounts",
                                                                    "UserDeleted",
                                                                    "/org/freedesktop/Accounts",
                                                                    NULL,
                                                                    G_DBUS_SIGNAL_FLAGS_NONE,
                                                                    accounts_user_deleted_cb,
                                                                    user_list,
                                                                    NULL);
    result = g_dbus_connection_call_sync (priv->bus,
                                          "org.freedesktop.Accounts",
                                          "/org/freedesktop/Accounts",
                                          "org.freedesktop.Accounts",
                                          "ListCachedUsers",
                                          g_variant_new ("()"),
                                          G_VARIANT_TYPE ("(ao)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
        g_warning ("Error getting user list from org.freedesktop.Accounts: %s", error->message);
    g_clear_error (&error);
    if (result)
    {
        GVariantIter *iter;
        const gchar *path;

        g_debug ("Loading users from org.freedesktop.Accounts");
        g_variant_get (result, "(ao)", &iter);
        while (g_variant_iter_loop (iter, "&o", &path))
            add_accounts_user (user_list, path, FALSE);
        g_variant_iter_free (iter);
        g_variant_unref (result);
    }
    else
    {
        GFile *passwd_file;

        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_added_signal);
        priv->user_added_signal = 0;
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_removed_signal);
        priv->user_removed_signal = 0;

        load_passwd_file (user_list, FALSE);

        /* Watch for changes to user list */

        passwd_file = g_file_new_for_path (PASSWD_FILE);
        priv->passwd_monitor = g_file_monitor (passwd_file, G_FILE_MONITOR_NONE, NULL, &error);
        g_object_unref (passwd_file);
        if (error)
            g_warning ("Error monitoring %s: %s", PASSWD_FILE, error->message);
        else
            g_signal_connect (priv->passwd_monitor, "changed", G_CALLBACK (passwd_changed_cb), user_list);
        g_clear_error (&error);
    }

    priv->session_added_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                     "org.freedesktop.DisplayManager",
                                                                     "org.freedesktop.DisplayManager",
                                                                     "SessionAdded",
                                                                     "/org/freedesktop/DisplayManager",
                                                                     NULL,
                                                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                                                     session_added_cb,
                                                                     user_list,
                                                                     NULL);
    priv->session_removed_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                       "org.freedesktop.DisplayManager",
                                                                       "org.freedesktop.DisplayManager",
                                                                       "SessionRemoved",
                                                                       "/org/freedesktop/DisplayManager",
                                                                       NULL,
                                                                       G_DBUS_SIGNAL_FLAGS_NONE,
                                                                       session_removed_cb,

                                                                    user_list,
                                                                    NULL);
    result = g_dbus_connection_call_sync (priv->bus,
                                          "org.freedesktop.DisplayManager",
                                          "/org/freedesktop/DisplayManager",
                                          "org.freedesktop.DBus.Properties",
                                          "Get",
                                          g_variant_new ("(ss)", "org.freedesktop.DisplayManager", "Sessions"),
                                          G_VARIANT_TYPE ("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
        g_warning ("Error getting session list from org.freedesktop.DisplayManager: %s", error->message);
    g_clear_error (&error);
    if (result)
    {
        if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(v)")))
        {
            GVariant *value;
            GVariantIter *iter;
            const gchar *path;

            g_variant_get (result, "(v)", &value);

            g_debug ("Loading sessions from org.freedesktop.DisplayManager");
            g_variant_get (value, "ao", &iter);
            while (g_variant_iter_loop (iter, "&o", &path))
                load_session (user_list, path);
            g_variant_iter_free (iter);

            g_variant_unref (value);
        }
        else
            g_warning ("Unexpected type from org.freedesktop.DisplayManager.Sessions: %s", g_variant_get_type_string (result));

        g_variant_unref (result);
    }
}

/**
 * lightdm_user_list_get_length:
 * @user_list: a #LightDMUserList
 *
 * Return value: The number of users able to log in
 **/
gint
lightdm_user_list_get_length (LightDMUserList *user_list)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), 0);
    load_users (user_list);
    return g_list_length (GET_LIST_PRIVATE (user_list)->users);
}

/**
 * lightdm_user_list_get_users:
 * @user_list: A #LightDMUserList
 *
 * Get a list of users to present to the user.  This list may be a subset of the
 * available users and may be empty depending on the server configuration.
 *
 * Return value: (element-type LightDMUser) (transfer none): A list of #LightDMUser that should be presented to the user.
 **/
GList *
lightdm_user_list_get_users (LightDMUserList *user_list)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), NULL);
    load_users (user_list);
    return GET_LIST_PRIVATE (user_list)->users;
}

/**
 * lightdm_user_list_get_user_by_name:
 * @user_list: A #LightDMUserList
 * @username: Name of user to get.
 *
 * Get infomation about a given user or #NULL if this user doesn't exist.
 *
 * Return value: (transfer none): A #LightDMUser entry for the given user.
 **/
LightDMUser *
lightdm_user_list_get_user_by_name (LightDMUserList *user_list, const gchar *username)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), NULL);
    g_return_val_if_fail (username != NULL, NULL);

    load_users (user_list);

    return get_user_by_name (user_list, username);
}

static void
lightdm_user_list_init (LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);

    priv->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
}

static void
lightdm_user_list_set_property (GObject    *object,
                                guint       prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lightdm_user_list_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LightDMUserList *self;

    self = LIGHTDM_USER_LIST (object);

    switch (prop_id)
    {
    case LIST_PROP_NUM_USERS:
        g_value_set_int (value, lightdm_user_list_get_length (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_user_list_finalize (GObject *object)
{
    LightDMUserList *self = LIGHTDM_USER_LIST (object);
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (self);

    /* Remove children first, they might access us */
    g_list_free_full (priv->users, g_object_unref);
    g_list_free_full (priv->sessions, g_object_unref);

    if (priv->user_added_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_added_signal);
    if (priv->user_removed_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_removed_signal);
    if (priv->session_added_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->session_added_signal);
    if (priv->session_removed_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->session_removed_signal);
    g_object_unref (priv->bus);
    if (priv->passwd_monitor)
        g_object_unref (priv->passwd_monitor);

    G_OBJECT_CLASS (lightdm_user_list_parent_class)->finalize (object);
}

static void
lightdm_user_list_class_init (LightDMUserListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (LightDMUserListPrivate));

    object_class->set_property = lightdm_user_list_set_property;
    object_class->get_property = lightdm_user_list_get_property;
    object_class->finalize = lightdm_user_list_finalize;

    g_object_class_install_property (object_class,
                                     LIST_PROP_NUM_USERS,
                                     g_param_spec_int ("num-users",
                                                       "num-users",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));
    /**
     * LightDMUserList::user-added:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been added.
     *
     * The ::user-added signal gets emitted when a user account is created.
     **/
    list_signals[USER_ADDED] =
        g_signal_new ("user-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-changed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been changed.
     *
     * The ::user-changed signal gets emitted when a user account is modified.
     **/
    list_signals[USER_CHANGED] =
        g_signal_new ("user-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-removed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been removed.
     *
     * The ::user-removed signal gets emitted when a user account is removed.
     **/
    list_signals[USER_REMOVED] =
        g_signal_new ("user-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);
}

static void
load_dmrc (LightDMUser *user)
{
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);
    gchar *path;
    //gboolean have_dmrc;

    if (!priv->dmrc_file)
        priv->dmrc_file = g_key_file_new ();

    /* Load from the user directory */  
    path = g_build_filename (priv->home_directory, ".dmrc", NULL);
    /*have_dmrc = */g_key_file_load_from_file (priv->dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free (path);

    /* If no ~/.dmrc, then load from the cache */
    // FIXME

    // FIXME: Watch for changes

    /* The Language field contains the locale */
    if (priv->language)
        g_free (priv->language);
    priv->language = g_key_file_get_string (priv->dmrc_file, "Desktop", "Language", NULL);

    if (g_key_file_has_key (priv->dmrc_file, "Desktop", "Layout", NULL))
    {
        g_strfreev (priv->layouts);
        priv->layouts = g_malloc (sizeof (gchar *) * 2);
        priv->layouts[0] = g_key_file_get_string (priv->dmrc_file, "Desktop", "Layout", NULL);
        priv->layouts[1] = NULL;
    }

    if (priv->session)
        g_free (priv->session);
    priv->session = g_key_file_get_string (priv->dmrc_file, "Desktop", "Session", NULL);
}

/* Loads language/layout/session info for user */
static void
load_user_values (LightDMUser *user)
{
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);

    if (priv->loaded_values)
        return;
    priv->loaded_values = TRUE;

    if (!priv->path)
        load_dmrc (user);
}

/**
 * lightdm_user_get_name:
 * @user: A #LightDMUser
 * 
 * Get the name of a user.
 * 
 * Return value: The name of the given user
 **/
const gchar *
lightdm_user_get_name (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->name;
}

/**
 * lightdm_user_get_real_name:
 * @user: A #LightDMUser
 * 
 * Get the real name of a user.
 *
 * Return value: The real name of the given user
 **/
const gchar *
lightdm_user_get_real_name (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->real_name;
}

/**
 * lightdm_user_get_display_name:
 * @user: A #LightDMUser
 * 
 * Get the display name of a user.
 * 
 * Return value: The display name of the given user
 **/
const gchar *
lightdm_user_get_display_name (LightDMUser *user)
{
    LightDMUserPrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);

    load_user_values (user);

    priv = GET_USER_PRIVATE (user);
    if (!priv->real_name || strcmp (priv->real_name, "") == 0)
        return priv->name;
    else
        return priv->real_name;
}

/**
 * lightdm_user_get_home_directory:
 * @user: A #LightDMUser
 * 
 * Get the home directory for a user.
 * 
 * Return value: The users home directory
 */
const gchar *
lightdm_user_get_home_directory (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->home_directory;
}

/**
 * lightdm_user_get_image:
 * @user: A #LightDMUser
 * 
 * Get the image URI for a user.
 * 
 * Return value: The image URI for the given user or #NULL if no URI
 **/
const gchar *
lightdm_user_get_image (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->image;
}

/**
 * lightdm_user_get_background:
 * @user: A #LightDMUser
 * 
 * Get the background file path for a user.
 * 
 * Return value: The background file path for the given user or #NULL if no path
 **/
const gchar *
lightdm_user_get_background (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->background;
}

/**
 * lightdm_user_get_language:
 * @user: A #LightDMUser
 * 
 * Get the language for a user.
 * 
 * Return value: The language in the form of a local specification (e.g. "de_DE.UTF-8") for the given user or #NULL if using the system default locale.
 **/
const gchar *
lightdm_user_get_language (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->language;
}

/**
 * lightdm_user_get_layout:
 * @user: A #LightDMUser
 * 
 * Get the keyboard layout for a user.
 * 
 * Return value: The keyboard layout for the given user or #NULL if using system defaults.  Copy the value if you want to use it long term.
 **/
const gchar *
lightdm_user_get_layout (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->layouts[0];
}

/**
 * lightdm_user_get_layouts:
 * @user: A #LightDMUser
 * 
 * Get the configured keyboard layouts for a user.
 * 
 * Return value: (transfer none): A NULL-terminated array of keyboard layouts for the given user.  Copy the values if you want to use them long term.
 **/
const gchar * const *
lightdm_user_get_layouts (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return (const gchar * const *) GET_USER_PRIVATE (user)->layouts;
}

/**
 * lightdm_user_get_session:
 * @user: A #LightDMUser
 * 
 * Get the session for a user.
 * 
 * Return value: The session for the given user or #NULL if using system defaults.
 **/
const gchar *
lightdm_user_get_session (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->session; 
}

/**
 * lightdm_user_get_logged_in:
 * @user: A #LightDMUser
 * 
 * Check if a user is logged in.
 * 
 * Return value: #TRUE if the user is currently logged in.
 **/
gboolean
lightdm_user_get_logged_in (LightDMUser *user)
{
    LightDMUserPrivate *priv;
    LightDMUserListPrivate *list_priv;
    GList *link;

    g_return_val_if_fail (LIGHTDM_IS_USER (user), FALSE);

    priv = GET_USER_PRIVATE (user);
    list_priv = GET_LIST_PRIVATE (priv->user_list);

    for (link = list_priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (strcmp (session->username, priv->name) == 0)
            return TRUE;
    }

    return FALSE;
}

/**
 * lightdm_user_get_has_messages:
 * @user: A #LightDMUser
 * 
 * Check if a user has waiting messages.
 * 
 * Return value: #TRUE if the user has waiting messages.
 **/
gboolean
lightdm_user_get_has_messages (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), FALSE);
    load_user_values (user);
    return GET_USER_PRIVATE (user)->has_messages;
}

static void
lightdm_user_init (LightDMUser *user)
{
    LightDMUserPrivate *priv = GET_USER_PRIVATE (user);
    priv->layouts = g_malloc (sizeof (gchar *) * 1);
    priv->layouts[0] = NULL;
}

static void
lightdm_user_set_property (GObject    *object,
                           guint       prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lightdm_user_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LightDMUser *self;

    self = LIGHTDM_USER (object);

    switch (prop_id)
    {
    case USER_PROP_NAME:
        g_value_set_string (value, lightdm_user_get_name (self));
        break;
    case USER_PROP_REAL_NAME:
        g_value_set_string (value, lightdm_user_get_real_name (self));
        break;
    case USER_PROP_DISPLAY_NAME:
        g_value_set_string (value, lightdm_user_get_display_name (self));
        break;
    case USER_PROP_HOME_DIRECTORY:
        g_value_set_string (value, lightdm_user_get_home_directory (self));
        break;
    case USER_PROP_IMAGE:
        g_value_set_string (value, lightdm_user_get_image (self));
        break;
    case USER_PROP_BACKGROUND:
        g_value_set_string (value, lightdm_user_get_background (self));
        break;
    case USER_PROP_LANGUAGE:
        g_value_set_string (value, lightdm_user_get_language (self));
        break;
    case USER_PROP_LAYOUT:
        g_value_set_string (value, lightdm_user_get_layout (self));
        break;
    case USER_PROP_LAYOUTS:
        g_value_set_boxed (value, g_strdupv ((gchar **) lightdm_user_get_layouts (self)));
        break;
    case USER_PROP_SESSION:
        g_value_set_string (value, lightdm_user_get_session (self));
        break;
    case USER_PROP_LOGGED_IN:
        g_value_set_boolean (value, lightdm_user_get_logged_in (self));
        break;
    case USER_PROP_HAS_MESSAGES:
        g_value_set_boolean (value, lightdm_user_get_has_messages (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_user_finalize (GObject *object)
{
    LightDMUser *self = LIGHTDM_USER (object);
    LightDMUserPrivate *priv = GET_USER_PRIVATE (self);

    g_free (priv->path);
    if (priv->changed_signal)
        g_dbus_connection_signal_unsubscribe (GET_LIST_PRIVATE (priv->user_list)->bus, priv->changed_signal);
    g_free (priv->name);
    g_free (priv->real_name);
    g_free (priv->home_directory);
    g_free (priv->image);
    g_free (priv->background);
    g_strfreev (priv->layouts);
    if (priv->dmrc_file)
        g_key_file_free (priv->dmrc_file);
}

static void
lightdm_user_class_init (LightDMUserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMUserPrivate));

    object_class->set_property = lightdm_user_set_property;
    object_class->get_property = lightdm_user_get_property;
    object_class->finalize = lightdm_user_finalize;

    g_object_class_install_property (object_class,
                                     USER_PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "name",
                                                          "Username",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_REAL_NAME,
                                     g_param_spec_string ("real-name",
                                                          "real-name",
                                                          "Users real name",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_DISPLAY_NAME,
                                     g_param_spec_string ("display-name",
                                                          "display-name",
                                                          "Users display name",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_HOME_DIRECTORY,
                                     g_param_spec_string ("home-directory",
                                                          "home-directory",
                                                          "Home directory",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_IMAGE,
                                     g_param_spec_string ("image",
                                                          "image",
                                                          "Avatar image",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_BACKGROUND,
                                     g_param_spec_string ("background",
                                                          "background",
                                                          "User background",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LANGUAGE,
                                     g_param_spec_string ("language",
                                                         "language",
                                                         "Language used by this user",
                                                         NULL,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUT,
                                     g_param_spec_string ("layout",
                                                          "layout",
                                                          "Keyboard layout used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUTS,
                                     g_param_spec_boxed ("layouts",
                                                         "layouts",
                                                         "Keyboard layouts used by this user",
                                                         G_TYPE_STRV,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_SESSION,
                                     g_param_spec_string ("session",
                                                          "session",
                                                          "Session used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("logged-in",
                                                           "logged-in",
                                                           "TRUE if the user is currently in a session",
                                                           FALSE,
                                                           G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("has-messages",
                                                           "has-messages",
                                                           "TRUE if the user is has waiting messages",
                                                           FALSE,
                                                           G_PARAM_READWRITE));

    /**
     * LightDMUser::changed:
     * @user: A #LightDMUser
     *
     * The ::changed signal gets emitted this user account is modified.
     **/
    user_signals[CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserClass, changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
session_init (Session *session)
{
}

static void
session_finalize (GObject *object)
{
    Session *self = SESSION (object);

    g_free (self->path);
    g_free (self->username);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = session_finalize;
}
