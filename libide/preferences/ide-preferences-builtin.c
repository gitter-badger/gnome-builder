/* ide-preferences-builtin.c
 *
 * Copyright (C) 2015 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "ide-preferences-builtin"

#include <glib/gi18n.h>
#include <gtksourceview/gtksource.h>
#include <libpeas/peas.h>

#include "ide-macros.h"

#include "application/ide-application-private.h"
#include "preferences/ide-preferences-builtin.h"
#include "preferences/ide-preferences-entry.h"
#include "preferences/ide-preferences-language-row.h"
#include "preferences/ide-preferences-spin-button.h"
#include "vcs/ide-vcs-config.h"

static void
ide_preferences_builtin_register_plugins (IdePreferences *preferences)
{
  PeasEngine *engine;
  const GList *list;
  guint i = 0;

  g_assert (IDE_IS_PREFERENCES (preferences));

  engine = peas_engine_get_default ();
  list = peas_engine_get_plugin_list (engine);

  ide_preferences_add_page (preferences, "plugins", _("Extensions"), 700);
  ide_preferences_add_list_group (preferences, "plugins", "installed", _("Installed Extensions"), 0);
  ide_preferences_add_list_group (preferences, "plugins", "builtin", _("Bundled Extensions"), 100);

  for (; list; list = list->next, i++)
    {
      g_autofree gchar *path = NULL;
      PeasPluginInfo *plugin_info = list->data;
      const gchar *desc;
      const gchar *name;
      const gchar *group;

      if (peas_plugin_info_is_hidden (plugin_info))
        continue;

      name = peas_plugin_info_get_name (plugin_info);
      desc = peas_plugin_info_get_description (plugin_info);

      path = g_strdup_printf ("/org/gnome/builder/plugins/%s/",
                              peas_plugin_info_get_module_name (plugin_info));

      if (peas_plugin_info_is_builtin (plugin_info))
        group = "builtin";
      else
        group = "installed";

      ide_preferences_add_switch (preferences, "plugins", group, "org.gnome.builder.plugin", "enabled", path, NULL, name, desc, NULL, i);
    }
}

static void
ide_preferences_builtin_register_appearance (IdePreferences *preferences)
{
  GtkSourceStyleSchemeManager *manager;
  const gchar * const *scheme_ids;
  GtkWidget *bin;
  gint i;
  gint dark_mode;

  ide_preferences_add_page (preferences, "appearance", _("Appearance"), 0);

  ide_preferences_add_list_group (preferences, "appearance", "basic", _("Themes"), 0);
  dark_mode = ide_preferences_add_switch (preferences, "appearance", "basic", "org.gnome.builder", "night-mode", NULL, NULL, _("Dark Theme"), _("Whether Builder should use a dark theme"), _("dark theme"), 0);
  ide_preferences_add_switch (preferences, "appearance", "basic", "org.gnome.builder.editor", "show-grid-lines", NULL, NULL, _("Grid Pattern"), _("Display a grid pattern underneath source code"), NULL, 0);

  ide_preferences_add_list_group (preferences, "appearance", "schemes", NULL, 100);

  manager = gtk_source_style_scheme_manager_get_default ();
  scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (manager);

  for (i = 0; scheme_ids [i]; i++)
    {
      g_autofree gchar *variant_str = NULL;
      GtkSourceStyleScheme *scheme;
      const gchar *title;

      variant_str = g_strdup_printf ("\"%s\"", scheme_ids [i]);
      scheme = gtk_source_style_scheme_manager_get_scheme (manager, scheme_ids [i]);
      title = gtk_source_style_scheme_get_name (scheme);

      ide_preferences_add_radio (preferences, "appearance", "schemes", "org.gnome.builder.editor", "style-scheme-name", NULL, variant_str, title, NULL, title, i);
    }

  ide_preferences_add_list_group (preferences, "appearance", "font", _("Font"), 200);
  ide_preferences_add_font_button (preferences, "appearance", "font", "org.gnome.builder.editor", "font-name", _("Editor"), C_("Keywords", "editor font monospace"), 0);
  /* XXX: This belongs in terminal addin */
  ide_preferences_add_font_button (preferences, "appearance", "font", "org.gnome.builder.terminal", "font-name", _("Terminal"), C_("Keywords", "terminal font monospace"), 0);

  if (ide_application_get_disable_theme_tracking (IDE_APPLICATION_DEFAULT))
    {
      bin = ide_preferences_get_widget (preferences, dark_mode);
      gtk_widget_set_sensitive (bin, FALSE);
    }
}

