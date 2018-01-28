/*
 * luakit.c - luakit main functions
 *
 * Copyright © 2010-2011 Mason Larobina <mason.larobina@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/util.h"
#include "globalconf.h"
#include "luah.h"
#include "ipc.h"
#include "log.h"
#include "web_context.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <webkit2/webkit2.h>

#if !WEBKIT_CHECK_VERSION(2,16,0)
#error Your version of WebKit is outdated!
#endif

static void
init_directories(void)
{
    /* create luakit directory */
    globalconf.cache_dir  = g_build_filename(g_get_user_cache_dir(),  "luakit", globalconf.profile, NULL);
    globalconf.config_dir = g_build_filename(g_get_user_config_dir(), "luakit", globalconf.profile, NULL);
    globalconf.data_dir   = g_build_filename(g_get_user_data_dir(),   "luakit", globalconf.profile, NULL);
    g_mkdir_with_parents(globalconf.cache_dir,  0771);
    g_mkdir_with_parents(globalconf.config_dir, 0771);
    g_mkdir_with_parents(globalconf.data_dir,   0771);
}

static void
parse_log_level_option(gchar *log_lvl)
{
    gchar **parts = g_strsplit(log_lvl, ",", 0);

    for (gchar **part = parts; *part; part++) {
        log_level_t lvl;
        if (!log_level_from_string(&lvl, *part))
            log_set_verbosity("all", lvl);
        else {
            gchar *sep = strchr(*part, '=');
            if (sep && !log_level_from_string(&lvl, sep+1)) {
                *sep = '\0';
                log_set_verbosity(*part, lvl);
            } else
                warn("ignoring unrecognized --log option '%s'", *part);
        }
    }
    g_strfreev(parts);
}

#if __GLIBC__ == 2 && __GLIBC_MINOR__ >= 50
static GLogWriterOutput
glib_log_writer(GLogLevelFlags log_level_flags, const GLogField *fields, gsize n_fields, gpointer UNUSED(user_data))
{
    const gchar *log_domain = "(unknown)",
                *message = "(empty)",
                *code_file = "(unknown)",
                *code_line = "(unknown)";

    for (gsize i = 0; i < n_fields; ++i) {
        if (!strcmp(fields[i].key, "GLIB_DOMAIN")) log_domain = fields[i].value;
        if (!strcmp(fields[i].key, "MESSAGE")) message = fields[i].value;
        if (!strcmp(fields[i].key, "CODE_FILE")) code_file = fields[i].value;
        if (!strcmp(fields[i].key, "CODE_LINE")) code_line = fields[i].value;
    }

    /* Probably not necessary, but just in case... */
    if (!(G_LOG_LEVEL_MASK & log_level_flags))
        return G_LOG_WRITER_UNHANDLED;

    log_level_t log_level = ((log_level_t[]){
        [G_LOG_LEVEL_ERROR]    = LOG_LEVEL_fatal,
        [G_LOG_LEVEL_CRITICAL] = LOG_LEVEL_warn,
        [G_LOG_LEVEL_WARNING]  = LOG_LEVEL_warn,
        [G_LOG_LEVEL_MESSAGE]  = LOG_LEVEL_info,
        [G_LOG_LEVEL_INFO]     = LOG_LEVEL_verbose,
        [G_LOG_LEVEL_DEBUG]    = LOG_LEVEL_debug,
    })[log_level_flags];

    _log(log_level, code_line, code_file, "%s: %s", log_domain, message);
    return G_LOG_WRITER_HANDLED;
}
#endif

void
luakit_startup(GApplication UNUSED(app))
{
    globalconf.windows = g_ptr_array_new();
#if __GLIBC__ == 2 && __GLIBC_MINOR__ >= 50
    g_log_set_writer_func(glib_log_writer, NULL, NULL);
#endif
    init_directories();
    web_context_init();
    ipc_init();
    luaH_init();
    /* parse and run configuration file */
    if (!luaH_parserc(globalconf.confpath, TRUE))
        fatal("couldn't find rc file");
}

// Let g_application_real_local_command_line call activate/open on remaining arguments
// 'activate' is called when there aren't any arguments to call 'open' on, here, send an empty open call
void
luakit_activate(GApplication *app)
{
    GFile **files;
    gint n_files = 0;
    files = g_new(GFile *, n_files);
    g_application_open(G_APPLICATION(app), files, n_files, "");
    g_free(files);
}

