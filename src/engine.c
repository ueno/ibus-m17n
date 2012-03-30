/* vim:set et sts=4: */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ibus.h>
#include <m17n.h>
#include <string.h>
#include "m17nutil.h"
#ifdef HAVE_EEKBOARD
#include <eekboard/eekboard-client.h>
#endif  /* HAVE_EEKBOARD */
#include "engine.h"

typedef struct _IBusM17NEngine IBusM17NEngine;
typedef struct _IBusM17NEngineClass IBusM17NEngineClass;

struct _IBusM17NEngine {
    IBusEngine parent;

    /* members */
    MInputContext *context;
    IBusLookupTable *table;

    IBusProperty    *status_prop;
#ifdef HAVE_SETUP
    IBusProperty    *setup_prop;
#endif  /* HAVE_SETUP */
    IBusProperty    *virtkbd_prop;
    IBusPropList    *prop_list;
};

struct _IBusM17NEngineClass {
    IBusEngineClass parent;

    /* configurations are per class */
    gchar *config_section;
    guint preedit_foreground;
    guint preedit_background;
    gint preedit_underline;
    gint lookup_table_orientation;
    gchar *virtual_keyboard;

    gchar *title;
    MInputMethod *im;

#ifdef HAVE_EEKBOARD
    EekboardContext *econtext;
    GSList *keyboards;
    GSList *keyboards_head;
#endif  /* HAVE_EEKBOARD */
};

/* functions prototype */
static void ibus_m17n_engine_class_init     (IBusM17NEngineClass    *klass);
static void ibus_m17n_config_value_changed  (IBusConfig             *config,
                                             const gchar            *section,
                                             const gchar            *name,
                                             GVariant               *value,
                                             IBusM17NEngineClass    *klass);

static GObject*
            ibus_m17n_engine_constructor    (GType                   type,
                                             guint                   n_construct_params,
                                             GObjectConstructParam  *construct_params);
static void ibus_m17n_engine_init           (IBusM17NEngine         *m17n);
static void ibus_m17n_engine_destroy        (IBusM17NEngine         *m17n);
static gboolean
            ibus_m17n_engine_process_key_event
                                            (IBusEngine             *engine,
                                             guint                   keyval,
                                             guint                   keycode,
                                             guint                   modifiers);
static void ibus_m17n_engine_focus_in       (IBusEngine             *engine);
static void ibus_m17n_engine_focus_out      (IBusEngine             *engine);
static void ibus_m17n_engine_reset          (IBusEngine             *engine);
static void ibus_m17n_engine_enable         (IBusEngine             *engine);
static void ibus_m17n_engine_disable        (IBusEngine             *engine);
static void ibus_engine_set_cursor_location (IBusEngine             *engine,
                                             gint                    x,
                                             gint                    y,
                                             gint                    w,
                                             gint                    h);
static void ibus_m17n_engine_set_capabilities
                                            (IBusEngine             *engine,
                                             guint                   caps);
static void ibus_m17n_engine_page_up        (IBusEngine             *engine);
static void ibus_m17n_engine_page_down      (IBusEngine             *engine);
static void ibus_m17n_engine_cursor_up      (IBusEngine             *engine);
static void ibus_m17n_engine_cursor_down    (IBusEngine             *engine);
static void ibus_m17n_engine_property_activate
                                            (IBusEngine             *engine,
                                             const gchar            *prop_name,
                                             guint                   prop_state);
static void ibus_m17n_engine_property_show
                                            (IBusEngine             *engine,
                                             const gchar            *prop_name);
static void ibus_m17n_engine_property_hide
                                            (IBusEngine             *engine,
                                             const gchar            *prop_name);

static void ibus_m17n_engine_commit_string
                                            (IBusM17NEngine         *m17n,
                                             const gchar            *string);
static void ibus_m17n_engine_callback       (MInputContext          *context,
                                             MSymbol                 command);
static void ibus_m17n_engine_update_preedit (IBusM17NEngine *m17n);
static void ibus_m17n_engine_update_lookup_table
                                            (IBusM17NEngine *m17n);

static IBusEngineClass *parent_class = NULL;

static IBusConfig      *config = NULL;

#ifdef HAVE_EEKBOARD
static EekboardClient  *eekboard = NULL;

static void
client_destroyed_cb (EekboardClient *client,
                     gpointer        user_data)
{
    if (eekboard) {
        g_object_unref (eekboard);
        eekboard = NULL;
    }
}

static void
context_destroyed_cb (EekboardContext *context,
                      IBusM17NEngineClass *klass)
{
    if (klass->econtext) {
        g_object_unref (klass->econtext);
        klass->econtext = NULL;
    }
}

static EekboardContext *
create_context (IBusM17NEngineClass *klass)
{
    EekboardContext *context = eekboard_client_create_context (eekboard,
                                                               "ibus-m17n",
                                                               NULL);
    g_signal_connect (context, "destroyed",
                      G_CALLBACK (context_destroyed_cb), klass);

    g_slist_free (klass->keyboards);
    klass->keyboards = NULL;

    gchar **keyboards = g_strsplit (klass->virtual_keyboard, ",", -1);
    gchar **p;

    for (p = keyboards; *p; p++) {
        guint keyboard = eekboard_context_add_keyboard (context,
                                                        g_strstrip (*p),
                                                        NULL);
        klass->keyboards = g_slist_prepend (klass->keyboards,
                                            GUINT_TO_POINTER (keyboard));
    }
    g_strfreev (keyboards);

    klass->keyboards = g_slist_reverse (klass->keyboards);
    klass->keyboards_head = klass->keyboards;

    eekboard_context_set_keyboard (context,
                                   GPOINTER_TO_UINT (klass->keyboards_head->data),
                                   NULL);
    return context;
}