static void
ide_preferences_builtin_register_keyboard (IdePreferences *preferences)
{
  ide_preferences_add_page (preferences, "keyboard", _("Keyboard"), 400);

  ide_preferences_add_list_group (preferences, "keyboard", "mode", _("Emulation"), 0);
  ide_preferences_add_radio (preferences, "keyboard", "mode", "org.gnome.builder.editor", "keybindings", NULL, "\"default\"", _("Builder"), _("Default keybinding mode which mimics gedit"), NULL, 0);
  ide_preferences_add_radio (preferences, "keyboard", "mode", "org.gnome.builder.editor", "keybindings", NULL, "\"emacs\"", _("Emacs"), _("Emulates the Emacs text editor"), NULL, 0);
  ide_preferences_add_radio (preferences, "keyboard", "mode", "org.gnome.builder.editor", "keybindings", NULL, "\"vim\"", _("Vim"), _("Emulates the Vim text editor"), NULL, 0);

  ide_preferences_add_list_group (preferences, "keyboard", "movements", _("Movement"), 100);
  ide_preferences_add_switch (preferences, "keyboard", "movements", "org.gnome.builder.editor", "smart-home-end", NULL, NULL, _("Smart Home and End"), _("Home moves to first non-whitespace character"), NULL, 0);
  ide_preferences_add_switch (preferences, "keyboard", "movements", "org.gnome.builder.editor", "smart-backspace", NULL, NULL, _("Smart Backspace"), _("Backspace will remove extra space to keep you aligned with your indentation"), NULL, 100);
}

static void
ide_preferences_builtin_register_editor (IdePreferences *preferences)
{
  ide_preferences_add_page (preferences, "editor", _("Editor"), 100);

  ide_preferences_add_list_group (preferences, "editor", "position", _("Cursor"), 0);
  ide_preferences_add_switch (preferences, "editor", "position", "org.gnome.builder.editor", "restore-insert-mark", NULL, NULL, _("Restore cursor position"), _("Restore cursor position when a file is reopened"), NULL, 0);
  ide_preferences_add_spin_button (preferences, "editor", "position", "org.gnome.builder.editor", "scroll-offset", NULL, _("Scroll Offset"), _("Minimum number of lines to keep above and below the cursor"), NULL, 10);
  ide_preferences_add_spin_button (preferences, "editor", "position", "org.gnome.builder.editor", "overscroll", NULL, _("Overscroll"), _("Allow the editor to scroll past the end of the buffer"), NULL, 20);

  ide_preferences_add_list_group (preferences, "editor", "line", _("Line Information"), 50);
  ide_preferences_add_switch (preferences, "editor", "line", "org.gnome.builder.editor", "show-line-numbers", NULL, NULL, _("Line numbers"), _("Show line number at beginning of each line"), NULL, 0);
  ide_preferences_add_switch (preferences, "editor", "line", "org.gnome.builder.editor", "show-line-changes", NULL, NULL, _("Line changes"), _("Show if a line was added or modified next to line number"), NULL, 1);

  ide_preferences_add_list_group (preferences, "editor", "highlight", _("Highlight"), 100);
  ide_preferences_add_switch (preferences, "editor", "highlight", "org.gnome.builder.editor", "highlight-current-line", NULL, NULL, _("Current line"), _("Make current line stand out with highlights"), NULL, 0);
  ide_preferences_add_switch (preferences, "editor", "highlight", "org.gnome.builder.editor", "highlight-matching-brackets", NULL, NULL, _("Matching brackets"), _("Highlight matching brackets based on cursor position"), NULL, 1);

  ide_preferences_add_list_group (preferences, "editor", "overview", _("Code Overview"), 100);
  ide_preferences_add_switch (preferences, "editor", "overview", "org.gnome.builder.editor", "show-map", NULL, NULL, _("Show overview map"), _("A zoomed out view to enhance navigating source code"), NULL, 0);
  ide_preferences_add_switch (preferences, "editor", "overview", "org.gnome.builder.editor", "auto-hide-map", NULL, NULL, _("Automatically hide overview map"), _("Automatically hide map when editor loses focus"), NULL, 1);

  ide_preferences_add_list_group (preferences, "editor", "draw-spaces", _("Whitespace Characters"), 400);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"space\"", _("Spaces"), NULL, NULL, 0);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"tab\"", _("Tabs"), NULL, NULL, 1);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"newline\"", _("New line and carriage return"), NULL, NULL, 2);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"nbsp\"", _("Non-breaking spaces"), NULL, NULL, 3);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"text\"", _("Spaces inside of text"), NULL, NULL, 4);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"trailing\"", _("Trailing Only"), NULL, NULL, 5);
  ide_preferences_add_radio (preferences, "editor", "draw-spaces", "org.gnome.builder.editor", "draw-spaces", NULL, "\"leading\"", _("Leading Only"), NULL, NULL, 6);
}

