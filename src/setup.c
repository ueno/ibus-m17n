/* vim:set et sts=4: */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <ibus.h>
#include <m17n.h>
#include <string.h>
#include <errno.h>
#include "m17nutil.h"

enum {
    COLUMN_KEY,
    COLUMN_VALUE,
    COLUMN_DESCRIPTION,
    NUM_COLS
};

struct _SetupDialog {
    GtkWidget *dialog;
    GtkWidget *combobox_underline;
    GtkWidget *combobox_orientation;
    GtkWidget *checkbutton_foreground;
    GtkWidget *colorbutton_foreground;
    GtkWidget *checkbutton_background;
    GtkWidget *colorbutton_background;
    GtkWidget *treeview;
    GtkListStore *store;

    gchar *lang;
    gchar *name;

    IBusConfig *config;
    gchar *section;
};
typedef struct _SetupDialog SetupDialog;

static gchar *opt_name = NULL;
static const GOptionEntry options[] = {
    {"name", '\0', 0, G_OPTION_ARG_STRING, &opt_name,
     "IBus engine name like \"m17n:si:wijesekera\"."},
    {NULL}
};

static gchar *
format_m17n_value (MPlist *plist)
{
    if (mplist_key (plist) == Msymbol)
        return g_strdup (msymbol_name ((MSymbol) mplist_value (plist)));

    if (mplist_key (plist) == Mtext)
        return g_strdup (mtext_data ((MText *) mplist_value (plist),
                                     NULL, NULL, NULL, NULL));

    if (mplist_key (plist) == Minteger)
        return g_strdup_printf ("%d", (gint) (long) mplist_value (plist));

    return NULL;
}

static MPlist *
parse_m17n_value (MPlist *plist, gchar *text)
{
    MPlist *value;

    if (mplist_key (plist) == Msymbol) {
        value = mplist ();
        mplist_add (value, Msymbol, msymbol (text));
        return value;
    }

    if (mplist_key (plist) == Mtext) {
        MText *mtext;

        mtext = mtext_from_data (text, strlen (text), MTEXT_FORMAT_UTF_8);
        value = mplist ();
        mplist_add (value, Mtext, mtext);
        return value;
    }

    if (mplist_key (plist) == Minteger) {
        long val;

        errno = 0;
        val = strtol (text, NULL, 10);
        if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0))
            return NULL;
        value = mplist ();
        mplist_add (value, Minteger, (void *)val);
        return value;
    }

    return NULL;
}

static void
insert_m17n_items (GtkListStore *store, MSymbol language, MSymbol name)
{
    MPlist *plist;

    plist = minput_get_variable (language, name, Mnil);

    for (; plist && mplist_key (plist) == Mplist; plist = mplist_next (plist)) {
        GtkTreeIter iter;
        MSymbol key;
        MPlist *p, *mvalue;
        gchar *description, *value;

        p = mplist_value (plist);
        key = mplist_value (p); /* name */

        p = mplist_next (p);  /* description */
        description = ibus_m17n_mtext_to_utf8 ((MText *) mplist_value (p));
        p = mplist_next (p);  /* status */
        mvalue = mplist_next (p);
        value = format_m17n_value (mvalue);

        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            COLUMN_KEY, msymbol_name (key),
                            COLUMN_DESCRIPTION, description,
                            COLUMN_VALUE, value,
                            -1);
        g_free (description);
        g_free (value);
    }
}

static gboolean
on_query_tooltip (GtkWidget  *widget,
                  gint        x,
                  gint        y,
                  gboolean    keyboard_tip,
                  GtkTooltip *tooltip,
                  gpointer    user_data)
{
    GtkTreeView *treeview = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model (treeview);
    GtkTreePath *path = NULL;
    GtkTreeIter iter;
    gchar *description;

    if (!gtk_tree_view_get_tooltip_context (treeview, &x, &y,
                                            keyboard_tip,
                                            &model, &path, &iter))
        return FALSE;

    gtk_tree_model_get (model, &iter, COLUMN_DESCRIPTION, &description, -1);
    gtk_tooltip_set_text (tooltip, description);
    gtk_tree_view_set_tooltip_row (treeview, tooltip, path);
    gtk_tree_path_free (path);
    g_free (description);
    return TRUE;
}

