/* vim: set backspace=2 ts=4 softtabstop=4 sw=4 cinoptions=>4 expandtab autoindent smartindent: */
/* xkb-config.c
 *
 * Copyright (C) 2013 Dmitriy Poltavchenko <zen@root.ua>
 *
 * Copyright (C) 2008 Alexander Iliev <sasoiliev@mamul.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gdk/gdkx.h>

#include "xkb-config.h"

#ifndef DEBUG
#define G_DISABLE_ASSERT
#endif

typedef struct
{
    XklEngine*            engine;

    gchar               **group_names;
    gchar               **variants;
    t_xkb_settings       *settings;
    GHashTable           *variant_index_by_group;


    GHashTable           *application_map;
    GHashTable           *window_map;

    guint                 current_window_id;
    guint                 current_application_id;

    gint                  group_count;

    XkbCallback           callback;
    gpointer              callback_data;


    XklConfigRec         *config_rec;
} t_xkb_config;

t_xkb_config *config;

void                xkb_config_state_changed            (XklEngine *engine,
        XklEngineStateChange *change,
        gint group,
        gboolean restore);

void                xkb_config_xkl_config_changed       (XklEngine *engine);

GdkFilterReturn     handle_xevent                       (GdkXEvent * xev,
        GdkEvent * event);

void                xkb_config_update_configuration     (t_xkb_settings *settings);
void         xkb_config_free                     ();
void         xkb_config_initialize_xkb_options   (t_xkb_settings *settings);

/* ---------------------- implementation ------------------------- */

gboolean
xkb_config_initialize (t_xkb_settings *settings,
                       XkbCallback callback,
                       gpointer callback_data)
{
    g_assert (settings != NULL);

    config = g_new0 (t_xkb_config, 1);

    config->settings = settings;

    config->callback = callback;
    config->callback_data = callback_data;

    config->engine = xkl_engine_get_instance (GDK_DISPLAY ());

    if (!config->engine)
    {
        return FALSE;
    }

    xkb_config_update_settings (settings);

    xkl_engine_set_group_per_toplevel_window (config->engine, FALSE);

    xkl_engine_start_listen (config->engine, XKLL_TRACK_KEYBOARD_STATE);

    g_signal_connect (config->engine,
                      "X-state-changed",
                      G_CALLBACK (xkb_config_state_changed),
                      NULL);
    g_signal_connect (config->engine,
                      "X-config-changed",
                      G_CALLBACK (xkb_config_xkl_config_changed),
                      NULL);
    gdk_window_add_filter (NULL, (GdkFilterFunc) handle_xevent, NULL);

    return TRUE;
}

void
xkb_config_initialize_xkb_options (t_xkb_settings *settings)
{
    XklConfigRegistry *registry;
    XklConfigItem *config_item;
    GHashTable *index_variants;
    gchar **group;
    gint val, i;
    gpointer pval;

    xkb_config_free ();

    group = config->config_rec->layouts;
    config->group_count = 0;
    while (*group)
    {
        group++;
        config->group_count++;
    }

    config->window_map = g_hash_table_new (g_direct_hash, NULL);
    config->application_map = g_hash_table_new (g_direct_hash, NULL);

    registry = xkl_config_registry_get_instance (config->engine);

#if LIBXKLAVIER_VERSION >= 40
    xkl_config_registry_load (registry, FALSE);
#else
    xkl_config_registry_load (registry);
#endif

    g_object_unref (registry);

    config_item = xkl_config_item_new ();

    config->group_names = (gchar **) g_new0 (typeof (gchar **), config->group_count);
    config->variants = (gchar **) g_new0 (typeof (gchar **), config->group_count);
    config->variant_index_by_group = g_hash_table_new (NULL, NULL);
    index_variants = g_hash_table_new (g_str_hash, g_str_equal);

    for (i = 0; i < config->group_count; i++)
    {

        g_stpcpy (config_item->name, config->config_rec->layouts[i]);
        config->group_names[i] = g_strdup (config->config_rec->layouts[i]);

        if (config->config_rec->variants[i] != NULL)
        {
            g_stpcpy (config_item->name, config->config_rec->variants[i]);
        }
        config->variants[i] = (config->config_rec->variants[i] == NULL)
                              ? g_strdup ("") : g_strdup (config->config_rec->variants[i]);

        pval = g_hash_table_lookup (
                   index_variants,
                   config->group_names[i]
               );
        val = (pval != NULL) ? GPOINTER_TO_INT (pval) : 0;
        val++;
        g_hash_table_insert (
            config->variant_index_by_group,
            config->group_names[i],
            GINT_TO_POINTER (val)
        );
        g_hash_table_insert (
            index_variants,
            config->group_names[i],
            GINT_TO_POINTER (val)
        );
    }
    g_hash_table_destroy (index_variants);
    g_object_unref (config_item);
}