static void
ide_preferences_builtin_register_code_insight (IdePreferences *preferences)
{
  ide_preferences_add_page (preferences, "code-insight", _("Code Insight"), 300);

  ide_preferences_add_list_group (preferences, "code-insight", "highlighting", _("Highlighting"), 0);
  ide_preferences_add_switch (preferences, "code-insight", "highlighting", "org.gnome.builder.code-insight", "semantic-highlighting", NULL, NULL, _("Semantic Highlighting"), _("Use code insight to highlight additional information discovered in source file"), NULL, 0);

  ide_preferences_add_list_group (preferences, "code-insight", "completion", _("Completion"), 100);
  ide_preferences_add_switch (preferences, "code-insight", "completion", "org.gnome.builder.code-insight", "word-completion", NULL, NULL, _("Suggest words found in open files"), _("Suggests completions as you type based on words found in any open document"), NULL, 0);
  ide_preferences_add_switch (preferences, "code-insight", "completion", "org.gnome.builder.code-insight", "ctags-autocompletion", NULL, NULL, _("Suggest completions using Ctags"), _("Create and manages a Ctags database for completing class names, functions, and more"), NULL, 10);
  ide_preferences_add_switch (preferences, "code-insight", "completion", "org.gnome.builder.code-insight", "clang-autocompletion", NULL, NULL, _("Suggest completions using Clang (Experimental)"), _("Use Clang to suggest completions for C and C++ languages"), NULL, 20);

  ide_preferences_add_list_group (preferences, "code-insight", "diagnostics", _("Diagnostics"), 200);
}

static void
ide_preferences_builtin_register_snippets (IdePreferences *preferences)
{
  ide_preferences_add_page (preferences, "snippets", _("Snippets"), 350);

  ide_preferences_add_list_group (preferences, "snippets", "completion", NULL, 0);
  ide_preferences_add_switch (preferences, "snippets", "completion", "org.gnome.builder.code-insight", "snippet-completion", NULL, NULL, _("Code snippets"), _("Use code fragments to increase typing efficiency"), NULL, 0);
}

