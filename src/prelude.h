/* Force-included first in every translation unit (see meson.build, -include).
 *
 * 1. Pulls in every system and library header the project is allowed to use, so they are
 *    all parsed before anything is poisoned.
 * 2. Poisons APIs we have banned. Using one is a compile error, not a review comment.
 *    Need a header that isn't here? Add it above the poison block, never below.
 *
 * meson.build reads every name from the poison lines below and probes it: the probe must
 * fail to compile here and compile with CHANDLER_PRELUDE_NO_BANS (which exists only for
 * those probes). Keep one `#pragma GCC poison` per line: that is what meson parses. */
#pragma once
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/eventfd.h> /* vendor/quoin/tests/test-lifetime.c relies on the prelude for it */
#include <unistd.h>

/* Each of these turns a check into nothing; under G_DISABLE_ASSERT, g_assert_not_reached()
 * becomes __builtin_unreachable(): undefined behaviour where an abort was meant. */
#if defined(G_DISABLE_ASSERT) || defined(G_DISABLE_CHECKS) || defined(G_DISABLE_CAST_CHECKS)
#error "G_DISABLE_ASSERT, G_DISABLE_CHECKS and G_DISABLE_CAST_CHECKS must never be defined"
#endif

#include <glib.h>
#include <glib-unix.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
/* Included everywhere so their bans below apply everywhere. Only the adapters may use them
 * (tools/check-source.py, rule "adapters"). */
#include <appstream.h>
#include <packagekit-glib2/packagekit.h>

#include "chandler-config.h"
#include "fatal.h"
#ifdef GBK_ANALYZER
#include "analyzer-glib.h"
#endif

#ifndef CHANDLER_PRELUDE_NO_BANS

/* --- Unbounded or error-swallowing libc string/number functions. ---
 * Use g_strdup_printf / GString / g_strlcpy / g_ascii_string_to_signed with error checks. */
#pragma GCC poison strcpy strcat strncpy strncat sprintf vsprintf gets strtok
#pragma GCC poison atoi atol atoll atof

/* --- Anything that hands a string to a shell. --- */
#pragma GCC poison system popen
#pragma GCC poison g_spawn_command_line_async g_spawn_command_line_sync

/* --- Thread cancellation / exit: unwinding would skip every cleanup in C code. --- */
#pragma GCC poison pthread_cancel pthread_exit g_thread_exit

/* --- Nested main loops: code running inside one can pull the world out from under the
 * caller. Use asynchronous "response" handlers. The clipboard's wait_* spin one too. --- */
#pragma GCC poison gtk_dialog_run gtk_main g_main_loop_run
#pragma GCC poison gtk_clipboard_wait_for_contents gtk_clipboard_wait_for_text
#pragma GCC poison gtk_clipboard_wait_for_uris gtk_clipboard_wait_for_targets
#pragma GCC poison gtk_clipboard_wait_for_image gtk_clipboard_wait_for_rich_text
#pragma GCC poison gtk_clipboard_wait_is_text_available gtk_clipboard_wait_is_uris_available
#pragma GCC poison gtk_clipboard_wait_is_target_available gtk_clipboard_wait_is_image_available
#pragma GCC poison gtk_clipboard_wait_is_rich_text_available

/* --- g_error: use gbk_fatal (vendor/quoin/src/fatal.h). --- */
#undef g_error
#pragma GCC poison g_error

/* --- Stack allocation of runtime size. --- */
#undef alloca
#pragma GCC poison alloca
#undef g_alloca
#undef g_alloca0
#undef g_newa
#undef g_newa0
#pragma GCC poison g_alloca g_alloca0 g_newa g_newa0

/* --- Blocking calls on the main thread freeze the window (plan §4.2). AppStream, PackageKit
 * and D-Bus are asynchronous only. PackageKit's synchronous client calls have no _sync
 * suffix (pk_client_resolve); PkTask's do, and PkTask is banned whole below. --- */
#pragma GCC poison as_pool_load
#pragma GCC poison pk_client_accept_eula pk_client_adopt pk_client_depends_on
#pragma GCC poison pk_client_download_packages pk_client_get_categories pk_client_get_details
#pragma GCC poison pk_client_get_details_local pk_client_get_distro_upgrades pk_client_get_files
#pragma GCC poison pk_client_get_files_local pk_client_get_old_transactions pk_client_get_packages
#pragma GCC poison pk_client_get_progress pk_client_get_repo_list pk_client_get_update_detail
#pragma GCC poison pk_client_get_updates pk_client_install_files pk_client_install_packages
#pragma GCC poison pk_client_install_signature pk_client_refresh_cache pk_client_remove_packages
#pragma GCC poison pk_client_repair_system pk_client_repo_enable pk_client_repo_remove
#pragma GCC poison pk_client_repo_set_data pk_client_required_by pk_client_resolve
#pragma GCC poison pk_client_search_details pk_client_search_files pk_client_search_groups
#pragma GCC poison pk_client_search_names pk_client_update_packages pk_client_upgrade_system
#pragma GCC poison pk_client_what_provides
#pragma GCC poison pk_package_sack_get_details pk_package_sack_get_update_detail
#pragma GCC poison pk_package_sack_resolve
#pragma GCC poison pk_control_get_properties pk_control_get_transaction_list
#pragma GCC poison pk_control_set_proxy pk_control_set_proxy2 pk_control_suggest_daemon_quit
#pragma GCC poison pk_offline_cancel pk_offline_cancel_with_flags pk_offline_clear_results
#pragma GCC poison pk_offline_get_prepared_ids pk_offline_get_prepared_sack pk_offline_get_results
#pragma GCC poison pk_offline_trigger pk_offline_trigger_upgrade pk_offline_trigger_with_flags
#pragma GCC poison g_dbus_connection_call_sync g_dbus_proxy_call_sync g_bus_get_sync
#pragma GCC poison g_dbus_connection_call_with_unix_fd_list_sync
#pragma GCC poison g_dbus_proxy_call_with_unix_fd_list_sync
#pragma GCC poison g_dbus_proxy_new_sync g_dbus_proxy_new_for_bus_sync
#pragma GCC poison gdk_pixbuf_new_from_file gdk_pixbuf_new_from_file_at_size
#pragma GCC poison gdk_pixbuf_new_from_file_at_scale g_file_load_contents
#pragma GCC poison gtk_image_new_from_file gtk_image_set_from_file