static void
on_edited (GtkCellRendererText *cell,
           gchar               *path_string,
           gchar               *new_text,
           gpointer             data)
{
    SetupDialog *dialog = data;
    GtkTreeModel *model = GTK_TREE_MODEL (dialog->store);
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

    gtk_tree_model_get_iter (model, &iter, path);

    gtk_list_store_set (dialog->store, &iter,
                        COLUMN_VALUE, new_text,
                        -1);
    gtk_tree_path_free (path);
}

static void
toggle_colorbutton_sensitive (GtkToggleButton *togglebutton,
                    GtkWidget       *colorbutton)
{
    if (gtk_toggle_button_get_active (togglebutton))
        gtk_widget_set_sensitive (colorbutton, TRUE);
    else
        gtk_widget_set_sensitive (colorbutton, FALSE);
}

static void
on_foreground_toggled (GtkToggleButton *togglebutton,
                       gpointer         user_data)
{
    SetupDialog *dialog = user_data;
    toggle_colorbutton_sensitive (togglebutton, dialog->colorbutton_foreground);
}

static void
on_background_toggled (GtkToggleButton *togglebutton,
                       gpointer         user_data)
{
    SetupDialog *dialog = user_data;
    toggle_colorbutton_sensitive (togglebutton, dialog->colorbutton_background);
}

static gint
get_combo_box_index_by_value (GtkComboBox *combobox, gint value)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint index;

    index = 0;
    model = gtk_combo_box_get_model (combobox);
    if (!gtk_tree_model_get_iter_first (model, &iter))
        return -1;

    do {
        gint _value;
        gtk_tree_model_get (model, &iter, COLUMN_VALUE, &_value, -1);
        if (_value == value)
            return index;
        index++;
    } while (gtk_tree_model_iter_next (model, &iter));
    return -1;
}

static void
_gdk_color_from_uint (guint color, GdkColor *color_gdk)
{
    color_gdk->pixel = 0;
    color_gdk->red = (color >> 8) & 0xFF00;
    color_gdk->green = color & 0xFF00;
    color_gdk->blue = (color & 0xFF) << 8;
}

static void
setup_dialog_load_config (SetupDialog *dialog)
{
    gchar *color;
    gboolean bvalue;
    gint ivalue;
    GdkColor cvalue;
    GtkCellRenderer *renderer;
    gint index;

    /* General -> Pre-edit Appearance */
    /* foreground color of pre-edit buffer */
    bvalue = FALSE;
    _gdk_color_from_uint (PREEDIT_FOREGROUND, &cvalue);
    if (ibus_m17n_config_get_string (dialog->config,
                                     dialog->section,
                                     "preedit_foreground",
                                     &color)) {
        if (g_strcmp0 (color, "none") != 0 && gdk_color_parse (color, &cvalue))
            bvalue = TRUE;
        g_free (color);
    }

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(dialog->checkbutton_foreground),
                                  bvalue);
    g_signal_connect (dialog->checkbutton_foreground, "toggled",
                      G_CALLBACK(on_foreground_toggled),
                      dialog);
    gtk_widget_set_sensitive (dialog->colorbutton_foreground,
                              bvalue);
    gtk_color_button_set_color (GTK_COLOR_BUTTON(dialog->colorbutton_foreground),
                                &cvalue);
    
    /* background color of pre-edit buffer */
    bvalue = FALSE;
    _gdk_color_from_uint (PREEDIT_BACKGROUND, &cvalue);
    if (ibus_m17n_config_get_string (dialog->config,
                                     dialog->section,
                                     "preedit_background",
                                     &color)) {
        if (g_strcmp0 (color, "none") != 0 && gdk_color_parse (color, &cvalue))
            bvalue = TRUE;
        g_free (color);
    }
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(dialog->checkbutton_background),
                                  bvalue);
    g_signal_connect (dialog->checkbutton_background, "toggled",
                      G_CALLBACK(on_background_toggled),
                      dialog);
    gtk_widget_set_sensitive (dialog->colorbutton_background,
                              bvalue);
    gtk_color_button_set_color (GTK_COLOR_BUTTON(dialog->colorbutton_background),
                                &cvalue);

    /* underline of pre-edit buffer */
    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(dialog->combobox_underline),
                                renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT(dialog->combobox_underline),
                                    renderer, "text", 0, NULL);
    if (!ibus_m17n_config_get_int (dialog->config,
                                   dialog->section,
                                   "preedit_underline",
                                   &ivalue))
        ivalue = IBUS_ATTR_UNDERLINE_NONE;

    index = get_combo_box_index_by_value
        (GTK_COMBO_BOX(dialog->combobox_underline),
         ivalue);
    gtk_combo_box_set_active (GTK_COMBO_BOX(dialog->combobox_underline),
                              index);

    /* General -> Other */
    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(dialog->combobox_orientation),
                                renderer, TRUE);
    gtk_cell_layout_set_attributes
        (GTK_CELL_LAYOUT(dialog->combobox_orientation),
         renderer, "text", 0, NULL);
    if (!ibus_m17n_config_get_int (dialog->config,
                                   dialog->section,
                                   "lookup_table_orientation",
                                   &ivalue))
        ivalue = IBUS_ORIENTATION_SYSTEM;

    index = get_combo_box_index_by_value
        (GTK_COMBO_BOX(dialog->combobox_orientation),
         ivalue);
    gtk_combo_box_set_active (GTK_COMBO_BOX(dialog->combobox_orientation),
                              index);

    /* Advanced -> m17n-lib configuration */
    dialog->store = gtk_list_store_new (NUM_COLS,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING);
    insert_m17n_items (dialog->store,
                       msymbol (dialog->lang),
                       msymbol (dialog->name));

    gtk_tree_view_set_model (GTK_TREE_VIEW(dialog->treeview),
                             GTK_TREE_MODEL (dialog->store));

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes
        (GTK_TREE_VIEW (dialog->treeview), -1,
         "Key",
         renderer,
         "text", COLUMN_KEY,
         NULL);
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes
        (GTK_TREE_VIEW (dialog->treeview), -1,
         "Value",
         renderer,
         "text", COLUMN_VALUE,
         NULL);
    g_object_set (renderer, "editable", TRUE, NULL);
    g_signal_connect (renderer, "edited", G_CALLBACK(on_edited), dialog);

    g_signal_connect (dialog->treeview, "query-tooltip",
                      G_CALLBACK(on_query_tooltip), NULL);
}