static void
ide_preferences_builtin_register_languages (IdePreferences *preferences)
{
  GtkSourceLanguageManager *manager;
  const gchar * const *language_ids;
  g_autoptr(GHashTable) sections = NULL;
  guint section_count = 0;
  gint i;

  sections = g_hash_table_new (g_str_hash, g_str_equal);

  ide_preferences_add_page (preferences, "languages", _("Programming Languages"), 200);

  manager = gtk_source_language_manager_get_default ();
  language_ids = gtk_source_language_manager_get_language_ids (manager);

  for (i = 0; language_ids [i]; i++)
    {
      IdePreferencesLanguageRow *row;
      GtkSourceLanguage *language;
      const gchar *name;
      const gchar *section;

      if (ide_str_equal0 (language_ids [i], "def"))
        continue;

      language = gtk_source_language_manager_get_language (manager, language_ids [i]);
      name = gtk_source_language_get_name (language);
      section = gtk_source_language_get_section (language);

      if (!g_hash_table_contains (sections, section))
        {
          ide_preferences_add_list_group (preferences, "languages",
                                          section, section, section_count++);
          g_hash_table_insert (sections, (gchar *)section, NULL);
        }

      row = g_object_new (IDE_TYPE_PREFERENCES_LANGUAGE_ROW,
                          "id", language_ids [i],
                          "title", name,
                          "visible", TRUE,
                          NULL);
      ide_preferences_add_custom (preferences, "languages", section, GTK_WIDGET (row), NULL, 0);
    }

  ide_preferences_add_page (preferences, "languages.id", NULL, 0);

  ide_preferences_add_list_group (preferences, "languages.id", "basic", NULL, 0);
  ide_preferences_add_switch (preferences, "languages.id", "basic", "org.gnome.builder.editor.language", "trim-trailing-whitespace", "/org/gnome/builder/editor/language/{id}/", NULL, _("Trim trailing whitespace"), _("Upon saving, trailing whitespace from modified lines will be trimmed."), NULL, 10);
  ide_preferences_add_switch (preferences, "languages.id", "basic", "org.gnome.builder.editor.language", "overwrite-braces", "/org/gnome/builder/editor/language/{id}/", NULL, _("Overwrite Braces"), _("Overwrite closing braces"), NULL, 20);

  ide_preferences_add_list_group (preferences, "languages.id", "margin", _("Margins"), 0);
  ide_preferences_add_radio (preferences, "languages.id", "margin", "org.gnome.builder.editor.language", "show-right-margin", "/org/gnome/builder/editor/language/{id}/", NULL, _("Show right margin"), NULL, NULL, 0);
  ide_preferences_add_spin_button (preferences, "languages.id", "margin", "org.gnome.builder.editor.language", "right-margin-position", "/org/gnome/builder/editor/language/{id}/", _("Right margin position"), _("Position in spaces for the right margin"), NULL, 10);

  ide_preferences_add_list_group (preferences, "languages.id", "indentation", _("Indentation"), 100);
  ide_preferences_add_spin_button (preferences, "languages.id", "indentation", "org.gnome.builder.editor.language", "tab-width", "/org/gnome/builder/editor/language/{id}/", _("Tab width"), _("Width of a tab character in spaces"), NULL, 10);
  ide_preferences_add_radio (preferences, "languages.id", "indentation", "org.gnome.builder.editor.language", "insert-spaces-instead-of-tabs", "/org/gnome/builder/editor/language/{id}/", NULL, _("Insert spaces instead of tabs"), _("Prefer spaces over use of tabs"), NULL, 20);
  ide_preferences_add_radio (preferences, "languages.id", "indentation", "org.gnome.builder.editor.language", "auto-indent", "/org/gnome/builder/editor/language/{id}/", NULL, _("Automatically indent"), _("Indent source code as you type"), NULL, 30);
}