static void
key_activated_cb (EekboardContext *context,
                  guint            keycode,
                  EekSymbol       *symbol,
                  guint            modifiers,
                  IBusM17NEngine  *m17n)
{
    IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);
    IBusEngine *engine = IBUS_ENGINE (m17n);

    if (EEK_IS_TEXT (symbol)) {
        const gchar *string;
        IBusText *text;

        string = eek_text_get_text (EEK_TEXT (symbol));
        text = ibus_text_new_from_static_string (string);
        ibus_engine_commit_text (engine, text);
    } else if (EEK_IS_KEYSYM (symbol)) {
        guint keyval = eek_keysym_get_xkeysym (EEK_KEYSYM (symbol));
        ibus_engine_forward_key_event (engine,
                                       keyval,
                                       0,
                                       modifiers);
        ibus_engine_forward_key_event (engine,
                                       keyval,
                                       0,
                                       modifiers | IBUS_RELEASE_MASK);
    } else if (g_strcmp0 (eek_symbol_get_name (symbol),
                          "cycle-keyboard") == 0) {
        klass->keyboards_head = g_slist_next (klass->keyboards_head);
        if (klass->keyboards_head == NULL)
            klass->keyboards_head = klass->keyboards;
        eekboard_context_set_keyboard (klass->econtext,
                                       GPOINTER_TO_UINT (klass->keyboards_head->data),
                                       NULL);
    }
}

static void
init_eekboard ()
{
    GDBusConnection *connection;
    GError *error;

    error = NULL;
    connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (connection == NULL) {
        g_printerr ("Can't connect to the session bus: %s\n",
                    error->message);
        g_error_free (error);
        return;
    }

    eek_init ();

    eekboard = eekboard_client_new (connection, NULL);
    g_object_unref (connection);

    g_signal_connect (eekboard, "destroyed",
                      G_CALLBACK (client_destroyed_cb), NULL);
}
#endif  /* HAVE_EEKBOARD */

static GType ibus_m17n_virtual_keyboard_implementation_type = 0;

typedef enum {
    IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_EEKBOARD,
    IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_IOK
} IBusM17NVirtualKeyboardImplementation;

#ifdef HAVE_EEKBOARD
static IBusM17NVirtualKeyboardImplementation virtual_keyboard_implementation =
    IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_EEKBOARD;
#else
static IBusM17NVirtualKeyboardImplementation virtual_keyboard_implementation =
    IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_IOK;
#endif

void
ibus_m17n_init (IBusBus *bus)
{
    static const GEnumValue evalues[] = {
        { IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_EEKBOARD,
          "IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_EEKBOARD",
          "eekboard" },
        { IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_IOK,
          "IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_IOK",
          "iok" }
    };

    GVariant *values;

    config = ibus_bus_get_config (bus);
    if (config)
        g_object_ref_sink (config);
    ibus_m17n_init_common ();

    ibus_m17n_virtual_keyboard_implementation_type =
        g_enum_register_static ("IBusM17NVirtualKeyboardImplementation",
                                evalues);
    values = ibus_config_get_values (config, "engine/M17N");
    if (values != NULL) {
        GVariant *value =
            g_variant_lookup_value (values,
                                    "virtual_keyboard_implementation",
                                    G_VARIANT_TYPE_STRING);
        if (value != NULL) {
            GEnumClass *eclass = G_ENUM_CLASS (g_type_class_peek (ibus_m17n_virtual_keyboard_implementation_type));
            GEnumValue *evalue = g_enum_get_value_by_nick (eclass, g_variant_get_string (value, NULL));
            if (evalue != NULL) {
                virtual_keyboard_implementation = evalue->value;
            }
        }
    }

    if (virtual_keyboard_implementation == IBUS_M17N_VIRTUAL_KEYBOARD_IMPLEMENTATION_EEKBOARD) {
#ifdef HAVE_EEKBOARD
        init_eekboard ();
#else
        g_warning ("eekboard is not supported");
#endif  /* HAVE_EEKBOARD */
    }
}

static gboolean
ibus_m17n_scan_engine_name (const gchar *engine_name,
                            gchar      **lang,
                            gchar      **name)
{
    gchar **strv;

    g_return_val_if_fail (g_str_has_prefix (engine_name, "m17n:"), FALSE);
    strv = g_strsplit (engine_name, ":", 3);

    if (g_strv_length (strv) != 3) {
        g_strfreev (strv);
        g_return_val_if_reached (FALSE);
    }

    *lang = strv[1];
    *name = strv[2];

    g_free (strv[0]);
    g_free (strv);

    return TRUE;
}

