/* ide-runner.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#define G_LOG_DOMAIN "ide-runner"

#include <glib/gi18n.h>
#include <libpeas/peas.h>
#include <stdlib.h>

#include "ide-context.h"
#include "ide-debug.h"

#include "runner/ide-runner.h"
#include "runner/ide-runner-addin.h"
#include "workers/ide-subprocess-launcher.h"

typedef struct
{
  PeasExtensionSet *addins;
  GQueue argv;
} IdeRunnerPrivate;

typedef struct
{
  GSList *prehook_queue;
  GSList *posthook_queue;
} IdeRunnerRunState;

enum {
  PROP_0,
  PROP_ARGV,
  N_PROPS
};

enum {
  SPAWNED,
  N_SIGNALS
};

static void ide_runner_tick_posthook (GTask *task);
static void ide_runner_tick_prehook  (GTask *task);
static void ide_runner_tick_run      (GTask *task);

G_DEFINE_TYPE_WITH_PRIVATE (IdeRunner, ide_runner, IDE_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static IdeRunnerAddin *
pop_runner_addin (GSList **list)
{
  IdeRunnerAddin *ret;

  g_assert (list != NULL);
  g_assert (*list != NULL);

  ret = (*list)->data;

  *list = g_slist_delete_link (*list, *list);

  return ret;
}

static void
ide_runner_run_state_free (gpointer data)
{
  IdeRunnerRunState *state = data;

  g_slist_foreach (state->prehook_queue, (GFunc)g_object_unref, NULL);
  g_slist_free (state->prehook_queue);

  g_slist_foreach (state->posthook_queue, (GFunc)g_object_unref, NULL);
  g_slist_free (state->posthook_queue);

  g_slice_free (IdeRunnerRunState, state);
}

static void
ide_runner_run_wait_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  IDE_ENTRY;

  g_assert (G_IS_SUBPROCESS (subprocess));

  if (!g_subprocess_wait_finish (subprocess, result, &error))
    {
      g_task_return_error (task, error);
      IDE_EXIT;
    }

  if (g_subprocess_get_if_exited (subprocess))
    {
      gint exit_code;

      exit_code = g_subprocess_get_exit_status (subprocess);

      if (exit_code == EXIT_SUCCESS)
        {
          g_task_return_boolean (task, TRUE);
          IDE_EXIT;
        }
    }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "%s",
                           _("Process quit unexpectedly"));

  IDE_EXIT;
}

static void
ide_runner_real_run_async (IdeRunner           *self,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);
  g_autoptr(GTask) task = NULL;
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  const gchar *identifier;
  GError *error = NULL;

  IDE_ENTRY;

  g_assert (IDE_IS_RUNNER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_runner_real_run_async);

  launcher = ide_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE);

  for (GList *iter = priv->argv.head; iter != NULL; iter = iter->next)
    ide_subprocess_launcher_push_argv (launcher, iter->data);

  ide_subprocess_launcher_set_cwd (launcher, g_get_home_dir ());

  subprocess = ide_subprocess_launcher_spawn_sync (launcher, cancellable, &error);

  g_assert (subprocess == NULL || G_IS_SUBPROCESS (subprocess));

  if (subprocess == NULL)
    {
      g_task_return_error (task, error);
      IDE_GOTO (failure);
    }

  identifier = g_subprocess_get_identifier (subprocess);

  g_signal_emit (self, signals [SPAWNED], 0, identifier);

  g_subprocess_wait_async (subprocess,
                           cancellable,
                           ide_runner_run_wait_cb,
                           g_steal_pointer (&task));

failure:
  IDE_EXIT;
}

static gboolean
ide_runner_real_run_finish (IdeRunner     *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_assert (IDE_IS_RUNNER (self));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (G_TASK (result), self));
  g_assert (g_task_get_source_tag (G_TASK (result)) == ide_runner_real_run_async);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
ide_runner_extension_added (PeasExtensionSet *set,
                            PeasPluginInfo   *plugin_info,
                            PeasExtension    *exten,
                            gpointer          user_data)
{
  IdeRunnerAddin *addin = (IdeRunnerAddin *)exten;
  IdeRunner *self = user_data;

  g_assert (PEAS_IS_EXTENSION_SET (set));
  g_assert (plugin_info != NULL);
  g_assert (IDE_IS_RUNNER_ADDIN (addin));
  g_assert (IDE_IS_RUNNER (self));

  ide_runner_addin_load (addin, self);
}

static void
ide_runner_extension_removed (PeasExtensionSet *set,
                              PeasPluginInfo   *plugin_info,
                              PeasExtension    *exten,
                              gpointer          user_data)
{
  IdeRunnerAddin *addin = (IdeRunnerAddin *)exten;
  IdeRunner *self = user_data;

  g_assert (PEAS_IS_EXTENSION_SET (set));
  g_assert (plugin_info != NULL);
  g_assert (IDE_IS_RUNNER_ADDIN (addin));
  g_assert (IDE_IS_RUNNER (self));

  ide_runner_addin_unload (addin, self);
}

static void
ide_runner_constructed (GObject *object)
{
  IdeRunner *self = (IdeRunner *)object;
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  G_OBJECT_CLASS (ide_runner_parent_class)->constructed (object);

  priv->addins = peas_extension_set_new (peas_engine_get_default (),
                                         IDE_TYPE_RUNNER_ADDIN,
                                         NULL);

  g_signal_connect (priv->addins,
                    "extension-added",
                    G_CALLBACK (ide_runner_extension_added),
                    self);

  g_signal_connect (priv->addins,
                    "extension-removed",
                    G_CALLBACK (ide_runner_extension_removed),
                    self);

  peas_extension_set_foreach (priv->addins,
                              ide_runner_extension_added,
                              self);
}

static void
ide_runner_finalize (GObject *object)
{
  IdeRunner *self = (IdeRunner *)object;
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  g_queue_foreach (&priv->argv, (GFunc)g_free, NULL);
  g_queue_clear (&priv->argv);

  G_OBJECT_CLASS (ide_runner_parent_class)->finalize (object);
}

static void
ide_runner_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  IdeRunner *self = IDE_RUNNER (object);

  switch (prop_id)
    {
    case PROP_ARGV:
      g_value_take_boxed (value, ide_runner_get_argv (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_runner_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  IdeRunner *self = IDE_RUNNER (object);

  switch (prop_id)
    {
    case PROP_ARGV:
      ide_runner_set_argv (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_runner_class_init (IdeRunnerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ide_runner_constructed;
  object_class->finalize = ide_runner_finalize;
  object_class->get_property = ide_runner_get_property;
  object_class->set_property = ide_runner_set_property;

  klass->run_async = ide_runner_real_run_async;
  klass->run_finish = ide_runner_real_run_finish;

  properties [PROP_ARGV] =
    g_param_spec_boxed ("argv",
                        "Argv",
                        "The argument list for the command",
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [SPAWNED] =
    g_signal_new ("spawned",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_STRING);
}

static void
ide_runner_init (IdeRunner *self)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  g_queue_init (&priv->argv);
}

/**
 * ide_runner_get_stdin:
 *
 * Returns: (nullable) (transfer full): An #GOutputStream or %NULL.
 */