gint
luakit_local_options(GApplication UNUSED(app), GVariantDict *dict)
{
    GVariant *value;
    /* print version and exit */
    value = g_variant_dict_lookup_value(dict, "version", NULL);
    if (value) {
        g_printf("luakit %s\n", VERSION);
        return 0;
    }
    /* check config syntax and exit */
    value = g_variant_dict_lookup_value(dict, "check", NULL);
    if (value) {
        init_directories();
        luaH_init(NULL);
        if (!luaH_parserc(globalconf.confpath, FALSE)) {
            g_fprintf(stderr, "Confiuration file syntax error.\n");
            return 1;
        } else {
            g_fprintf(stderr, "Configuration file syntax OK.\n");
            return 0;
        }
    }
    /* log level */
    gboolean verbose = false;
    value = g_variant_dict_lookup_value(dict, "verbose", NULL);
    if (value) verbose = true;
    gchar *log_lvl = NULL;
    if (g_variant_dict_lookup(dict, "log", "s", &log_lvl)) {
        log_set_verbosity("all", LOG_LEVEL_info);
        parse_log_level_option(log_lvl);
        if (verbose)
            warn("invalid mix of -v and -l, ignoring -v...");
    } else {
        log_set_verbosity("all", verbose ? LOG_LEVEL_verbose : LOG_LEVEL_info);
    }
    return -1;
}

gint
main(gint argc, gchar *argv[])
{
    globalconf.starttime = l_time();
    globalconf.application_name = "org.luakit";
    if (!g_application_id_is_valid(globalconf.application_name))
        fatal("invalid application name");

    log_init();

    /* set numeric locale to C (required for compatibility with
       LuaJIT and luakit scripts) */
    gtk_disable_setlocale();
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    // Before gapplication kicks in, test for existence of the following flags
    gboolean *nonblock = NULL;
    gboolean *nonunique = false;
    /* const GOptionEntry low_lvl_entries[] = { */
    /*     { "nonblock",  'n', 0, G_OPTION_ARG_NONE, &nonblock,  "run in background",                    NULL }, */
    /*     { "nonunique", 'U', 0, G_OPTION_ARG_NONE, &nonunique, "Use the nonunique gapplication flag",  NULL }, */
    /*     { NULL } */
    /* }; */
    /* GOptionContext *context; */
    /* context = g_option_context_new(NULL); */
    /* g_option_context_add_main_entries(context, low_lvl_entries, NULL); */
    /* g_option_context_set_help_enabled(context, false); */
    /* g_option_context_parse(context, &argc, &argv, NULL); */
    /* g_option_context_free(context); */

    /* if non block mode - respawn, detach and continue in child */
    if (nonblock) {
        pid_t pid = fork();
        if (pid < 0) {
            fatal("Cannot fork: %d", errno);
        } else if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        pid_t sid = setsid();
        if (sid < 0) {
            fatal("New SID creation failure: %d", errno);
        }
    }

    /* save luakit exec path */
    globalconf.execpath = g_strdup(argv[0]);
    /* Save a copy of argv */
    globalconf.argv = g_ptr_array_new_with_free_func(g_free);
    for (gint i = 0; i < argc; ++i)
        g_ptr_array_add(globalconf.argv, g_strdup(argv[i]));

    globalconf.application = gtk_application_new(globalconf.application_name, G_APPLICATION_HANDLES_OPEN);
    if (nonunique)
        g_application_set_flags(G_APPLICATION(globalconf.application), G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_OPEN);

    /* register command line options */
    const GOptionEntry entries[] = {
        { "check",     'k', 0, G_OPTION_ARG_NONE,   NULL,                 "check config and exit",               NULL   },
        { "version",   'V', 0, G_OPTION_ARG_NONE,   NULL,                 "print version and exit",              NULL   },
        { "verbose",   'v', 0, G_OPTION_ARG_NONE,   NULL,                 "print verbose output",                NULL   },
        { "log",       'l', 0, G_OPTION_ARG_STRING, NULL,                 "specify precise log level",           "NAME" },
        { "config",    'c', 0, G_OPTION_ARG_STRING, &globalconf.confpath, "configuration file to use",           "FILE" },
        { "profile",   'p', 0, G_OPTION_ARG_STRING, &globalconf.profile,  "profile name to use",                 "NAME" },
        // following two entries are only in this entry group to be displayed via --help
        { "nonblock",  'n', 0, G_OPTION_ARG_NONE,   &nonblock,            "run in background",                   NULL   },
        { "nonunique", 'U', 0, G_OPTION_ARG_NONE,   &nonunique,           "Use the nonunique gapplication flag", NULL   },
        { NULL }
    };
    g_application_add_main_option_entries(G_APPLICATION(globalconf.application), entries);

    g_signal_connect(globalconf.application, "handle-local-options", G_CALLBACK(luakit_local_options), NULL);
    g_signal_connect(globalconf.application, "startup", G_CALLBACK(luakit_startup), NULL);
    g_signal_connect(globalconf.application, "open", G_CALLBACK(luakit_browse), NULL);
    g_signal_connect(globalconf.application, "activate", G_CALLBACK(luakit_activate), NULL);

    // Duplicate argv for g_application_run, then erase the real argv
    char **argv2 = g_strdupv(argv);
    /* hide command line parameters so process lists don't leak (possibly
       confidential) URLs */
    for (gint i = 1; i < argc; i++)
        memset(argv[i], 0, strlen(argv[i]));
    int status = g_application_run(G_APPLICATION(globalconf.application), argc, argv2);
    g_object_unref(globalconf.application);
    return status;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
