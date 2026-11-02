#include "icon.h"
#include "async.h"

#define GENERIC_ICON "application-x-executable"
#define SOURCE_KEY "chandler-icon-source"

typedef enum
{
  SOURCE_GENERIC = 1, /* nonzero: 0 is "no data" */
  SOURCE_STOCK,
  SOURCE_CACHED,
} Source;

static void
set_source (GtkWidget *image, Source source)
{
  g_object_set_data (G_OBJECT (image), SOURCE_KEY, GINT_TO_POINTER (source));
}

const char *
chandler_icon_source_for_test (GtkWidget *image)
{
  int source = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (image), SOURCE_KEY));
  if (source == SOURCE_GENERIC)
    return "generic";
  if (source == SOURCE_STOCK)
    return "stock";
  if (source == SOURCE_CACHED)
    return "cached";
  gbk_fatal ("an image without an icon source");
}

static void
on_pixbuf (GObject *source G_GNUC_UNUSED, GAsyncResult *result, GObject *owner,
           void *data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = nullptr;
  g_autoptr (GdkPixbuf) pixbuf = gdk_pixbuf_new_from_stream_finish (result, &error);
  if (owner == nullptr)
    return;
  if (pixbuf == nullptr)
    {
      g_debug ("cached icon unreadable: %s %d", g_quark_to_string (error->domain), error->code);
      return;
    }
  gtk_image_set_from_pixbuf (GTK_IMAGE (owner), pixbuf);
  set_source (GTK_WIDGET (owner), SOURCE_CACHED);
}

static void
on_opened (GObject *source, GAsyncResult *result, GObject *owner, void *data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = nullptr;
  g_autoptr (GFileInputStream) stream = g_file_read_finish (G_FILE (source), result, &error);
  if (owner == nullptr)
    return;
  if (stream == nullptr)
    {
      g_debug ("cached icon unopenable: %s %d", g_quark_to_string (error->domain), error->code);
      return;
    }
  ChandlerAsync *a = chandler_async_new (owner, on_pixbuf, nullptr, nullptr);
  gdk_pixbuf_new_from_stream_at_scale_async (
      G_INPUT_STREAM (stream), CHANDLER_ICON_SIZE, CHANDLER_ICON_SIZE, true,
      chandler_async_cancellable (a), chandler_async_ready, a);
}

GtkWidget *
chandler_icon_new (const ChandlerApp *app)
{
  GtkWidget *image = gtk_image_new ();
  gtk_image_set_pixel_size (GTK_IMAGE (image), CHANDLER_ICON_SIZE);
  gtk_widget_set_valign (image, GTK_ALIGN_START);
  if (app->icon_stock != nullptr
      && gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), app->icon_stock))
    {
      gtk_image_set_from_icon_name (GTK_IMAGE (image), app->icon_stock, GTK_ICON_SIZE_DIALOG);
      set_source (image, SOURCE_STOCK);
      return image;
    }
  gtk_image_set_from_icon_name (GTK_IMAGE (image), GENERIC_ICON, GTK_ICON_SIZE_DIALOG);
  set_source (image, SOURCE_GENERIC);
  if (app->icon_cached != nullptr)
    {
      g_autoptr (GFile) file = g_file_new_for_path (app->icon_cached);
      ChandlerAsync *a = chandler_async_new (G_OBJECT (image), on_opened, nullptr, nullptr);
      g_file_read_async (file, G_PRIORITY_LOW, chandler_async_cancellable (a), chandler_async_ready,
                         a);
    }
  return image;
}