GInputStream *
ide_runner_get_stdin (IdeRunner *self)
{
  g_return_val_if_fail (IDE_IS_RUNNER (self), NULL);

  return IDE_RUNNER_GET_CLASS (self)->get_stdin (self);
}

/**
 * ide_runner_get_stdout:
 *
 * Returns: (nullable) (transfer full): An #GOutputStream or %NULL.
 */
GOutputStream *
ide_runner_get_stdout (IdeRunner *self)
{
  g_return_val_if_fail (IDE_IS_RUNNER (self), NULL);

  return IDE_RUNNER_GET_CLASS (self)->get_stdout (self);
}

/**
 * ide_runner_get_stderr:
 *
 * Returns: (nullable) (transfer full): An #GOutputStream or %NULL.
 */
GOutputStream *
ide_runner_get_stderr (IdeRunner *self)
{
  g_return_val_if_fail (IDE_IS_RUNNER (self), NULL);

  return IDE_RUNNER_GET_CLASS (self)->get_stderr (self);
}

void
ide_runner_force_quit (IdeRunner *self)
{
  g_return_if_fail (IDE_IS_RUNNER (self));

  IDE_RUNNER_GET_CLASS (self)->force_quit (self);
}

void
ide_runner_set_argv (IdeRunner           *self,
                     const gchar * const *argv)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);
  guint i;

  g_return_if_fail (IDE_IS_RUNNER (self));

  g_queue_foreach (&priv->argv, (GFunc)g_free, NULL);
  g_queue_clear (&priv->argv);

  if (argv != NULL)
    {
      for (i = 0; argv [i]; i++)
        g_queue_push_tail (&priv->argv, g_strdup (argv [i]));
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ARGV]);
}

/**
 * ide_runner_get_argv:
 *
 * Gets the argument list as a newly allocated string array.
 *
 * Returns: (transfer full): A newly allocated string array that should
 *   be freed with g_strfreev().
 */
gchar **
ide_runner_get_argv (IdeRunner *self)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);
  GPtrArray *ar;
  GList *iter;

  g_return_val_if_fail (IDE_IS_RUNNER (self), NULL);

  ar = g_ptr_array_new ();

  for (iter = priv->argv.head; iter != NULL; iter = iter->next)
    {
      const gchar *param = iter->data;

      g_ptr_array_add (ar, g_strdup (param));
    }

  g_ptr_array_add (ar, NULL);

  return (gchar **)g_ptr_array_free (ar, FALSE);
}