/* --- Only installing is reachable (plan §3.3; the owner's decision §8.3: nothing is ever
 * removed, downgraded or replaced). PkTask exists to ask "install untrusted anyway?" and
 * to accept simulations; every role that removes or changes installed software is out. --- */
#undef PK_TYPE_TASK
#pragma GCC poison pk_task_new pk_task_get_type PK_TYPE_TASK
#pragma GCC poison pk_client_remove_packages_async pk_client_update_packages_async
#pragma GCC poison pk_client_upgrade_system_async pk_client_repair_system_async
#pragma GCC poison pk_client_install_files_async pk_client_repo_remove_async
#pragma GCC poison pk_client_repo_enable_async pk_client_repo_set_data_async
#pragma GCC poison pk_client_install_signature_async pk_client_refresh_cache_async

/* --- No Pango markup anywhere: catalog text is shown as text, so it cannot inject markup
 * (plan §3.6). Emphasis comes from CSS or PangoAttrList. --- */
#pragma GCC poison gtk_label_set_markup gtk_label_set_markup_with_mnemonic
#pragma GCC poison gtk_label_set_use_markup gtk_widget_set_tooltip_markup gtk_tooltip_set_markup
#pragma GCC poison gtk_message_dialog_new_with_markup gtk_message_dialog_set_markup
#pragma GCC poison gtk_message_dialog_format_secondary_markup g_markup_printf_escaped
#pragma GCC poison g_markup_vprintf_escaped
#pragma GCC poison pango_layout_set_markup pango_layout_set_markup_with_accel pango_parse_markup
#pragma GCC poison pango_markup_parser_new gtk_tree_view_set_tooltip_column
#pragma GCC poison gtk_icon_view_set_tooltip_column gtk_expander_set_use_markup
#pragma GCC poison gtk_entry_set_icon_tooltip_markup gtk_tool_item_set_tooltip_markup

/* --- Raw signal connections and main-loop sources. ---
 * A callback that outlives the object it points at is the classic GTK crash. Use
 * gbk_connect() / gbk_idle_add() / gbk_timeout_add() / ... from lifetime.h: every callback
 * has an owner and is disconnected/removed automatically when the owner is destroyed. Only
 * vendor/quoin/src/lifetime.c may use the raw APIs. Macros must be #undef'd before
 * poisoning, because GCC does not flag a poisoned name inside a macro defined before it. */
#ifndef GBK_LIFETIME_IMPL
#undef g_signal_connect
#undef g_signal_connect_after
#undef g_signal_connect_swapped
#pragma GCC poison g_signal_connect g_signal_connect_after g_signal_connect_swapped
#pragma GCC poison g_signal_connect_data g_signal_connect_object
#pragma GCC poison g_signal_connect_closure g_signal_connect_closure_by_id
#pragma GCC poison g_idle_add g_idle_add_full g_idle_add_once
#pragma GCC poison g_timeout_add g_timeout_add_full g_timeout_add_once
#pragma GCC poison g_timeout_add_seconds g_timeout_add_seconds_full g_timeout_add_seconds_once
#pragma GCC poison g_source_attach g_source_set_callback g_source_set_closure
#pragma GCC poison g_unix_fd_add g_unix_fd_add_full g_unix_signal_add g_unix_signal_add_full
#pragma GCC poison g_io_add_watch g_io_add_watch_full g_child_watch_add g_child_watch_add_full
#pragma GCC poison gtk_clipboard_request_contents gtk_clipboard_request_text
#pragma GCC poison gtk_clipboard_request_rich_text gtk_clipboard_request_image
#pragma GCC poison gtk_clipboard_request_uris gtk_clipboard_request_targets
#pragma GCC poison gdk_threads_add_idle gdk_threads_add_idle_full
#pragma GCC poison gdk_threads_add_timeout gdk_threads_add_timeout_full
#pragma GCC poison gdk_threads_add_timeout_seconds gdk_threads_add_timeout_seconds_full
#pragma GCC poison g_dbus_connection_signal_subscribe
#endif

#endif /* CHANDLER_PRELUDE_NO_BANS */