void
kbd_config_free (t_xkb_kbd_config *kbd_config)
{
    g_free (kbd_config->model);
    g_free (kbd_config->layouts);
    g_free (kbd_config->variants);
    g_free (kbd_config->toggle_option);
    g_free (kbd_config->compose_key_position);
    g_free (kbd_config);
}

void
xkb_config_free (void)
{
    gint i;

    g_assert (config != NULL);

    if (config->group_names)
    {
        for (i = 0; i < config->group_count; i++)
            g_free (config->group_names[i]);
        g_free (config->group_names);
    }

    if (config->variants)
    {
        for (i = 0; i < config->group_count; i++)
            g_free (config->variants[i]);
        g_free (config->variants);
    }

    if (config->variant_index_by_group)
        g_hash_table_destroy (config->variant_index_by_group);

    if (config->window_map)
        g_hash_table_destroy (config->window_map);

    if (config->application_map)
        g_hash_table_destroy (config->application_map);
}

void
xkb_config_finalize (void)
{
    xkb_config_free ();

#if LIBXKLAVIER_VERSION >= 50
    xkl_engine_stop_listen (config->engine, XKLL_TRACK_KEYBOARD_STATE);
#else
    xkl_engine_stop_listen (config->engine);
#endif
    g_object_unref (config->config_rec);

    g_free (config);

    gdk_window_remove_filter (NULL, (GdkFilterFunc) handle_xevent, NULL);
}

gint
xkb_config_get_current_group (void)
{
    XklState* state = xkl_engine_get_current_state (config->engine);
    return state->group;
}

gboolean
xkb_config_set_group (gint group)
{
    g_assert (config != NULL);

    if (G_UNLIKELY (group < 0 || group >= config->group_count))
    {
        return FALSE;
    }

    xkl_engine_lock_group (config->engine, group);

    return TRUE;
}

gboolean
xkb_config_next_group (void)
{
    xkl_engine_lock_group (config->engine,
                           xkl_engine_get_next_group (config->engine));

    return TRUE;
}

gboolean
xkb_config_prev_group (void)
{
    xkl_engine_lock_group (config->engine,
                           xkl_engine_get_prev_group (config->engine));

    return TRUE;
}