static gboolean
ibus_m17n_scan_class_name (const gchar *class_name,
                           gchar      **lang,
                           gchar      **name)
{
    gchar *p;

    g_return_val_if_fail (g_str_has_prefix (class_name, "IBusM17N"), FALSE);
    g_return_val_if_fail (g_str_has_suffix (class_name, "Engine"), FALSE);

    /* Strip prefix and suffix */
    p = *lang = g_strdup (class_name + 8);
    p = g_strrstr (p, "Engine");
    *p = '\0';

    /* Find the start position of <Name> */
    while (!g_ascii_isupper (*--p) && p > *lang)
        ;
    g_return_val_if_fail (p > *lang, FALSE);
    *name = g_strdup (p);
    *p = '\0';

    *lang[0] = g_ascii_tolower (*lang[0]);
    *name[0] = g_ascii_tolower (*name[0]);

    return TRUE;
}

GType
ibus_m17n_engine_get_type_for_name (const gchar *engine_name)
{
    GType type;
    gchar *type_name, *lang = NULL, *name = NULL;

    GTypeInfo type_info = {
        sizeof (IBusM17NEngineClass),
        (GBaseInitFunc)      NULL,
        (GBaseFinalizeFunc)  NULL,
        (GClassInitFunc)     ibus_m17n_engine_class_init,
        (GClassFinalizeFunc) NULL,
        NULL,
        sizeof (IBusM17NEngine),
        0,
        (GInstanceInitFunc)  ibus_m17n_engine_init,
    };

    if (!ibus_m17n_scan_engine_name (engine_name, &lang, &name)) {
        g_free (lang);
        g_free (name);
        return G_TYPE_INVALID;
    }
    lang[0] = g_ascii_toupper (lang[0]);
    name[0] = g_ascii_toupper (name[0]);
    type_name = g_strdup_printf ("IBusM17N%s%sEngine", lang, name);
    g_free (lang);
    g_free (name);

    type = g_type_from_name (type_name);
    g_assert (type == 0 || g_type_is_a (type, IBUS_TYPE_ENGINE));

    if (type == 0) {
        type = g_type_register_static (IBUS_TYPE_ENGINE,
                                       type_name,
                                       &type_info,
                                       (GTypeFlags) 0);
    }
    g_free (type_name);

    return type;
}

static void
ibus_m17n_engine_class_init (IBusM17NEngineClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    IBusObjectClass *ibus_object_class = IBUS_OBJECT_CLASS (klass);
    IBusEngineClass *engine_class = IBUS_ENGINE_CLASS (klass);
    gchar *engine_name, *lang = NULL, *name = NULL;
    IBusM17NEngineConfig *engine_config;
    GVariant *values;

    if (parent_class == NULL)
        parent_class = (IBusEngineClass *) g_type_class_peek_parent (klass);

    object_class->constructor = ibus_m17n_engine_constructor;
    ibus_object_class->destroy = (IBusObjectDestroyFunc) ibus_m17n_engine_destroy;

    engine_class->process_key_event = ibus_m17n_engine_process_key_event;

    engine_class->reset = ibus_m17n_engine_reset;
    engine_class->enable = ibus_m17n_engine_enable;
    engine_class->disable = ibus_m17n_engine_disable;

    engine_class->focus_in = ibus_m17n_engine_focus_in;
    engine_class->focus_out = ibus_m17n_engine_focus_out;

    engine_class->page_up = ibus_m17n_engine_page_up;
    engine_class->page_down = ibus_m17n_engine_page_down;

    engine_class->cursor_up = ibus_m17n_engine_cursor_up;
    engine_class->cursor_down = ibus_m17n_engine_cursor_down;

    engine_class->property_activate = ibus_m17n_engine_property_activate;

    if (!ibus_m17n_scan_class_name (G_OBJECT_CLASS_NAME (klass),
                                    &lang, &name)) {
        g_free (lang);
        g_free (name);
        return;
    }
    engine_name = g_strdup_printf ("m17n:%s:%s", lang, name);
    klass->config_section = g_strdup_printf ("engine/M17N/%s/%s", lang, name);
    MPlist *l = minput_get_title_icon (msymbol (lang), msymbol (name));
    if (l && mplist_key (l) == Mtext) {
        klass->title = ibus_m17n_mtext_to_utf8 (mplist_value (l));
    }
    g_free (lang);
    g_free (name);

    engine_config = ibus_m17n_get_engine_config (engine_name);
    g_free (engine_name);

    /* configurations are per class */
    klass->preedit_foreground = engine_config->preedit_highlight ?
        PREEDIT_FOREGROUND :
        INVALID_COLOR;
    klass->preedit_background = engine_config->preedit_highlight ?
        PREEDIT_BACKGROUND :
        INVALID_COLOR;
    klass->preedit_underline = IBUS_ATTR_UNDERLINE_NONE;
    klass->lookup_table_orientation = IBUS_ORIENTATION_SYSTEM;
    klass->virtual_keyboard = engine_config->virtual_keyboard;

    ibus_m17n_engine_config_free (engine_config);

    values = ibus_config_get_values (config,
                                     klass->config_section);
    if (values != NULL) {
        GVariant *value;

        value = g_variant_lookup_value (values,
                                        "preedit_foreground",
                                        G_VARIANT_TYPE_STRING);
        if (value != NULL) {
            const gchar *hex = g_variant_get_string (value, NULL);
            klass->preedit_foreground = ibus_m17n_parse_color (hex);
            g_variant_unref (value);
        }

        value = g_variant_lookup_value (values,
                                        "preedit_background",
                                        G_VARIANT_TYPE_STRING);
        if (value != NULL) {
            const gchar *hex = g_variant_get_string (value, NULL);
            klass->preedit_background = ibus_m17n_parse_color (hex);
            g_variant_unref (value);
        }

        value = g_variant_lookup_value (values,
                                        "preedit_underline",
                                        G_VARIANT_TYPE_INT32);
        if (value != NULL) {
            klass->preedit_background = g_variant_get_int32 (value);
            g_variant_unref (value);
        }

        value = g_variant_lookup_value (values,
                                        "lookup_table_orientation",
                                        G_VARIANT_TYPE_INT32);
        if (value != NULL) {
            klass->lookup_table_orientation = g_variant_get_int32 (value);
            g_variant_unref (value);
        }
        g_variant_unref (values);
    }

    g_signal_connect (config, "value-changed",
                      G_CALLBACK(ibus_m17n_config_value_changed),
                      klass);

    klass->im = NULL;

#if HAVE_EEKBOARD
    if (eekboard)
        klass->econtext = create_context (klass);
#endif  /* HAVE_EEKBOARD */
}

