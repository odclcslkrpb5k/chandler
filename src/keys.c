#include "keys.h"
#include "lifetime.h"

#define KEYS_KEY "chandler-keys"

typedef struct
{
  GArray *down; /* guint16 hardware keycodes */
  bool repeat;  /* the press being delivered */
} Keys;

static void
keys_free (Keys *keys)
{
  g_array_unref (keys->down);
  g_free (keys);
}

static bool
find (const Keys *keys, guint16 code, guint *at)
{
  for (guint i = 0; i < keys->down->len; i++)
    if (g_array_index (keys->down, guint16, i) == code)
      {
        *at = i;
        return true;
      }
  return false;
}

static gboolean
on_press (GtkWidget *window G_GNUC_UNUSED, GdkEventKey *event, void *data)
{
  Keys *keys = data;
  guint at = 0;
  keys->repeat = find (keys, event->hardware_keycode, &at);
  if (!keys->repeat)
    g_array_append_val (keys->down, event->hardware_keycode);
  return false;
}

static gboolean
on_release (GtkWidget *window G_GNUC_UNUSED, GdkEventKey *event, void *data)
{
  Keys *keys = data;
  guint at = 0;
  if (find (keys, event->hardware_keycode, &at))
    g_array_remove_index_fast (keys->down, at);
  keys->repeat = false;
  return false;
}

void
chandler_keys_watch (GtkWindow *window)
{
  Keys *keys = g_new0 (Keys, 1);
  keys->down = g_array_new (false, false, sizeof (guint16));
  g_object_set_data_full (G_OBJECT (window), KEYS_KEY, keys, (GDestroyNotify) keys_free);
  /* A window's own handlers run before GtkWindow hands the event to its focused widget. */
  gbk_connect (G_OBJECT (window), window, "key-press-event", G_CALLBACK (on_press), keys);
  gbk_connect (G_OBJECT (window), window, "key-release-event", G_CALLBACK (on_release), keys);
}

bool
chandler_keys_is_repeat (GtkWidget *widget)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  const Keys *keys = g_object_get_data (G_OBJECT (toplevel), KEYS_KEY);
  return keys != nullptr && keys->repeat;
}