gboolean
xkb_config_update_settings (t_xkb_settings *settings)
{
    gboolean activate_settings = FALSE;

    gchar **opt;
    gchar **prefix;

    g_assert (config != NULL);
    g_assert (settings != NULL);

    config->settings = settings;

    if (config->config_rec == NULL)
    {
        config->config_rec = xkl_config_rec_new ();
    }

    if (settings->kbd_config == NULL || settings->never_modify_config)
    {
        xkl_config_rec_get_from_server (config->config_rec, config->engine);
        if (settings->kbd_config == NULL)
            settings->kbd_config = g_new0 (t_xkb_kbd_config, 1);

        g_free (settings->kbd_config->model);
        settings->kbd_config->model = g_strdup (config->config_rec->model);
        g_free (settings->kbd_config->layouts);
        settings->kbd_config->layouts = g_strjoinv (",", config->config_rec->layouts);

        /* XklConfigRec uses for NULL for empty variant instead of "".
         * So if has skipped variants we can't get proper settings->kbd_config->variants.
         * So I use this hack to get proper kbd_config->variants */
        gchar *tmp1 = g_strdup("");
        gchar *tmp2 = NULL;
        int i;
        for (i = 0; config->config_rec->layouts[i]; i++)
        {
            tmp2 = g_strconcat (tmp1, config->config_rec->variants[i] ? config->config_rec->variants[i] : "", NULL);
            g_free(tmp1);
            tmp1 = tmp2;
            if (config->config_rec->layouts[i + 1])
            {
                tmp2 = g_strconcat (tmp1, ",", NULL);
                g_free(tmp1);
                tmp1 = tmp2;
            }
        }
        g_free (settings->kbd_config->variants);
        settings->kbd_config->variants = tmp2;
    }
    else
    {
        gchar *options;

        activate_settings = TRUE;

        g_free (config->config_rec->model);
        config->config_rec->model = g_strdup (settings->kbd_config->model);

        g_strfreev (config->config_rec->layouts);
        config->config_rec->layouts = g_strsplit_set (settings->kbd_config->layouts, ",", 0);

        g_strfreev (config->config_rec->variants);
        config->config_rec->variants = g_strsplit_set (settings->kbd_config->variants, ",", 0);

        if (settings->kbd_config->toggle_option
                && strlen (settings->kbd_config->toggle_option) > 0)
            options = g_strdup (settings->kbd_config->toggle_option);
        else options = g_strdup ("");

        if (settings->kbd_config->compose_key_position
                && strlen (settings->kbd_config->compose_key_position) > 0)
        {
            gchar *tmp = options;
            options = g_strconcat (tmp, ",", settings->kbd_config->compose_key_position, NULL);
            g_free (tmp);
        }

        g_strfreev (config->config_rec->options);
        config->config_rec->options = g_strsplit_set (options, ",", 0);
        g_free (options);
    }

    g_free (settings->kbd_config->toggle_option);
    settings->kbd_config->toggle_option = NULL;
    g_free (settings->kbd_config->compose_key_position);
    settings->kbd_config->compose_key_position = NULL;
    opt = config->config_rec->options;
    while (opt && *opt)
    {
        prefix = g_strsplit(*opt, ":", 2);
        if (prefix && *prefix && strcmp(*prefix, "compose") != 0)
        {
            if (settings->kbd_config->toggle_option == NULL)
            {
                settings->kbd_config->toggle_option = g_strdup (*opt);
            }
            else
            {
                settings->kbd_config->toggle_option = g_strconcat(
                        settings->kbd_config->toggle_option, ",", g_strdup (*opt), NULL);
            }
        }
        else if (prefix && *prefix && strcmp(*prefix, "compose") == 0)
        {
            settings->kbd_config->compose_key_position = g_strdup (*opt);
        }

        g_strfreev (prefix);
        opt++;
    }

    if (activate_settings && !settings->never_modify_config)
        xkl_config_rec_activate (config->config_rec, config->engine);

    xkb_config_initialize_xkb_options (settings);

    return TRUE;
}

void
xkb_config_window_changed (guint new_window_id, guint application_id)
{
    gint group;
    gpointer key, value;
    GHashTable *hashtable;
    guint id;
    g_assert (config != NULL);

    id = 0;
    hashtable = NULL;

    switch (config->settings->group_policy)
    {
    case GROUP_POLICY_GLOBAL:
        return;

    case GROUP_POLICY_PER_WINDOW:
        hashtable = config->window_map;
        id = new_window_id;
        config->current_window_id = id;
        break;

    case GROUP_POLICY_PER_APPLICATION:
        hashtable = config->application_map;
        id = application_id;
        config->current_application_id = id;
        break;
    }

    group = config->settings->default_group;

    if (g_hash_table_lookup_extended (hashtable, GINT_TO_POINTER (id), &key, &value))
    {
        group = GPOINTER_TO_INT (value);
    }

    g_hash_table_insert (
        hashtable,
        GINT_TO_POINTER (id),
        GINT_TO_POINTER (group)
    );

    xkb_config_set_group (group);
}