static void
ibus_m17n_config_value_changed (IBusConfig          *config,
                                const gchar         *section,
                                const gchar         *name,
                                GVariant            *value,
                                IBusM17NEngineClass *klass)
{
    if (g_strcmp0 (section, klass->config_section) == 0) {
        if (g_strcmp0 (name, "preedit_foreground") == 0) {
            const gchar *hex = g_variant_get_string (value, NULL);
            guint color;
            color = ibus_m17n_parse_color (hex);
            if (color != INVALID_COLOR) {
                klass->preedit_foreground = color;
            }
        } else if (g_strcmp0 (name, "preedit_background") == 0) {
            const gchar *hex = g_variant_get_string (value, NULL);
            guint color;
            color = ibus_m17n_parse_color (hex);
            if (color != INVALID_COLOR) {
                klass->preedit_background = color;
            }
        } else if (g_strcmp0 (name, "preedit_underline") == 0) {
            klass->preedit_underline = g_variant_get_int32 (value);
        } else if (g_strcmp0 (name, "lookup_table_orientation") == 0) {
            klass->lookup_table_orientation = g_variant_get_int32 (value);
        }
    }
}

static void
ibus_m17n_engine_init (IBusM17NEngine *m17n)
{
    IBusText* label;
    IBusText* tooltip;
    IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);

    m17n->prop_list = ibus_prop_list_new ();
    g_object_ref_sink (m17n->prop_list);

    m17n->status_prop = ibus_property_new ("status",
                                           PROP_TYPE_NORMAL,
                                           NULL,
                                           NULL,
                                           NULL,
                                           TRUE,
                                           FALSE,
                                           0,
                                           NULL);
    g_object_ref_sink (m17n->status_prop);
    ibus_prop_list_append (m17n->prop_list,  m17n->status_prop);

#ifdef HAVE_SETUP
    label = ibus_text_new_from_string ("Setup");
    tooltip = ibus_text_new_from_string ("Configure M17N engine");
    m17n->setup_prop = ibus_property_new ("setup",
                                          PROP_TYPE_NORMAL,
                                          label,
                                          "gtk-preferences",
                                          tooltip,
                                          TRUE,
                                          TRUE,
                                          PROP_STATE_UNCHECKED,
                                          NULL);
    g_object_ref_sink (m17n->setup_prop);
    ibus_prop_list_append (m17n->prop_list, m17n->setup_prop);
#endif  /* HAVE_SETUP */

    label = ibus_text_new_from_string ("Screen Keyboard");
    tooltip = ibus_text_new_from_string ("Show screen keyboard");
    m17n->virtkbd_prop = ibus_property_new ("virtual-keyboard",
                                            PROP_TYPE_NORMAL,
                                            label,
                                            "input-keyboard",
                                            tooltip,
                                            TRUE,
                                            FALSE,
                                            PROP_STATE_UNCHECKED,
                                            NULL);
    g_object_ref_sink (m17n->virtkbd_prop);
    ibus_prop_list_append (m17n->prop_list, m17n->virtkbd_prop);

#ifdef HAVE_EEKBOARD
    if (eekboard != NULL)
        ibus_property_set_visible (m17n->virtkbd_prop, TRUE);
    else
#endif  /* HAVE_EEKBOARD */
    {
        gchar *lang = NULL, *name = NULL;
        if (ibus_m17n_scan_class_name (G_OBJECT_CLASS_NAME (klass),
                                       &lang,
                                       &name) &&
            g_str_has_prefix (name, "inscript"))
            ibus_property_set_visible (m17n->virtkbd_prop, TRUE);
        g_free (lang);
        g_free (name);
    }

    m17n->table = ibus_lookup_table_new (9, 0, TRUE, TRUE);
    g_object_ref_sink (m17n->table);
    m17n->context = NULL;
}

