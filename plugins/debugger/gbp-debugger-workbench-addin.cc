/* gbp-debugger-workbench-addin.c
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

#define G_LOG_DOMAIN "gbp-debugger-workbench-addin"

#include <glib/gi18n.h>

#include "gbp-debugger-perspective.h"
#include "gbp-debugger-workbench-addin.h"

#include <iostream>
#include <glibmm.h>
#include "common/nmv-initializer.h"
#include "common/nmv-safe-ptr-utils.h"
#include "common/nmv-dynamic-module.h"
#include "dynmods/nmv-i-debugger.h"

using namespace nemiver;
using namespace nemiver::common;

Glib::RefPtr<Glib::MainLoop> loop =
    Glib::MainLoop::create (Glib::MainContext::get_default ());

static int gv_cur_frame=-1;

struct _GbpDebuggerWorkbenchAddin
{
  GObject                 parent_instance;

  IdeWorkbench           *workbench;
  GbpDebuggerPerspective *perspective;
  GSimpleActionGroup     *actions;

  GtkBox                 *exec_controls;
  GtkBox                 *step_controls;
};

void
on_engine_died_signal ()
{
    std::cout << "engine died\n";
    loop->quit ();
}

void
on_stopped_signal (IDebugger::StopReason a_reason,
                   bool a_has_frame,
                   const IDebugger::Frame &a_frame,
                   int a_thread_id,
                   const string &/*bp num*/,
                   const UString &/*a_cookie*/)
{
    std::cout << "stopped, reason: " << (int)a_reason << " ";
    if (a_has_frame) {
        std::cout << "in frame: " << a_frame.function_name ();
    }
    std::cout << "thread-id: '" << a_thread_id << "'\n";
}

void
on_current_frame_signal (const IDebugger::Frame &a_frame, const UString &a_cookie)
{
    if (a_cookie.empty ()) {}

    gv_cur_frame = a_frame.level ();
}

void
on_frames_listed_signal (const std::vector<IDebugger::Frame> &a_frames,
                         const UString &a_cookie)
{
    if (a_cookie.empty ()) {}

    std::cout << "frame stack list: \n";
    vector<IDebugger::Frame>::size_type i = 0;
    for (i = 0; i < a_frames.size () ; ++i) {
        std::cout << a_frames[i].function_name () << "() ";
        if ((int) i == gv_cur_frame) {
            std::cout << "<---";
        }
        std::cout << "\n";
    }
    gv_cur_frame = -1;
    std::cout << "end of frame stack list\n";
    loop->quit ();
}

void
on_frames_params_listed_signal
    (const std::map< int, std::list<IDebugger::VariableSafePtr> >& a_frame_params,
     const UString &a_cookie)
{
    if (a_frame_params.empty () || a_cookie.empty ()) {}
}

void
on_program_finished_signal ()
{
    std::cout << "program finished";
    loop->quit ();
}

void
on_console_message_signal (const UString &a_msg)
{
    cout << "(gdb) " << a_msg << "\n";
}

void
on_error_message_signal (const UString &a_msg)
{
    cout << "(gdb error) " << a_msg << "\n";
}

static void
gbp_debugger_workbench_addin_runner_exited (GbpDebuggerWorkbenchAddin *self,
                                            IdeRunner                 *runner)
{
  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));
  g_assert (IDE_IS_RUNNER (runner));

  gtk_widget_hide (GTK_WIDGET (self->exec_controls));
  gtk_widget_hide (GTK_WIDGET (self->step_controls));

  ide_workbench_set_visible_perspective_name (self->workbench, "editor");
}