static gboolean
workers_output (GtkSpinButton *button)
{
  GtkAdjustment *adj = gtk_spin_button_get_adjustment (button);

  if (gtk_adjustment_get_value (adj) == -1)
    {
      gtk_entry_set_text (GTK_ENTRY (button), _("Default"));
      return TRUE;
    }
  else if (gtk_adjustment_get_value (adj) == 0)
    {
      gtk_entry_set_text (GTK_ENTRY (button), _("Number of CPU"));
      return TRUE;
    }

  return FALSE;
}

static gint
workers_input (GtkSpinButton *button,
               gdouble       *new_value)
{
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (button));

  if (g_strcmp0 (text, _("Default")) == 0)
    {
      *new_value = -1;
      return TRUE;
    }
  else if (g_strcmp0 (text, _("Number of CPU")) == 0)
    {
      *new_value = 0;
      return TRUE;
    }

  return FALSE;
}

static void
ide_preferences_builtin_register_build (IdePreferences *preferences)
{
  GtkWidget *widget, *bin;
  guint id;

  ide_preferences_add_page (preferences, "build", _("Build"), 500);

  ide_preferences_add_list_group (preferences, "build", "basic", _("General"), 0);
  id = ide_preferences_add_spin_button (preferences, "build", "basic", "org.gnome.builder.build", "parallel", "/org/gnome/builder/build/", _("Build Workers"), _("Number of parallel build workers"), NULL, 0);

  bin = ide_preferences_get_widget (preferences, id);
  widget = ide_preferences_spin_button_get_spin_button (IDE_PREFERENCES_SPIN_BUTTON (bin));
  gtk_entry_set_width_chars (GTK_ENTRY (widget), 20);
  g_signal_connect (widget, "input", G_CALLBACK (workers_input), NULL);
  g_signal_connect (widget, "output", G_CALLBACK (workers_output), NULL);
}

static void
ide_preferences_builtin_register_projects (IdePreferences *preferences)
{
  ide_preferences_add_page (preferences, "projects", _("Projects"), 450);

  ide_preferences_add_list_group (preferences, "projects", "directory", _("Workspace"), 0);
  ide_preferences_add_file_chooser (preferences, "projects", "directory", "org.gnome.builder", "projects-directory", NULL, _("Projects directory"), _("A place for all your projects"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, NULL, 0);
  ide_preferences_add_switch (preferences, "projects", "directory", "org.gnome.builder", "restore-previous-files", NULL, NULL, _("Restore previously opened files"), _("Open previously opened files when loading a project"), NULL, 10);

  ide_preferences_add_list_group (preferences, "projects", "discovery", _("Project Discovery"), 0);
  ide_preferences_add_switch (preferences, "projects", "discovery", "org.gnome.builder", "enable-project-miners", NULL, NULL, _("Discover projects on my computer"), _("Scan your computer for existing projects"), NULL, 0);
}

static void
author_changed_cb (IdePreferencesEntry *entry,
                   const gchar         *text,
                   IdeVcsConfig        *conf)
{
  GValue value = G_VALUE_INIT;

  g_assert (IDE_IS_PREFERENCES_ENTRY (entry));
  g_assert (text != NULL);
  g_assert (IDE_IS_VCS_CONFIG (conf));

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, text);

  ide_vcs_config_set_config (conf, IDE_VCS_CONFIG_FULL_NAME, &value);

  g_value_unset (&value);
}

static void
email_changed_cb (IdePreferencesEntry *entry,
                  const gchar         *text,
                  IdeVcsConfig        *conf)
{
  GValue value = G_VALUE_INIT;

  g_assert (IDE_IS_PREFERENCES_ENTRY (entry));
  g_assert (text != NULL);
  g_assert (IDE_IS_VCS_CONFIG (conf));

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, text);

  ide_vcs_config_set_config (conf, IDE_VCS_CONFIG_EMAIL, &value);

  g_value_unset (&value);
}