static void
ide_runner_collect_addins_cb (PeasExtensionSet *set,
                              PeasPluginInfo   *plugin_info,
                              PeasExtension    *exten,
                              gpointer          user_data)
{
  GSList **list = user_data;

  *list = g_slist_prepend (*list, exten);
}

static void
ide_runner_collect_addins (IdeRunner  *self,
                           GSList    **list)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  g_assert (IDE_IS_RUNNER (self));
  g_assert (list != NULL);

  peas_extension_set_foreach (priv->addins,
                              ide_runner_collect_addins_cb,
                              list);
}

static void
ide_runner_posthook_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  IdeRunnerAddin *addin = (IdeRunnerAddin *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (IDE_IS_RUNNER_ADDIN (addin));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!ide_runner_addin_posthook_finish (addin, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  ide_runner_tick_posthook (task);
}

static void
ide_runner_tick_posthook (GTask *task)
{
  IdeRunnerRunState *state;

  g_assert (G_IS_TASK (task));

  state = g_task_get_task_data (task);

  if (state->posthook_queue != NULL)
    {
      g_autoptr(IdeRunnerAddin) addin = NULL;

      addin = pop_runner_addin (&state->posthook_queue);
      ide_runner_addin_posthook_async (addin,
                                       g_task_get_cancellable (task),
                                       ide_runner_posthook_cb,
                                       g_object_ref (task));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
ide_runner_run_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  IdeRunner *self = (IdeRunner *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (IDE_IS_RUNNER (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!IDE_RUNNER_GET_CLASS (self)->run_finish (self, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  ide_runner_tick_posthook (task);
}

static void
ide_runner_tick_run (GTask *task)
{
  IdeRunner *self;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  IDE_RUNNER_GET_CLASS (self)->run_async (self,
                                          g_task_get_cancellable (task),
                                          ide_runner_run_cb,
                                          g_object_ref (task));
}

static void
ide_runner_prehook_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  IdeRunnerAddin *addin = (IdeRunnerAddin *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (IDE_IS_RUNNER_ADDIN (addin));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!ide_runner_addin_prehook_finish (addin, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  ide_runner_tick_prehook (task);
}

static void
ide_runner_tick_prehook (GTask *task)
{
  IdeRunnerRunState *state;

  g_assert (G_IS_TASK (task));

  state = g_task_get_task_data (task);

  if (state->prehook_queue != NULL)
    {
      g_autoptr(IdeRunnerAddin) addin = NULL;

      addin = pop_runner_addin (&state->prehook_queue);
      ide_runner_addin_prehook_async (addin,
                                      g_task_get_cancellable (task),
                                      ide_runner_prehook_cb,
                                      g_object_ref (task));
      return;
    }

  ide_runner_tick_run (task);
}

void
ide_runner_run_async (IdeRunner           *self,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  IdeRunnerRunState *state;

  g_return_if_fail (IDE_IS_RUNNER (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_runner_run_async);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_priority (task, G_PRIORITY_LOW);

  /*
   * We need to run the prehook functions for each addin first before we
   * can call our IdeRunnerClass.run vfunc.  Since these are async, we
   * have to bring some state along with us.
   */
  state = g_slice_new0 (IdeRunnerRunState);
  ide_runner_collect_addins (self, &state->prehook_queue);
  ide_runner_collect_addins (self, &state->posthook_queue);
  g_task_set_task_data (task, state, ide_runner_run_state_free);

  ide_runner_tick_prehook (task);
}

gboolean
ide_runner_run_finish (IdeRunner     *self,
                       GAsyncResult  *result,
                       GError       **error)
{
  g_return_val_if_fail (IDE_IS_RUNNER (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
ide_runner_append_argv (IdeRunner   *self,
                        const gchar *param)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  g_return_if_fail (IDE_IS_RUNNER (self));
  g_return_if_fail (param != NULL);

  g_queue_push_tail (&priv->argv, g_strdup (param));
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ARGV]);
}

void
ide_runner_prepend_argv (IdeRunner   *self,
                         const gchar *param)
{
  IdeRunnerPrivate *priv = ide_runner_get_instance_private (self);

  g_return_if_fail (IDE_IS_RUNNER (self));
  g_return_if_fail (param != NULL);

  g_queue_push_head (&priv->argv, g_strdup (param));
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ARGV]);
}

IdeRunner *
ide_runner_new (IdeContext *context)
{
  g_return_val_if_fail (IDE_IS_CONTEXT (context), NULL);

  return g_object_new (IDE_TYPE_RUNNER,
                       "context", context,
                       NULL);
}