static void
debugger_run_handler (IdeRunManager *run_manager,
                      IdeRunner     *runner,
                      gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  IDE_ENTRY;

  g_assert (IDE_IS_RUN_MANAGER (run_manager));
  g_assert (IDE_IS_RUNNER (runner));
  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

  ide_workbench_set_visible_perspective_name (self->workbench, "debugger");

  g_signal_connect_object (runner,
                           "exited",
                           G_CALLBACK (gbp_debugger_workbench_addin_runner_exited),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_show (GTK_WIDGET (self->exec_controls));
  gtk_widget_show (GTK_WIDGET (self->step_controls));

  ide_runner_prepend_argv (runner, "--args");
  ide_runner_prepend_argv (runner, "run");
  ide_runner_prepend_argv (runner, "-ex");
  ide_runner_prepend_argv (runner, "gdb");

  //TODO: Add nemiver interface to pid.

  IDebuggerSafePtr debugger = debugger_utils::load_debugger_iface_with_confmgr();

  debugger->set_event_loop_context (loop->get_context ());
  debugger->engine_died_signal ().connect(sigc::ptr_fun (&on_engine_died_signal));
  debugger->program_finished_signal ().connect(sigc::ptr_fun (&on_program_finished_signal));
  debugger->stopped_signal ().connect (&on_stopped_signal);
  debugger->current_frame_signal ().connect (&on_current_frame_signal);
  debugger->frames_listed_signal ().connect (&on_frames_listed_signal);
  debugger->frames_arguments_listed_signal ().connect(&on_frames_params_listed_signal);
  debugger->console_message_signal ().connect (&on_console_message_signal);
  debugger->log_message_signal ().connect (&on_error_message_signal);

  debugger->load_core_file("program", "core");
  debugger->list_frames();
  loop->run();

  IDE_EXIT;
}

static void
gbp_debugger_workbench_addin_load (IdeWorkbenchAddin *addin,
                                   IdeWorkbench      *workbench)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)addin;
  IdeWorkbenchHeaderBar *headerbar;
  IdeRunManager *run_manager;
  IdeContext *context;
  GtkBox *box;

  g_assert (IDE_IS_WORKBENCH_ADDIN (addin));
  g_assert (IDE_IS_WORKBENCH (workbench));

  self->workbench = workbench;

  gtk_widget_insert_action_group (GTK_WIDGET (workbench),
                                  "debugger",
                                  G_ACTION_GROUP (self->actions));

  headerbar = ide_workbench_get_headerbar (workbench);

  context = ide_workbench_get_context (workbench);

  run_manager = ide_context_get_run_manager (context);
  ide_run_manager_add_handler (run_manager,
                               "debugger",
                               _("Run with Debugger"),
                               "nemiver-symbolic",
                               "F5",
                               debugger_run_handler,
                               g_object_ref (self),
                               g_object_unref);

  self->perspective = (GbpDebuggerPerspective*)g_object_new (GBP_TYPE_DEBUGGER_PERSPECTIVE,
                                    "visible", TRUE,
                                    NULL);
  ide_workbench_add_perspective (workbench, IDE_PERSPECTIVE (self->perspective));

  self->exec_controls = (GtkBox*)g_object_new (GTK_TYPE_BOX,
                                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                                      NULL);
  ide_widget_add_style_class (GTK_WIDGET (self->exec_controls), "linked");
  ide_workbench_header_bar_insert_left (headerbar, GTK_WIDGET (self->exec_controls), GTK_PACK_START, 100);

