#include "async.h"

struct _ChandlerAsync
{
  GObject *owner; /* nullptr once disposed (weak reference) */
  GCancellable *cancellable;
  ChandlerAsyncFunc fn;
  void *data;
  GDestroyNotify destroy;
  const char *name;
};

static int live;
static gint64 stall_limit_us = -1; /* -1: GBK_STALL_MS not read yet; 0: off */

static gint64
stall_limit (void)
{
  if (stall_limit_us < 0)
    {
      stall_limit_us = 0;
      const char *v = g_getenv ("GBK_STALL_MS");
      guint64 ms = 0;
      if (v != nullptr && !g_ascii_string_to_unsigned (v, 10, 1, 600000, &ms, nullptr))
        gbk_fatal ("GBK_STALL_MS must be a whole number of milliseconds, 1 to 600000");
      stall_limit_us = (gint64) ms * G_TIME_SPAN_MILLISECOND;
    }
  return stall_limit_us;
}

static void
owner_gone (void *data, GObject *was G_GNUC_UNUSED)
{
  ChandlerAsync *a = data;
  a->owner = nullptr;
  g_cancellable_cancel (a->cancellable);
}

ChandlerAsync *
chandler_async_new_named (GObject *owner, ChandlerAsyncFunc fn, void *data, GDestroyNotify destroy,
                          const char *name)
{
  if (owner == nullptr || fn == nullptr)
    gbk_fatal ("chandler_async_new: %s without an owner or a function", name);
  ChandlerAsync *a = g_new0 (ChandlerAsync, 1);
  a->owner = owner;
  a->cancellable = g_cancellable_new ();
  a->fn = fn;
  a->data = data;
  a->destroy = destroy;
  a->name = name;
  g_object_weak_ref (owner, owner_gone, a);
  live++;
  return a;
}

GCancellable *
chandler_async_cancellable (const ChandlerAsync *async)
{
  return async->cancellable;
}

void
chandler_async_ready (GObject *source, GAsyncResult *result, void *async)
{
  ChandlerAsync *a = async;
  GObject *owner = a->owner;
  if (owner != nullptr)
    {
      g_object_weak_unref (owner, owner_gone, a);
      g_object_ref (owner);
    }
  gint64 limit = stall_limit ();
  gint64 start = limit > 0 ? g_get_monotonic_time () : 0;
  a->fn (source, result, owner, a->data);
  if (limit > 0)
    {
      gint64 took = g_get_monotonic_time () - start;
      if (took > limit)
        gbk_fatal ("main loop blocked for %" G_GINT64_FORMAT " ms (GBK_STALL_MS=%" G_GINT64_FORMAT
                   ") by async completion %s",
                   took / G_TIME_SPAN_MILLISECOND, limit / G_TIME_SPAN_MILLISECOND, a->name);
    }
  if (owner != nullptr)
    g_object_unref (owner);
  if (a->destroy != nullptr)
    a->destroy (a->data);
  g_object_unref (a->cancellable);
  g_free (a);
  live--;
}

int
chandler_async_live (void)
{
  return live;
}