static gchar *
_gdk_color_to_string (GdkColor *color)
{
    g_strdup_printf ("#%02X%02X%02X",
                     (color->red & 0xFF00) >> 8,
                     (color->green & 0xFF00) >> 8,
                     (color->blue & 0xFF00) >> 8);
}

static void
save_color (SetupDialog     *dialog,
            GtkToggleButton *togglebutton,
            GtkColorButton  *colorbutton,
            const gchar     *name)
{
    if (gtk_toggle_button_get_active (togglebutton)) {
        GdkColor color;
        gchar *svalue;

        gtk_color_button_get_color (colorbutton, &color);
        svalue = _gdk_color_to_string (&color);
        ibus_m17n_config_set_string (dialog->config,
                                     dialog->section,
                                     name,
                                     svalue);
        g_free (svalue);
    } else
        ibus_m17n_config_set_string (dialog->config,
                                     dialog->section,
                                     name,
                                     "none");
}

static void
save_choice (SetupDialog *dialog,
             GtkComboBox *combo,
             const gchar *name)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint active;

    model = gtk_combo_box_get_model (combo);
    gtk_combo_box_get_active_iter (combo, &iter);
    gtk_tree_model_get (model, &iter, COLUMN_VALUE, &active, -1);

    ibus_m17n_config_set_int (dialog->config, dialog->section, name, active);
}

static gboolean
save_m17n_options (SetupDialog *dialog)
{
    GtkTreeModel *model = GTK_TREE_MODEL (dialog->store);
    GtkTreeIter iter;
    MPlist *plist, *p, *mvalue = NULL;
    MSymbol lang, name;
    gchar *key = NULL, *value = NULL;
    gboolean retval = TRUE;

    if (!gtk_tree_model_get_iter_first (model, &iter))
        return;

    lang = msymbol (dialog->lang);
    name = msymbol (dialog->name);

    do {
        gtk_tree_model_get (model, &iter,
                            COLUMN_KEY, &key,
                            COLUMN_VALUE, &value,
                            -1);

        plist = minput_get_variable (lang, name, msymbol (key));
        if (!plist) {
            retval = FALSE;
            break;
        }

        p = mplist_next (mplist_next (mplist_next (mplist_value (plist))));
        if (!p) {
            retval = FALSE;
            break;
        }

        mvalue = parse_m17n_value (p, value);
        if (!mvalue) {
            retval = FALSE;
            break;
        }

        if (minput_config_variable (lang, name, msymbol (key), mvalue) != 0) {
            retval = FALSE;
            break;
        }

        if (mvalue)
            m17n_object_unref (mvalue);
        g_free (key);
        g_free (value);
        mvalue = NULL;
        key = NULL;
        value = NULL;
    } while (gtk_tree_model_iter_next (model, &iter));

    if (retval && minput_save_config () != 1)
        retval = FALSE;

    if (mvalue)
        m17n_object_unref (mvalue);
    g_free (key);
    g_free (value);

    return retval;
}