static GObject*
ibus_m17n_engine_constructor (GType                   type,
                              guint                   n_construct_params,
                              GObjectConstructParam  *construct_params)
{
    IBusM17NEngine *m17n;
    GObjectClass *object_class;
    IBusM17NEngineClass *klass;

    m17n = (IBusM17NEngine *) G_OBJECT_CLASS (parent_class)->constructor (type,
                                                       n_construct_params,
                                                       construct_params);

    object_class = G_OBJECT_GET_CLASS (m17n);
    klass = (IBusM17NEngineClass *) object_class;
    if (klass->im == NULL) {
        const gchar *engine_name;
        gchar *lang = NULL, *name = NULL;

        engine_name = ibus_engine_get_name ((IBusEngine *) m17n);
        if (!ibus_m17n_scan_engine_name (engine_name, &lang, &name)) {
            g_free (lang);
            g_free (name);
            return NULL;
        }

        klass->im = minput_open_im (msymbol (lang), msymbol (name), NULL);
        g_free (lang);
        g_free (name);

        if (klass->im == NULL) {
            g_warning ("Can not find m17n keymap %s", engine_name);
            g_object_unref (m17n);
            return NULL;
        }

        mplist_put (klass->im->driver.callback_list, Minput_preedit_start, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_preedit_draw, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_preedit_done, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_status_start, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_status_draw, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_status_done, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_candidates_start, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_candidates_draw, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_candidates_done, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_set_spot, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_toggle, ibus_m17n_engine_callback);
        /*
          Does not set reset callback, uses the default callback in m17n.
          mplist_put (klass->im->driver.callback_list, Minput_reset, ibus_m17n_engine_callback);
        */
        mplist_put (klass->im->driver.callback_list, Minput_get_surrounding_text, ibus_m17n_engine_callback);
        mplist_put (klass->im->driver.callback_list, Minput_delete_surrounding_text, ibus_m17n_engine_callback);
    }

    m17n->context = minput_create_ic (klass->im, m17n);

    return (GObject *) m17n;
}

static void
ibus_m17n_engine_destroy (IBusM17NEngine *m17n)
{
    if (m17n->prop_list) {
        g_object_unref (m17n->prop_list);
        m17n->prop_list = NULL;
    }

    if (m17n->status_prop) {
        g_object_unref (m17n->status_prop);
        m17n->status_prop = NULL;
    }

#if HAVE_SETUP
    if (m17n->setup_prop) {
        g_object_unref (m17n->setup_prop);
        m17n->setup_prop = NULL;
    }
#endif  /* HAVE_SETUP */

    if (m17n->virtkbd_prop) {
        g_object_unref (m17n->virtkbd_prop);
        m17n->virtkbd_prop = NULL;
    }

    if (m17n->table) {
        g_object_unref (m17n->table);
        m17n->table = NULL;
    }

    if (m17n->context) {
        minput_destroy_ic (m17n->context);
        m17n->context = NULL;
    }

    IBUS_OBJECT_CLASS (parent_class)->destroy ((IBusObject *)m17n);
}

static void
ibus_m17n_engine_update_preedit (IBusM17NEngine *m17n)
{
    IBusText *text;
    gchar *buf;
    IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);

    buf = ibus_m17n_mtext_to_utf8 (m17n->context->preedit);
    if (buf) {
        text = ibus_text_new_from_static_string (buf);
        if (klass->preedit_foreground != INVALID_COLOR)
            ibus_text_append_attribute (text, IBUS_ATTR_TYPE_FOREGROUND,
                                        klass->preedit_foreground, 0, -1);
        if (klass->preedit_background != INVALID_COLOR)
            ibus_text_append_attribute (text, IBUS_ATTR_TYPE_BACKGROUND,
                                        klass->preedit_background, 0, -1);
        ibus_text_append_attribute (text, IBUS_ATTR_TYPE_UNDERLINE,
                                    klass->preedit_underline, 0, -1);
        ibus_engine_update_preedit_text ((IBusEngine *) m17n,
                                         text,
                                         m17n->context->cursor_pos,
                                         mtext_len (m17n->context->preedit) > 0);
    }
}

static void
ibus_m17n_engine_commit_string (IBusM17NEngine *m17n,
                                const gchar    *string)
{
    IBusText *text;
    text = ibus_text_new_from_static_string (string);
    ibus_engine_commit_text ((IBusEngine *)m17n, text);
    ibus_m17n_engine_update_preedit (m17n);
}

/* Note on AltGr (Level3 Shift) handling: While currently we expect
   AltGr == mod5, it would be better to not expect the modifier always
   be assigned to particular modX.  However, it needs some code like:

   KeyCode altgr = XKeysymToKeycode (display, XK_ISO_Level3_Shift);
   XModifierKeymap *mods = XGetModifierMapping (display);
   for (i = 3; i < 8; i++)
     for (j = 0; j < mods->max_keypermod; j++) {
       KeyCode code = mods->modifiermap[i * mods->max_keypermod + j];
       if (code == altgr)
         ...
     }
                
   Since IBus engines are supposed to be cross-platform, the code
   should go into IBus core, instead of ibus-m17n. */
