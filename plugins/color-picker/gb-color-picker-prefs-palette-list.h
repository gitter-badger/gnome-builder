/* gb-color-picker-prefs-palette-list.h
 *
 * Copyright (C) 2016 Sebastien Lafargue <slafargue@gnome.org>
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

#ifndef GB_COLOR_PICKER_PREFS_PALETTE_LIST_H
#define GB_COLOR_PICKER_PREFS_PALETTE_LIST_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GB_TYPE_COLOR_PICKER_PREFS_PALETTE_LIST (gb_color_picker_prefs_palette_list_get_type())

G_DECLARE_FINAL_TYPE (GbColorPickerPrefsPaletteList, gb_color_picker_prefs_palette_list, GB, COLOR_PICKER_PREFS_PALETTE_LIST, GtkBox)

GbColorPickerPrefsPaletteList    *gb_color_picker_prefs_palette_list_new             (void);
GtkListBox                       *gb_color_picker_prefs_palette_list_get_list_box    (GbColorPickerPrefsPaletteList     *self);

G_END_DECLS

#endif /* GB_COLOR_PICKER_PREFS_PALETTE_LIST_H */

