#include <stdlib.h>
#include <glib.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);
  
    if (argc < 2)
    {
        g_printerr ("Usage: %s text [return-value]\n", argv[0]);
        return EXIT_FAILURE;
    }

    notify_status ("SCRIPT-HOOK %s", argv[1]);

    if (argc > 2)
        return atoi (argv[2]);
    else
        return EXIT_SUCCESS;
}