static MSymbol
ibus_m17n_key_event_to_symbol (guint keycode,
                               guint keyval,
                               guint modifiers)
{
    GString *keysym;
    MSymbol mkeysym = Mnil;
    guint mask = 0;
    IBusKeymap *keymap;

    if (keyval >= IBUS_Shift_L && keyval <= IBUS_Hyper_R) {
        return Mnil;
    }

    /* Here, keyval is already translated by IBUS_MOD5_MASK.  Obtain
       the untranslated keyval from the underlying keymap and
       represent the translated keyval as the form "G-<untranslated
       keyval>", which m17n-lib accepts. */
    if (modifiers & IBUS_MOD5_MASK) {
        keymap = ibus_keymap_get ("us");
        keyval = ibus_keymap_lookup_keysym (keymap, keycode,
                                            modifiers & ~IBUS_MOD5_MASK);
        g_object_unref (keymap);
    }

    keysym = g_string_new ("");

    if (keyval >= IBUS_space && keyval <= IBUS_asciitilde) {
        gint c = keyval;

        if (keyval == IBUS_space && modifiers & IBUS_SHIFT_MASK)
            mask |= IBUS_SHIFT_MASK;

        if (modifiers & IBUS_CONTROL_MASK) {
            if (c >= IBUS_a && c <= IBUS_z)
                c += IBUS_A - IBUS_a;
            mask |= IBUS_CONTROL_MASK;
        }

        g_string_append_c (keysym, c);
    }
    else {
        const gchar *name = ibus_keyval_name (keyval);
        if (name == NULL) {
            g_string_free (keysym, TRUE);
            return Mnil;
        }
        mask |= modifiers & (IBUS_CONTROL_MASK | IBUS_SHIFT_MASK);
        g_string_append (keysym, name);
    }

    mask |= modifiers & (IBUS_MOD1_MASK |
                         IBUS_MOD5_MASK |
                         IBUS_META_MASK |
                         IBUS_SUPER_MASK |
                         IBUS_HYPER_MASK);


    if (mask & IBUS_HYPER_MASK) {
        g_string_prepend (keysym, "H-");
    }
    if (mask & IBUS_SUPER_MASK) {
        g_string_prepend (keysym, "s-");
    }
    if (mask & IBUS_MOD5_MASK) {
        g_string_prepend (keysym, "G-");
    }
    if (mask & IBUS_MOD1_MASK) {
        g_string_prepend (keysym, "A-");
    }
    if (mask & IBUS_META_MASK) {
        g_string_prepend (keysym, "M-");
    }
    if (mask & IBUS_CONTROL_MASK) {
        g_string_prepend (keysym, "C-");
    }
    if (mask & IBUS_SHIFT_MASK) {
        g_string_prepend (keysym, "S-");
    }

    mkeysym = msymbol (keysym->str);
    g_string_free (keysym, TRUE);

    return mkeysym;
}

static gboolean
ibus_m17n_engine_process_key (IBusM17NEngine *m17n,
                              MSymbol         key)
{
    gchar *buf;
    MText *produced;
    gint retval;

    retval = minput_filter (m17n->context, key, NULL);

    if (retval) {
        return TRUE;
    }

    produced = mtext ();

    retval = minput_lookup (m17n->context, key, NULL, produced);

    if (retval) {
        // g_debug ("minput_lookup returns %d", retval);
    }

    buf = ibus_m17n_mtext_to_utf8 (produced);
    m17n_object_unref (produced);

    if (buf && strlen (buf)) {
        ibus_m17n_engine_commit_string (m17n, buf);
    }
    g_free (buf);

    return retval == 0;
}

static gboolean
ibus_m17n_engine_process_key_event (IBusEngine     *engine,
                                    guint           keyval,
                                    guint           keycode,
                                    guint           modifiers)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    if (modifiers & IBUS_RELEASE_MASK)
        return FALSE;
    MSymbol m17n_key = ibus_m17n_key_event_to_symbol (keycode, keyval, modifiers);

    if (m17n_key == Mnil)
        return FALSE;

    return ibus_m17n_engine_process_key (m17n, m17n_key);
}

static void
ibus_m17n_engine_focus_in (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_engine_register_properties (engine, m17n->prop_list);
    ibus_m17n_engine_process_key (m17n, Minput_focus_in);

    parent_class->focus_in (engine);
}

static void
ibus_m17n_engine_focus_out (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_process_key (m17n, Minput_focus_out);

    parent_class->focus_out (engine);
}

static void
ibus_m17n_engine_reset (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    parent_class->reset (engine);

    minput_reset_ic (m17n->context);
}

static void
ibus_m17n_engine_enable (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    parent_class->enable (engine);

    /* Issue a dummy ibus_engine_get_surrounding_text() call to tell
       input context that we will use surrounding-text. */
    ibus_engine_get_surrounding_text (engine, NULL, NULL, NULL);

#ifdef HAVE_EEKBOARD
    if (eekboard) {
        IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);
        if (klass->econtext)
            eekboard_client_push_context (eekboard, klass->econtext, NULL);
    }
#endif  /* HAVE_EEKBOARD */
}

static void
ibus_m17n_engine_disable (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_focus_out (engine);
    parent_class->disable (engine);

#ifdef HAVE_EEKBOARD
    if (eekboard) {
        IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);
        if (klass->econtext)
            eekboard_client_pop_context (eekboard, NULL);
    }