static void
vcs_configs_foreach_cb (PeasExtensionSet *set,
                        PeasPluginInfo   *plugin_info,
                        PeasExtension    *exten,
                        gpointer          user_data)
{
  IdePreferences *preferences = user_data;
  IdeVcsConfig *conf = (IdeVcsConfig *)exten;
  GValue value = G_VALUE_INIT;
  GtkWidget *fullname;
  GtkWidget *email;
  GtkSizeGroup *size_group;
  g_autofree gchar *key = NULL;
  g_autofree gchar *author_name = NULL;
  g_autofree gchar *author_email = NULL;
  const gchar *name;
  const gchar *id;

  g_assert (PEAS_IS_EXTENSION_SET (set));
  g_assert (IDE_IS_PREFERENCES (preferences));
  g_assert (IDE_IS_VCS_CONFIG (conf));

  name = peas_plugin_info_get_name (plugin_info);
  id = peas_plugin_info_get_module_name (plugin_info);
  key = g_strdup_printf ("%s-config", id);

  g_object_set_data_full (G_OBJECT (preferences), key, g_object_ref (conf), g_object_unref);

  g_value_init (&value, G_TYPE_STRING);

  ide_vcs_config_get_config (conf, IDE_VCS_CONFIG_FULL_NAME, &value);
  author_name = g_strdup (g_value_get_string (&value));

  g_value_reset (&value);

  ide_vcs_config_get_config (conf, IDE_VCS_CONFIG_EMAIL, &value);
  author_email = g_strdup (g_value_get_string (&value));

  g_value_unset (&value);

  fullname = g_object_new (IDE_TYPE_PREFERENCES_ENTRY,
                           "text", ide_str_empty0 (author_name) ? "" : author_name,
                           "title", "Author",
                           "visible", TRUE,
                           NULL);

  g_signal_connect_object (fullname,
                           "changed",
                           G_CALLBACK (author_changed_cb),
                           conf,
                           0);

  email = g_object_new (IDE_TYPE_PREFERENCES_ENTRY,
                        "text", ide_str_empty0 (author_email) ? "" : author_email,
                        "title", "Email",
                        "visible", TRUE,
                        NULL);

  g_signal_connect_object (email,
                           "changed",
                           G_CALLBACK (email_changed_cb),
                           conf,
                           0);

  ide_preferences_add_list_group (preferences, "vcs", id, name, 0);
  ide_preferences_add_custom (preferences, "vcs", id, fullname, NULL, 0);
  ide_preferences_add_custom (preferences, "vcs", id, email, NULL, 0);

  size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  gtk_size_group_add_widget (size_group, ide_preferences_entry_get_title_widget (IDE_PREFERENCES_ENTRY (fullname)));
  gtk_size_group_add_widget (size_group, ide_preferences_entry_get_title_widget (IDE_PREFERENCES_ENTRY (email)));
  g_clear_object (&size_group);
}

static void
ide_preferences_builtin_register_vcs (IdePreferences *preferences)
{
  PeasEngine *engine;
  PeasExtensionSet *extensions;

  ide_preferences_add_page (preferences, "vcs", _("Version Control"), 600);

  engine = peas_engine_get_default ();
  extensions = peas_extension_set_new (engine, IDE_TYPE_VCS_CONFIG, NULL);
  peas_extension_set_foreach (extensions, vcs_configs_foreach_cb, preferences);
  g_clear_object (&extensions);
}

void
_ide_preferences_builtin_register (IdePreferences *preferences)
{
  ide_preferences_builtin_register_appearance (preferences);
  ide_preferences_builtin_register_editor (preferences);
  ide_preferences_builtin_register_languages (preferences);
  ide_preferences_builtin_register_code_insight (preferences);
  ide_preferences_builtin_register_snippets (preferences);
  ide_preferences_builtin_register_keyboard (preferences);
  ide_preferences_builtin_register_plugins (preferences);
  ide_preferences_builtin_register_build (preferences);
  ide_preferences_builtin_register_projects (preferences);
  ide_preferences_builtin_register_vcs (preferences);
}