static void
setup_dialog_save_config (SetupDialog *dialog)
{
    save_color (dialog,
                GTK_TOGGLE_BUTTON(dialog->checkbutton_foreground),
                GTK_COLOR_BUTTON(dialog->colorbutton_foreground),
                "preedit_foreground");
    save_color (dialog,
                GTK_TOGGLE_BUTTON(dialog->checkbutton_background),
                GTK_COLOR_BUTTON(dialog->colorbutton_background),
                "preedit_background");
    save_choice (dialog,
                 GTK_COMBO_BOX(dialog->combobox_underline),
                 "preedit_underline");
    save_choice (dialog,
                 GTK_COMBO_BOX(dialog->combobox_orientation),
                 "lookup_table_orientation");
    save_m17n_options (dialog);
}

static SetupDialog *
setup_dialog_new (IBusConfig  *config,
                  const gchar *lang,
                  const gchar *name)
{
    GtkBuilder *builder;
    SetupDialog *dialog;
    GObject *object;
    GError *error;

    dialog = g_slice_new0 (SetupDialog);
    dialog->config = g_object_ref_sink (config);
    dialog->lang = g_strdup (lang);
    dialog->name = g_strdup (name);
    dialog->section = g_strdup_printf ("engine/M17N/%s/%s", lang, name);

    builder = gtk_builder_new ();
    gtk_builder_set_translation_domain (builder, "ibus-m17n");

    error = NULL;
    gtk_builder_add_from_file (builder,
                               SETUPDIR "/ibus-m17n-preferences.ui",
                               &error);
    g_assert_no_error (error);

    object = gtk_builder_get_object (builder, "dialog");
    dialog->dialog = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "checkbutton_foreground");
    dialog->checkbutton_foreground = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "colorbutton_foreground");
    dialog->colorbutton_foreground = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "checkbutton_background");
    dialog->checkbutton_background = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "colorbutton_background");
    dialog->colorbutton_background = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "combobox_underline");
    dialog->combobox_underline = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "combobox_orientation");
    dialog->combobox_orientation = GTK_WIDGET(object);
    object = gtk_builder_get_object (builder, "treeview_mim_config");
    dialog->treeview = GTK_WIDGET(object);

    return dialog;
}

static void
setup_dialog_free (SetupDialog *dialog)
{
    gtk_widget_destroy (dialog->dialog);

    g_free (dialog->lang);
    g_free (dialog->name);

    g_free (dialog->section);
    g_object_unref (dialog->store);
    g_slice_free (SetupDialog, dialog);
}

static void
start (const gchar *engine_name)
{
    IBusBus *bus;
    IBusConfig *config;
    gchar **strv, *lang, *name, *section;
    SetupDialog *dialog;
    GObject *object;
    GError *error = NULL;

    ibus_init ();
    ibus_m17n_init_common ();

    strv = g_strsplit (engine_name, ":", 3);
    
    g_assert (g_strv_length (strv) == 3);
    g_assert (g_strcmp0 (strv[0], "m17n") == 0);

    lang = strv[1];
    name = strv[2];

    bus = ibus_bus_new ();
    //g_signal_connect (bus, "disconnected", G_CALLBACK (ibus_disconnected_cb), NULL);

    config = ibus_bus_get_config (bus);
    dialog = setup_dialog_new (config, lang, name);

    g_strfreev (strv);

    setup_dialog_load_config (dialog);

    gtk_widget_show_all (dialog->dialog);
    gtk_dialog_run (GTK_DIALOG(dialog->dialog));

    setup_dialog_save_config (dialog);
    setup_dialog_free (dialog);

    g_object_unref (bus);

    M17N_FINI ();
}

int
main (gint argc, gchar **argv)
{
    GOptionContext *context;

    context = g_option_context_new ("ibus-setup-m17n");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    gtk_init (&argc, &argv);
    if (!opt_name) {
        fprintf (stderr, "can't determine IBus engine name; use --name\n");
        exit (1);
    }

    if (strncmp (opt_name, "m17n:", 5) != 0 ||
        strchr (&opt_name[5], ':') == NULL) {
        fprintf (stderr, "wrong format of IBus engine name\n");
        exit (1);
    }

    start (opt_name);

    return 0;
}