void
xkb_config_application_closed (guint application_id)
{
    g_assert (config != NULL);

    switch (config->settings->group_policy)
    {
    case GROUP_POLICY_GLOBAL:
    case GROUP_POLICY_PER_WINDOW:
        return;

    case GROUP_POLICY_PER_APPLICATION:
        g_hash_table_remove (
            config->application_map,
            GINT_TO_POINTER (application_id)
        );

        break;
    }
}

void
xkb_config_window_closed (guint window_id)
{
    g_assert (config != NULL);

    switch (config->settings->group_policy)
    {
    case GROUP_POLICY_GLOBAL:
    case GROUP_POLICY_PER_APPLICATION:
        return;

    case GROUP_POLICY_PER_WINDOW:
        g_hash_table_remove (
            config->window_map,
            GINT_TO_POINTER (window_id)
        );

        break;
    }
}


gint
xkb_config_get_group_count (void)
{
    g_assert (config != NULL);

    return config->group_count;
}

const gchar*
xkb_config_get_group_name (gint group)
{
    g_assert (config != NULL);

    if (G_UNLIKELY (group >= config->group_count))
        return NULL;

    if (group == -1)
        group = xkb_config_get_current_group ();

    return config->group_names[group];
}

const gchar*
xkb_config_get_variant (gint group)
{
    g_assert (config != NULL);

    if (G_UNLIKELY (group >= config->group_count))
        return NULL;

    if (group == -1)
        group = xkb_config_get_current_group ();

    return config->variants[group];
}

void
xkb_config_state_changed (XklEngine *engine,
                          XklEngineStateChange *change,
                          gint group,
                          gboolean restore)
{
    if (change == GROUP_CHANGED)
    {
        switch (config->settings->group_policy)
        {
        case GROUP_POLICY_GLOBAL:
            break;

        case GROUP_POLICY_PER_WINDOW:
            g_hash_table_insert (
                config->window_map,
                GINT_TO_POINTER (config->current_window_id),
                GINT_TO_POINTER (group)
            );
            break;

        case GROUP_POLICY_PER_APPLICATION:
            g_hash_table_insert (
                config->application_map,
                GINT_TO_POINTER (config->current_application_id),
                GINT_TO_POINTER (group)
            );
            break;
        }

        if (config->callback != NULL) config->callback (group, FALSE, config->callback_data);
    }
}

void
xkb_config_xkl_config_changed (XklEngine *engine)
{
    kbd_config_free (config->settings->kbd_config);
    config->settings->kbd_config = NULL;
    xkb_config_update_settings (config->settings);

    if (config->callback != NULL)
        config->callback (xkb_config_get_current_group (), TRUE, config->callback_data);
}

gint
xkb_config_variant_index_for_group (gint group)
{
    gpointer presult;
    gint result;
    gchar *key;

    g_return_val_if_fail (config != NULL, 0);

    if (group == -1) group = xkb_config_get_current_group ();

    key = config->group_names[group];

    presult = g_hash_table_lookup (
                  config->variant_index_by_group,
                  key
              );
    if (presult == NULL) return 0;

    result = GPOINTER_TO_INT (presult);
    result = (result <= 0) ? 0 : result - 1;
    return result;
}

GdkFilterReturn
handle_xevent (GdkXEvent * xev, GdkEvent * event)
{
    XEvent *xevent = (XEvent *) xev;

    xkl_engine_filter_events (config->engine, xevent);

    return GDK_FILTER_CONTINUE;
}

XklConfigRegistry*
xkb_config_get_xkl_registry (void)
{
    XklConfigRegistry *registry;

    if (!config) return NULL;

    registry = xkl_config_registry_get_instance (config->engine);

#if LIBXKLAVIER_VERSION >= 40
    xkl_config_registry_load (registry, FALSE);
#else
    xkl_config_registry_load (registry);
#endif

    return registry;
}

gint
xkb_config_get_max_layout_number (void)
{
    if (config == NULL) return 0;

    return xkl_engine_get_max_num_groups (config->engine);
}