#define ADD_BUTTON(action_name, icon_name, tooltip_text) \
  G_STMT_START { \
    GtkButton *button; \
    button = (GtkButton*)g_object_new (GTK_TYPE_BUTTON, \
                           "action-name", action_name, \
                           "child", g_object_new (GTK_TYPE_IMAGE, \
                                                  "icon-name", icon_name, \
                                                  "visible", TRUE, \
                                                  NULL), \
                           "tooltip-text", tooltip_text, \
                           "visible", TRUE, \
                           NULL); \
    gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (button)); \
  } G_STMT_END

  box = self->exec_controls;
  ADD_BUTTON ("debugger.continue",            "debug-continue-symbolic",            _("Continue"));
  ADD_BUTTON ("debugger.execute-from-cursor", "debug-execute-to-cursor-symbolic",   _("Execute to cursor"));
  ADD_BUTTON ("debugger.execute-to-cursor",   "debug-execute-from-cursor-symbolic", _("Execute from cursor"));

  self->step_controls = (GtkBox*)g_object_new (GTK_TYPE_BOX,
                                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                                      NULL);
  ide_widget_add_style_class (GTK_WIDGET (self->step_controls), "linked");
  ide_workbench_header_bar_insert_left (headerbar, GTK_WIDGET (self->step_controls), GTK_PACK_START, 110);

  box = self->step_controls;
  ADD_BUTTON ("debugger.step-in",   "debug-step-in-symbolic",   _("Step in"));
  ADD_BUTTON ("debugger.step-out",  "debug-step-out-symbolic",  _("Step out"));
  ADD_BUTTON ("debugger.step-over", "debug-step-over-symbolic", _("Step over"));
}

static void
gbp_debugger_workbench_addin_unload (IdeWorkbenchAddin *addin,
                                     IdeWorkbench      *workbench)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)addin;
  IdeRunManager *run_manager;
  IdeContext *context;

  g_assert (IDE_IS_WORKBENCH_ADDIN (addin));
  g_assert (IDE_IS_WORKBENCH (workbench));

  context = ide_workbench_get_context (workbench);
  run_manager = ide_context_get_run_manager (context);
  ide_run_manager_remove_handler (run_manager, "debugger");

  gtk_widget_insert_action_group (GTK_WIDGET (workbench), "debugger", NULL);

  ide_workbench_remove_perspective (workbench, IDE_PERSPECTIVE (self->perspective));
  self->perspective = NULL;

  self->workbench = NULL;
}

static void
workbench_addin_iface_init (IdeWorkbenchAddinInterface *iface)
{
  iface->load = gbp_debugger_workbench_addin_load;
  iface->unload = gbp_debugger_workbench_addin_unload;
}

G_DEFINE_TYPE_EXTENDED (GbpDebuggerWorkbenchAddin, gbp_debugger_workbench_addin, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (IDE_TYPE_WORKBENCH_ADDIN, workbench_addin_iface_init))

static void
gbp_debugger_workbench_addin_finalize (GObject *object)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)object;

  g_clear_object (&self->actions);

  G_OBJECT_CLASS (gbp_debugger_workbench_addin_parent_class)->finalize (object);
}

static void
gbp_debugger_workbench_addin_class_init (GbpDebuggerWorkbenchAddinClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gbp_debugger_workbench_addin_finalize;
}

static void
step_in_activate (GSimpleAction *action,
                  GVariant      *param,
                  gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static void
step_out_activate (GSimpleAction *action,
                   GVariant      *param,
                   gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static void
step_over_activate (GSimpleAction *action,
                    GVariant      *param,
                    gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static void
continue_action (GSimpleAction *action,
                 GVariant      *param,
                 gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static void
execute_to_cursor_action (GSimpleAction *action,
                          GVariant      *param,
                          gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static void
execute_from_cursor_action (GSimpleAction *action,
                            GVariant      *param,
                            gpointer       user_data)
{
  GbpDebuggerWorkbenchAddin *self = (GbpDebuggerWorkbenchAddin *)user_data;

  g_assert (GBP_IS_DEBUGGER_WORKBENCH_ADDIN (self));

}

static GActionEntry action_entries[] = {
  { "continue", continue_action },
  { "step-in", step_in_activate },
  { "step-out", step_out_activate },
  { "step-over", step_over_activate },
  { "execute-from-cursor", execute_from_cursor_action },
  { "execute-to-cursor", execute_to_cursor_action },
};

static void
gbp_debugger_workbench_addin_init (GbpDebuggerWorkbenchAddin *self)
{
  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   self);
}