#endif  /* HAVE_EEKBOARD */
}

static void
ibus_m17n_engine_page_up (IBusEngine *engine)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_process_key (m17n, msymbol ("Up"));
    parent_class->page_up (engine);
}

static void
ibus_m17n_engine_page_down (IBusEngine *engine)
{

    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_process_key (m17n, msymbol ("Down"));
    parent_class->page_down (engine);
}

static void
ibus_m17n_engine_cursor_up (IBusEngine *engine)
{

    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_process_key (m17n, msymbol ("Left"));
    parent_class->cursor_up (engine);
}

static void
ibus_m17n_engine_cursor_down (IBusEngine *engine)
{

    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;

    ibus_m17n_engine_process_key (m17n, msymbol ("Right"));
    parent_class->cursor_down (engine);
}

static void
ibus_m17n_engine_property_activate (IBusEngine  *engine,
                                    const gchar *prop_name,
                                    guint        prop_state)
{
    IBusM17NEngine *m17n = (IBusM17NEngine *) engine;
    IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);

#ifdef HAVE_SETUP
    if (g_strcmp0 (prop_name, "setup") == 0) {
        const gchar *engine_name;
        gchar *setup;

        engine_name = ibus_engine_get_name ((IBusEngine *) m17n);
        g_assert (engine_name);
        setup = g_strdup_printf ("%s/ibus-setup-m17n --name %s",
                                 LIBEXECDIR, engine_name);
        g_spawn_command_line_async (setup, NULL);
        g_free (setup);
    }
#endif  /* HAVE_SETUP */

    if (g_strcmp0 (prop_name, "virtual-keyboard") == 0) {
#ifdef HAVE_EEKBOARD
        if (eekboard) {
            if (klass->econtext == NULL) {
                klass->econtext = create_context (klass);
                eekboard_client_push_context (eekboard, klass->econtext, NULL);
            }
            g_signal_handlers_disconnect_by_func (klass->econtext,
                                                  G_CALLBACK (key_activated_cb),
                                                  m17n);
            g_signal_connect (klass->econtext, "key-activated",
                              G_CALLBACK (key_activated_cb), m17n);
            eekboard_context_show_keyboard (klass->econtext, NULL);
        } else
#endif  /* HAVE_EEKBOARD */
        {
            gchar *lang = NULL, *name = NULL;

            if (ibus_m17n_scan_class_name (G_OBJECT_CLASS_NAME (klass),
                                           &lang,
                                           &name) &&
                g_str_has_prefix (name, "inscript")) {
                gchar *argv[4];
                GError *error;

                argv[0] = "iok";
                argv[1] = "-n";
                argv[2] = lang;
                argv[3] = NULL;
                error = NULL;
                if (!g_spawn_async (NULL,
                                    argv,
                                    NULL,
                                    G_SPAWN_SEARCH_PATH,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &error)) {
                    g_warning ("can't spawn iok: %s", error->message);
                    g_error_free (error);
                }
            }
            g_free (lang);
            g_free (name);
        }
    }

    parent_class->property_activate (engine, prop_name, prop_state);
}

static void
ibus_m17n_engine_update_lookup_table (IBusM17NEngine *m17n)
{
    ibus_lookup_table_clear (m17n->table);

    if (m17n->context->candidate_list && m17n->context->candidate_show) {
        IBusText *text;
        MPlist *group;
        group = m17n->context->candidate_list;
        gint i = 0;
        gint page = 1;
        IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);

        while (1) {
            gint len;
            if (mplist_key (group) == Mtext)
                len = mtext_len ((MText *) mplist_value (group));
            else
                len = mplist_length ((MPlist *) mplist_value (group));

            if (i + len > m17n->context->candidate_index)
                break;

            i += len;
            group = mplist_next (group);
            page ++;
        }

        if (mplist_key (group) == Mtext) {
            MText *mt;
            gunichar *buf;
            glong nchars, i;

            mt = (MText *) mplist_value (group);
            ibus_lookup_table_set_page_size (m17n->table, mtext_len (mt));

            buf = ibus_m17n_mtext_to_ucs4 (mt, &nchars);
            g_warn_if_fail (buf != NULL);

            for (i = 0; buf != NULL && i < nchars; i++) {
                IBusText *text = ibus_text_new_from_unichar (buf[i]);
                if (text == NULL) {
                    text = ibus_text_new_from_printf ("INVCODE=U+%04"G_GINT32_FORMAT"X", buf[i]);
                    g_warn_if_reached ();
                }
                ibus_lookup_table_append_candidate (m17n->table, text);
            }
            g_free (buf);
        }
        else {
            MPlist *p;

            p = (MPlist *) mplist_value (group);
            ibus_lookup_table_set_page_size (m17n->table, mplist_length (p));

            for (; mplist_key (p) != Mnil; p = mplist_next (p)) {
                MText *mtext;
                gchar *buf;

                mtext = (MText *) mplist_value (p);
                buf = ibus_m17n_mtext_to_utf8 (mtext);
                if (buf) {
                    ibus_lookup_table_append_candidate (m17n->table,
                        ibus_text_new_from_string (buf));
                    g_free (buf);
                }
                else {
                    ibus_lookup_table_append_candidate (m17n->table,
                        ibus_text_new_from_static_string ("NULL"));
                    g_warn_if_reached();
                }
            }
        }

        ibus_lookup_table_set_cursor_pos (m17n->table, m17n->context->candidate_index - i);
        ibus_lookup_table_set_orientation (m17n->table, klass->lookup_table_orientation);

        text = ibus_text_new_from_printf ("( %d / %d )", page, mplist_length (m17n->context->candidate_list));

        ibus_engine_update_lookup_table ((IBusEngine *)m17n, m17n->table, TRUE);
        ibus_engine_update_auxiliary_text ((IBusEngine *)m17n, text, TRUE);
    }
    else {
        ibus_engine_hide_lookup_table ((IBusEngine *)m17n);
        ibus_engine_hide_auxiliary_text ((IBusEngine *)m17n);
    }
}

