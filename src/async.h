/* Owner-scoped completion of an asynchronous call (plan §4.2).
 *
 * The vendored lifetime module covers signals and main-loop sources; this covers the other
 * way a callback outlives its owner: a library's GAsyncReadyCallback. Every `*_async` call in
 * src/ passes chandler_async_ready as its callback and a ChandlerAsync as its user data
 * (tools/check-source.py, rule "async"):
 *
 *   ChandlerAsync *a = chandler_async_new (owner, on_loaded, data, g_free);
 *   as_pool_load_async (pool, chandler_async_cancellable (a), chandler_async_ready, a);
 *
 * - When `owner` is disposed first, the call's cancellable is cancelled.
 * - `fn` always runs exactly once, so it can finish the call and free what it holds; its
 *   `owner` argument is nullptr when the owner has gone, and fn must then touch nothing of it.
 * - `destroy` (may be nullptr) is called on `data` after fn.
 * - fn is timed by the stall watch (GBK_STALL_MS, as in lifetime.h), naming fn.
 * Main thread only. */
#pragma once

typedef struct _ChandlerAsync ChandlerAsync;
typedef void (*ChandlerAsyncFunc) (GObject *source, GAsyncResult *result, GObject *owner,
                                   void *data);

#define chandler_async_new(owner, fn, data, destroy) \
  chandler_async_new_named (owner, fn, data, destroy, #fn)
ChandlerAsync *chandler_async_new_named (GObject *owner, ChandlerAsyncFunc fn, void *data,
                                         GDestroyNotify destroy, const char *name);

/* Cancelled when the owner is disposed. Owned by the ChandlerAsync. */
GCancellable *chandler_async_cancellable (const ChandlerAsync *async);

/* The GAsyncReadyCallback to pass, with the ChandlerAsync as its user data. */
void chandler_async_ready (GObject *source, GAsyncResult *result, void *async);

/* Calls started and not yet completed. Tests assert it returns to 0. */
int chandler_async_live (void);
