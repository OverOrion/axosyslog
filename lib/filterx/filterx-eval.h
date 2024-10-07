/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#ifndef FILTERX_EVAL_H_INCLUDED
#define FILTERX_EVAL_H_INCLUDED

#include "filterx/filterx-scope.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-error.h"
#include "template/eval.h"


typedef enum _FilterXEvalResult
{
  FXE_SUCCESS,
  FXE_FAILURE,
  FXE_DROP,
} FilterXEvalResult;


typedef enum _FilterXEvalControl
{
  FXC_NOTSET,
  FXC_DROP,
  FXC_DONE
} FilterXEvalControl;

typedef struct _FilterXEvalContext FilterXEvalContext;
struct _FilterXEvalContext
{
  LogMessage **msgs;
  gint num_msg;
  FilterXScope *scope;
  FilterXError error;
  LogTemplateEvalOptions template_eval_options;
  GPtrArray *weak_refs;
  FilterXEvalControl eval_control_modifier;
  FilterXEvalContext *previous_context;
};

FilterXEvalContext *filterx_eval_get_context(void);
FilterXScope *filterx_eval_get_scope(void);
void filterx_eval_push_error(const gchar *message, FilterXExpr *expr, FilterXObject *object);
void filterx_eval_push_error_info(const gchar *message, FilterXExpr *expr, gchar *info, gboolean free_info);
void filterx_eval_set_context(FilterXEvalContext *context);
FilterXEvalResult filterx_eval_exec(FilterXEvalContext *context, FilterXExpr *expr, LogMessage *msg);
void filterx_eval_sync_scope_and_message(FilterXScope *scope, LogMessage *msg);
const gchar *filterx_eval_get_last_error(void);
EVTTAG *filterx_format_last_error(void);
EVTTAG *filterx_format_last_error_location(void);
void filterx_eval_clear_errors(void);
EVTTAG *filterx_format_eval_result(FilterXEvalResult result);

void filterx_eval_store_weak_ref(FilterXObject *object);

void filterx_eval_init_context(FilterXEvalContext *context, FilterXEvalContext *previous_context);
void filterx_eval_deinit_context(FilterXEvalContext *context);

static inline void
filterx_eval_sync_message(FilterXEvalContext *context, LogMessage **pmsg, LogPathOptions *path_options)
{
  if (!context)
    return;

  if (!filterx_scope_is_dirty(context->scope))
    return;

  log_msg_make_writable(pmsg, path_options);
  filterx_scope_sync(context->scope, *pmsg);
}

static inline void
filterx_eval_prepare_for_fork(FilterXEvalContext *context, LogMessage **pmsg, LogPathOptions *path_options)
{
  filterx_eval_sync_message(context, pmsg, path_options);
  if (context)
    filterx_scope_write_protect(context->scope);
}

#endif