static void
ibus_m17n_engine_callback (MInputContext *context,
                           MSymbol        command)
{
    IBusM17NEngine *m17n = NULL;

    m17n = context->arg;
    g_return_if_fail (m17n != NULL);

    /* the callback may be called in minput_create_ic, in the time
     * m17n->context has not be assigned, so need assign it. */
    if (m17n->context == NULL) {
        m17n->context = context;
    }

    if (command == Minput_preedit_start) {
        ibus_engine_hide_preedit_text ((IBusEngine *)m17n);
    }
    else if (command == Minput_preedit_draw) {
        ibus_m17n_engine_update_preedit (m17n);
    }
    else if (command == Minput_preedit_done) {
        ibus_engine_hide_preedit_text ((IBusEngine *)m17n);
    }
    else if (command == Minput_status_start) {
        ibus_engine_hide_preedit_text ((IBusEngine *)m17n);
    }
    else if (command == Minput_status_draw) {
        gchar *status;
        status = ibus_m17n_mtext_to_utf8 (m17n->context->status);
        IBusM17NEngineClass *klass = (IBusM17NEngineClass *) G_OBJECT_GET_CLASS (m17n);

        if (status && strlen (status) && g_strcmp0 (status, klass->title)) {
            IBusText *text;
            text = ibus_text_new_from_string (status);
            ibus_property_set_label (m17n->status_prop, text);
            ibus_property_set_visible (m17n->status_prop, TRUE);
        }
        else {
            ibus_property_set_label (m17n->status_prop, NULL);
            ibus_property_set_visible (m17n->status_prop, FALSE);
        }

        ibus_engine_update_property ((IBusEngine *)m17n, m17n->status_prop);
        g_free (status);
    }
    else if (command == Minput_status_done) {
    }
    else if (command == Minput_candidates_start) {
        ibus_engine_hide_lookup_table ((IBusEngine *) m17n);
        ibus_engine_hide_auxiliary_text ((IBusEngine *) m17n);
    }
    else if (command == Minput_candidates_draw) {
        ibus_m17n_engine_update_lookup_table (m17n);
    }
    else if (command == Minput_candidates_done) {
        ibus_engine_hide_lookup_table ((IBusEngine *) m17n);
        ibus_engine_hide_auxiliary_text ((IBusEngine *) m17n);
    }
    else if (command == Minput_set_spot) {
    }
    else if (command == Minput_toggle) {
    }
    else if (command == Minput_reset) {
    }
    else if (command == Minput_get_surrounding_text &&
             (((IBusEngine *) m17n)->client_capabilities &
              IBUS_CAP_SURROUNDING_TEXT) != 0) {
        IBusText *text;
        guint cursor_pos, anchor_pos, nchars, nbytes;
        MText *mt, *surround;
        int len, pos;

        ibus_engine_get_surrounding_text ((IBusEngine *) m17n,
                                          &text,
                                          &cursor_pos,
                                          &anchor_pos);
        nchars = ibus_text_get_length (text);
        nbytes = g_utf8_offset_to_pointer (text->text, nchars) - text->text;
        mt = mconv_decode_buffer (Mcoding_utf_8, text->text, nbytes);
        g_object_unref (text);

        len = (long) mplist_value (m17n->context->plist);
        if (len < 0) {
            pos = cursor_pos + len;
            if (pos < 0)
                pos = 0;
            surround = mtext_duplicate (mt, pos, cursor_pos);
        }
        else if (len > 0) {
            pos = cursor_pos + len;
            if (pos > nchars)
                pos = nchars;
            surround = mtext_duplicate (mt, cursor_pos, pos);
        }
        else {
            surround = mtext ();
        }
        m17n_object_unref (mt);
        mplist_set (m17n->context->plist, Mtext, surround);
        m17n_object_unref (surround);
    }
    else if (command == Minput_delete_surrounding_text &&
             (((IBusEngine *) m17n)->client_capabilities &
              IBUS_CAP_SURROUNDING_TEXT) != 0) {
        int len;

        len = (long) mplist_value (m17n->context->plist);
        if (len < 0)
            ibus_engine_delete_surrounding_text ((IBusEngine *) m17n,
                                                 len, -len);
        else if (len > 0)
            ibus_engine_delete_surrounding_text ((IBusEngine *) m17n,
                                                 0, len);
    }
}
