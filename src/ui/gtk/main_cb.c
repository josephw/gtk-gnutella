/*
 * $Id$
 *
 * Copyright (c) 2001-2003, Raphael Manfredi, Richard Eckart
 *
 *----------------------------------------------------------------------
 * This file is part of gtk-gnutella.
 *
 *  gtk-gnutella is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  gtk-gnutella is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gtk-gnutella; if not, write to the Free Software
 *  Foundation, Inc.:
 *      59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *----------------------------------------------------------------------
 */

#include "gui.h"

RCSID("$Id$");

#include "main_cb.h"
#include "main.h"
#include "notebooks.h"

#include "if/gui_property.h"
#include "if/bridge/ui2c.h"

#include "lib/override.h"	/* Must be the last header included */

/***
 *** Private functions
 ***/

static void quit(gboolean force)
{
    gboolean confirm;

    gui_prop_get_boolean(PROP_CONFIRM_QUIT, &confirm, 0, 1);
    if (force || !confirm)
       	guc_gtk_gnutella_exit(0);
    else
        gtk_widget_show(dlg_quit);
}

/***
 *** Main window
 ***/

gboolean
on_main_window_delete_event(GtkWidget *unused_widget, GdkEvent *unused_event,
		gpointer unused_udata)
{
	(void) unused_widget;
	(void) unused_event;
	(void) unused_udata;

    quit(FALSE);
	return TRUE;
}

void
on_button_quit_clicked(GtkButton *unused_button, gpointer unused_udata)
{
	(void) unused_button;
	(void) unused_udata;

    quit(FALSE);
}



/***
 *** menu bar
 ***/

void
on_menu_about_activate(GtkMenuItem *unused_menuitem, gpointer unused_udata)
{
	(void) unused_menuitem;
	(void) unused_udata;

    gtk_widget_show(dlg_about);
}

void
on_menu_prefs_activate(GtkMenuItem *unused_menuitem, gpointer unused_udata)
{
	(void) unused_menuitem;
	(void) unused_udata;

    gtk_widget_show(dlg_prefs);
}



/***
 *** about dialog
 ***/

void
on_button_about_close_clicked(GtkButton *unused_button, gpointer unused_udata)
{
	(void) unused_button;
	(void) unused_udata;

    gtk_widget_hide(dlg_about);
}

gboolean
on_dlg_about_delete_event(GtkWidget *unused_widget, GdkEvent *unused_event,
	gpointer unused_udata)
{
	(void) unused_widget;
	(void) unused_event;
	(void) unused_udata;

	gtk_widget_hide(dlg_about);
	return TRUE;
}

/***
 *** prefs dialog
 ***/

void
on_button_prefs_close_clicked(GtkButton *unused_button, gpointer unused_udata)
{
	(void) unused_button;
	(void) unused_udata;

    gtk_widget_hide(dlg_prefs);
}

gboolean
on_dlg_prefs_delete_event(GtkWidget *unused_widget, GdkEvent *unused_event,
	gpointer unused_udata)
{
	(void) unused_widget;
	(void) unused_event;
	(void) unused_udata;

	gtk_widget_hide(dlg_prefs);
	return TRUE;
}


/***
 *** Quit dialog
 ***/

void
on_button_really_quit_clicked(GtkButton *unused_button, gpointer unused_udata)
{
	(void) unused_button;
	(void) unused_udata;

    gtk_widget_hide(dlg_quit);
	quit(TRUE);
}

void
on_button_abort_quit_clicked(GtkButton *unused_button, gpointer unused_udata)
{
	(void) unused_button;
	(void) unused_udata;

    gtk_widget_hide(dlg_quit);
}

gboolean
on_dlg_quit_delete_event(GtkWidget *unused_widget, GdkEvent *unused_event,
	gpointer unused_udata)
{
	(void) unused_widget;
	(void) unused_event;
	(void) unused_udata;

    gtk_widget_hide(dlg_quit);
    return TRUE;
}

#ifdef USE_GTK2
void
on_main_gui_treeview_menu_cursor_changed(GtkTreeView *treeview,
	gpointer unused_udata)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gint tab = 0;

	(void) unused_udata;
    g_assert(treeview != NULL);

    selection = gtk_tree_view_get_selection(treeview);
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 1, &tab, (-1));
        gtk_notebook_set_page
            (GTK_NOTEBOOK(lookup_widget(main_window, "notebook_main")), tab);
    }
}

void
on_main_gui_treeview_menu_row_collapsed(GtkTreeView *tree, GtkTreeIter *iter,
		GtkTreePath *unused_path, gpointer unused_data)
{
	GtkTreeModel *model;
    gint id = 0;
	guint32 expanded = FALSE;

	(void) unused_path;
	(void) unused_data;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, 2, &id, (-1));
	g_assert(id >= 0 && id < TREEMENU_NODES);
	gui_prop_set_guint32(PROP_TREEMENU_NODES_EXPANDED, &expanded, id, 1);
}

void
on_main_gui_treeview_menu_row_expanded(GtkTreeView *tree, GtkTreeIter *iter,
	GtkTreePath *unused_path, gpointer unused_data)
{
	GtkTreeModel *model;
    gint id = 0;
	guint32 expanded = TRUE;

	(void) unused_path;
	(void) unused_data;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, 2, &id, (-1));
	g_assert(id >= 0 && id < TREEMENU_NODES);
	gui_prop_set_guint32(PROP_TREEMENU_NODES_EXPANDED, &expanded, id, 1);
}

#endif /* USE_GTK2 */

/* vi: set ts=4 sw=4 cindent: */
