#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "gnutella.h"

#include "callbacks.h"
#include "interface.h"
#include "gui.h"
#include "support.h"
#include "search.h"
#include "share.h"
#include "sockets.h"
#include "hosts.h"
#include "downloads.h"
#include "misc.h"
#include "autodownload.h"
#include "dialog-filters.h"

gchar c_tmp[2048];

/*
 * Main window
 */

gboolean on_main_window_delete_event(GtkWidget * widget, GdkEvent * event,
									 gpointer user_data)
{
	gtk_gnutella_exit(0);
	return TRUE;
}

/*
 * Left part
 */

void on_clist_menu_select_row(GtkCList * clist, gint row, gint column,
							  GdkEvent * event, gpointer user_data)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook_main), row);
}

void on_button_stats_update_clicked(GtkButton * button, gpointer user_data)
{
	ping_stats_update();
}

void on_button_quit_clicked(GtkButton * button, gpointer user_data)
{
	gtk_gnutella_exit(0);
}

/*
 * gnutellaNet
 */

/* connections */

void cb_node_add(void)
{
	gchar *seek, *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_host)));
	guint32 port = 6346;

	g_strstrip(e);

	seek = e;

	while (*seek && *seek != ':' && *seek != ' ')
		seek++;

	if (*seek) {
		*seek++ = 0;
		while (*seek && (*seek == ':' || *seek == ' '))
			seek++;
		if (*seek)
			port = atol(seek);
	}

	if (port < 1 || port > 65535)
		printf("Bad host !\n");
	else {
		guint32 ip = host_to_ip(e);
		if (ip) {
			node_add(NULL, ip, port);
			gtk_entry_set_text(GTK_ENTRY(entry_host), "");
		}
	}

	g_free(e);
}

void on_button_nodes_add_clicked(GtkButton * button, gpointer user_data)
{
	cb_node_add();
}

void on_entry_host_activate(GtkEditable * editable, gpointer user_data)
{
	cb_node_add();
}

void on_entry_host_changed(GtkEditable * editable, gpointer user_data)
{
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
	g_strstrip(e);
	if (*e)
		gtk_widget_set_sensitive(button_nodes_add, TRUE);
	else
		gtk_widget_set_sensitive(button_nodes_add, FALSE);
	g_free(e);
}

void on_button_nodes_remove_clicked(GtkButton * button, gpointer user_data)
{
	if (GTK_CLIST(clist_nodes)->selection) {
		struct gnutella_node *n;
		GList *l = GTK_CLIST(clist_nodes)->selection;

		while (l) {
			n = (struct gnutella_node *)
				gtk_clist_get_row_data(GTK_CLIST(clist_nodes),
									   (gint) l->data);
			l = l->next;
			if (NODE_IS_WRITABLE(n))
				node_bye(n, 201, "User manual removal");
			node_remove(n, NULL);
			node_real_remove(n);
		}
	}
}

void on_clist_nodes_select_row(GtkCList * clist, gint row, gint column,
							   GdkEvent * event, gpointer user_data)
{
	gtk_widget_set_sensitive(button_nodes_remove, TRUE);
}

void on_clist_nodes_unselect_row(GtkCList * clist, gint row, gint column,
								 GdkEvent * event, gpointer user_data)
{
	gtk_widget_set_sensitive(button_nodes_remove,
							 (gboolean) GTK_CLIST(clist_nodes)->selection);
}

void on_clist_nodes_resize_column(GtkCList * clist, gint column,
								  gint width, gpointer user_data)
{
	nodes_col_widths[column] = width;
}

/* minimum connections up */

void on_entry_up_connections_activate(GtkEditable * editable,
									  gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_up_connections_focus_out_event(GtkWidget * widget,
												 GdkEventFocus * event,
												 gpointer user_data)
{
	guint32 v;
	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_up_connections)));
	g_strstrip(e);
	v = atol(e);
	g_free(e);
	if (v >= 0 && v < 512) {
		up_connections = v;
	}
	gui_update_up_connections();
	if (up_connections > max_connections) {
		max_connections = up_connections;
		gui_update_max_connections();
	}
	return TRUE;
}

/* nodes popup menu */

gboolean on_clist_nodes_button_press_event(GtkWidget * widget,
										   GdkEventButton * event,
										   gpointer user_data)
{
	if (event->button != 3)
		return FALSE;

	gtk_clist_unselect_all(GTK_CLIST(clist_nodes));

	gtk_menu_popup(GTK_MENU(popup_nodes), NULL, NULL, NULL, NULL, 3, 0);

	return TRUE;
}

void on_popup_nodes_title_activate(GtkMenuItem * menuitem,
								   gpointer user_data)
{
}

void on_button_host_catcher_clear_clicked(GtkButton * button,
										  gpointer user_data)
{
	host_clear_cache();
}

GtkWidget *hosts_write_filesel = (GtkWidget *) NULL;

gboolean fs_hosts_write_delete_event(GtkWidget * widget, GdkEvent * event,
									 gpointer user_data)
{
	gtk_widget_destroy(hosts_write_filesel);
	hosts_write_filesel = (GtkWidget *) NULL;
	return TRUE;
}

void button_fs_hosts_write_clicked(GtkButton * button, gpointer user_data)
{
	if (user_data)
		hosts_write_to_file(gtk_file_selection_get_filename
							(GTK_FILE_SELECTION(hosts_write_filesel)));

	gtk_widget_destroy(hosts_write_filesel);
	hosts_write_filesel = (GtkWidget *) NULL;
}

void on_popup_hosts_export_activate(GtkMenuItem * menuitem,
									gpointer user_data)
{
	if (!hosts_write_filesel) {
		hosts_write_filesel =
			gtk_file_selection_new
			("Please choose a file to save the catched hosts");

		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(hosts_write_filesel)->
							ok_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_hosts_write_clicked),
						   (gpointer) 1);
		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(hosts_write_filesel)->
							cancel_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_hosts_write_clicked),
						   NULL);
		gtk_signal_connect(GTK_OBJECT(hosts_write_filesel), "delete_event",
						   GTK_SIGNAL_FUNC(fs_hosts_write_delete_event),
						   NULL);

		gtk_widget_show(hosts_write_filesel);
	}
}

GtkWidget *hosts_read_filesel = (GtkWidget *) NULL;

gboolean fs_hosts_read_delete_event(GtkWidget * widget, GdkEvent * event,
									gpointer user_data)
{
	gtk_widget_destroy(hosts_read_filesel);
	hosts_read_filesel = (GtkWidget *) NULL;
	return TRUE;
}

void button_fs_hosts_read_clicked(GtkButton * button, gpointer user_data)
{
	if (user_data)
		hosts_read_from_file(gtk_file_selection_get_filename
							 (GTK_FILE_SELECTION(hosts_read_filesel)),
							 FALSE);

	gtk_widget_destroy(hosts_read_filesel);
	hosts_read_filesel = (GtkWidget *) NULL;
}

void on_popup_hosts_importe_activate(GtkMenuItem * menuitem,
									 gpointer user_data)
{
	if (!hosts_read_filesel) {
		hosts_read_filesel =
			gtk_file_selection_new("Please choose a text hosts file");

		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(hosts_read_filesel)->
							ok_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_hosts_read_clicked),
						   (gpointer) 1);
		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(hosts_read_filesel)->
							cancel_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_hosts_read_clicked),
						   NULL);
		gtk_signal_connect(GTK_OBJECT(hosts_read_filesel), "delete_event",
						   GTK_SIGNAL_FUNC(fs_hosts_read_delete_event),
						   NULL);

		gtk_widget_show(hosts_read_filesel);
	}
}

/*
 * Uploads
 */

void on_clist_uploads_select_row(GtkCList * clist, gint row, gint column,
								 GdkEvent * event, gpointer user_data)
{
	gui_update_upload_kill();
}

void on_clist_uploads_unselect_row(GtkCList * clist, gint row, gint column,
								   GdkEvent * event, gpointer user_data)
{
	gui_update_upload_kill();
}

void on_clist_uploads_click_column(GtkCList * clist, gint column,
								   gpointer user_data)
{
}

void on_clist_uploads_resize_column(GtkCList * clist, gint column,
									gint width, gpointer user_data)
{
	uploads_col_widths[column] = width;
}

void on_button_kill_upload_clicked(GtkButton * button, gpointer user_data)
{
	GList *l = NULL, *next;
	struct upload *d;

	l = GTK_CLIST(clist_uploads)->selection;

	while (l) {
		next = l->next;
		d = (struct upload *)
			gtk_clist_get_row_data(GTK_CLIST(clist_uploads),
								   (gint) l->data);

		if (!d) {
			g_warning
				("on_button_kill_uploadd_clicked(): row %d has NULL data\n",
				 (gint) l->data);
			continue;
		}

		if (!UPLOAD_IS_COMPLETE(d))
			socket_destroy(d->socket);
		l = next;
	}

	gui_update_count_uploads();
	gui_update_c_uploads();
	return;

}

void on_button_clear_uploads_clicked(GtkButton * button,
									 gpointer user_data)
{
	struct upload *d;
	gint row;

	for (row = 0;;) {
		d = gtk_clist_get_row_data(GTK_CLIST(clist_uploads), row);
		if (!d)
			break;
		if (UPLOAD_IS_COMPLETE(d))
			upload_remove(d, NULL);
		else
			row++;
	}
	gtk_widget_set_sensitive(button_clear_uploads, 0);
}

void on_checkbutton_clear_uploads_toggled(GtkToggleButton * togglebutton,
										  gpointer user_data)
{
	clear_uploads = gtk_toggle_button_get_active(togglebutton);
}

/* uploads popup menu */

gboolean on_clist_uploads_button_press_event(GtkWidget * widget,
											 GdkEventButton * event,
											 gpointer user_data)
{
	gint row, col;
	/* struct upload *d; */

	if (event->button != 3)
		return FALSE;

	if (!gtk_clist_get_selection_info
		(GTK_CLIST(clist_uploads), event->x, event->y, &row, &col))
		return FALSE;

#if 0
		/* XXX -- disabled for now */
		d = (struct upload *)
			gtk_clist_get_row_data(GTK_CLIST(clist_uploads), row);
		gtk_clist_unselect_all(GTK_CLIST(clist_uploads));
		gtk_widget_set_sensitive(button_kill_upload,
			!UPLOAD_IS_COMPLETE(d));
#endif

	gui_update_upload_kill();

	gtk_menu_popup(GTK_MENU(popup_uploads), NULL, NULL, NULL, NULL, 3, 0);

	return TRUE;
}

void on_popup_uploads_title_activate(GtkMenuItem * menuitem,
									 gpointer user_data)
{

}

/*
 * Downloads
 */

/* Active downloads clist */

void on_clist_downloads_select_row(GtkCList * clist, gint row, gint column,
								   GdkEvent * event, gpointer user_data)
{
	gui_update_download_abort_resume();
}

void on_clist_downloads_unselect_row(GtkCList * clist, gint row,
									 gint column, GdkEvent * event,
									 gpointer user_data)
{
	gui_update_download_abort_resume();
}

void on_clist_downloads_click_column(GtkCList * clist, gint column,
									 gpointer user_data)
{
}

void on_clist_downloads_resize_column(GtkCList * clist, gint column,
									  gint width, gpointer user_data)
{
	dl_active_col_widths[column] = width;
}

/* Active downloads popup menu */

gboolean on_clist_downloads_button_press_event(GtkWidget * widget,
											   GdkEventButton * event,
											   gpointer user_data)
{
	gint row, col;
	struct download *d;

	if (event->button != 3)
		return FALSE;

	if (!gtk_clist_get_selection_info
		(GTK_CLIST(clist_downloads), event->x, event->y, &row, &col))
		return FALSE;

	d = (struct download *)
		gtk_clist_get_row_data(GTK_CLIST(clist_downloads), row);

	strncpy(c_tmp + 24, "...", 4);
	strncpy(c_tmp, d->file_name, 24);

	gtk_label_set(GTK_LABEL
				  ((GTK_MENU_ITEM(popup_dl_active_title)->item.bin.child)),
				  c_tmp);

	gtk_widget_set_sensitive(download_p_push, !d->push);
	gtk_widget_set_sensitive(download_p_queue,
							 d->status != GTA_DL_COMPLETED);
	gtk_widget_set_sensitive(download_p_kill,
							 d->status != GTA_DL_COMPLETED);

	gtk_clist_unselect_all(GTK_CLIST(clist_downloads));

	gtk_menu_popup(GTK_MENU(popup_dl_active), NULL, NULL, NULL, NULL, 3,
				   0);

	selected_active_download = d;

	return TRUE;
}

void on_download_p_push_activate(GtkMenuItem * menuitem,
								 gpointer user_data)
{
	if (selected_active_download)
		download_fallback_to_push(selected_active_download, FALSE, TRUE);
	selected_active_download = (struct download *) NULL;
}

void on_download_p_queue_activate(GtkMenuItem * menuitem,
								  gpointer user_data)
{
	if (selected_active_download)
		download_queue(selected_active_download);
	selected_active_download = (struct download *) NULL;
}

void on_download_p_kill_activate(GtkMenuItem * menuitem,
								 gpointer user_data)
{
	if (selected_active_download)
		download_kill(selected_active_download);
	selected_active_download = (struct download *) NULL;
}

/* Active downloads buttons and entries */

void on_button_abort_download_clicked(GtkButton * button,
									  gpointer user_data)
{
	GList *l;
	struct download *d;

	for (l = GTK_CLIST(clist_downloads)->selection; l; l = l->next) {
		d = (struct download *)
			gtk_clist_get_row_data(GTK_CLIST(clist_downloads),
								   (gint) l->data);

		if (!d) {
			g_warning
				("on_button_abort_download_clicked(): row %d has NULL data\n",
				 (gint) l->data);
			continue;
		}

		download_abort(d);
	}
}

void on_button_resume_download_clicked(GtkButton * button,
									   gpointer user_data)
{
	GList *l;
	struct download *d;

	for (l = GTK_CLIST(clist_downloads)->selection; l; l = l->next) {
		d = (struct download *)
			gtk_clist_get_row_data(GTK_CLIST(clist_downloads),
								   (gint) l->data);

		if (!d) {
			g_warning
				("on_button_abort_download_clicked(): row %d has NULL data\n",
				 (gint) l->data);
			continue;
		}

		download_resume(d);
	}

	gui_update_download_abort_resume();
	gui_update_download_clear();
}

void on_button_clear_download_clicked(GtkButton * button,
									  gpointer user_data)
{
	downloads_clear_stopped(TRUE, TRUE);
}

void on_checkbutton_clear_downloads_toggled(GtkToggleButton * togglebutton,
											gpointer user_data)
{
	clear_downloads = gtk_toggle_button_get_active(togglebutton);
	if (clear_downloads)
		downloads_clear_stopped(FALSE, TRUE);
}

void on_entry_max_downloads_activate(GtkEditable * editable,
									 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_max_downloads_focus_out_event(GtkWidget * widget,
												GdkEventFocus * event,
												gpointer user_data)
{
	gint v = atol(gtk_entry_get_text(GTK_ENTRY(entry_max_downloads)));
	if (v > 0 && v < 512)
		max_downloads = v;

	/*
	 * XXX If the user modifies the max simulteneous download and click on a
	 * XXX queued download, gtk-gnutella segfaults in some cases.
	 * XXX This unselected_all() is a first attempt to work around the problem.
	 */

	gtk_clist_unselect_all(GTK_CLIST(clist_download_queue));

	gui_update_max_downloads();
	download_pickup_queued();

	return TRUE;
}

void on_entry_max_host_downloads_activate(GtkEditable * editable,
										  gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_max_host_downloads_focus_out_event(GtkWidget * widget,
													 GdkEventFocus * event,
													 gpointer user_data)
{
	gint v = atol(gtk_entry_get_text(GTK_ENTRY(entry_max_host_downloads)));
	if (v > 0 && v < 512)
		max_host_downloads = v;

	/*
	 * XXX If the user modifies the max simulteneous download and click on a
	 * XXX queued download, gtk-gnutella segfaults in some cases.
	 * XXX This unselected_all() is a first attempt to work around the problem.
	 */

	gtk_clist_unselect_all(GTK_CLIST(clist_download_queue));

	gui_update_max_host_downloads();
	download_pickup_queued();

	return TRUE;
}

/* Queued downloads */

void on_clist_download_queue_select_row(GtkCList * clist, gint row,
										gint column, GdkEvent * event,
										gpointer user_data)
{
	gtk_widget_set_sensitive(button_remove_download, TRUE);
}

void on_clist_download_queue_unselect_row(GtkCList * clist, gint row,
										  gint column, GdkEvent * event,
										  gpointer user_data)
{
	gtk_widget_set_sensitive(button_remove_download,
							 (gboolean) GTK_CLIST(clist_download_queue)->
							 selection);
}

void on_button_remove_download_clicked(GtkButton * button,
									   gpointer user_data)
{
	if (GTK_CLIST(clist_download_queue)->selection) {
		struct download *d;
		GList *l = GTK_CLIST(clist_download_queue)->selection;

		while (l) {
			d = (struct download *)
				gtk_clist_get_row_data(GTK_CLIST(clist_download_queue),
									   (gint) l->data);
			l = l->next;
			if (d->status == GTA_DL_QUEUED)
				download_free(d);
		}
	}
}

void on_clist_download_queue_click_column(GtkCList * clist, gint column,
										  gpointer user_data)
{
}

void on_download_start_now_activate(GtkMenuItem * menuitem,
									gpointer user_data)
{
	if (selected_queued_download)
		download_start(selected_queued_download, TRUE);
}

gboolean on_clist_download_queue_button_press_event(GtkWidget * widget,
													GdkEventButton * event,
													gpointer user_data)
{
	gint row, col;
	struct download *d;

	if (event->button != 3)
		return FALSE;

	if (!gtk_clist_get_selection_info
		(GTK_CLIST(clist_download_queue), event->x, event->y, &row, &col))
		return FALSE;

	d = (struct download *)
		gtk_clist_get_row_data(GTK_CLIST(clist_download_queue), row);

	strncpy(c_tmp + 24, "...", 4);
	strncpy(c_tmp, d->file_name, 24);

	gtk_label_set(GTK_LABEL
				  ((GTK_MENU_ITEM(popup_dl_queued_title)->item.bin.child)),
				  c_tmp);

	gtk_clist_unselect_all(GTK_CLIST(clist_download_queue));

	gtk_menu_popup(GTK_MENU(popup_dl_queued), NULL, NULL, NULL, NULL, 3,
				   0);

	if (d->status == GTA_DL_QUEUED)
		selected_queued_download = d;
	else
		g_warning("popup_dl_queued(): Unexpected download status %d !\n",
				  d->status);

	return TRUE;
}

void on_clist_download_queue_resize_column(GtkCList * clist, gint column,
										   gint width, gpointer user_data)
{
	dl_queued_col_widths[column] = width;
}

/*
 * Searches
 */

void on_entry_minimum_speed_activate(GtkEditable * editable,
									 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_minimum_speed_focus_out_event(GtkWidget * widget,
												GdkEventFocus * event,
												gpointer user_data)
{
	gint speed = atol(gtk_entry_get_text(GTK_ENTRY(entry_minimum_speed)));
	if (speed >= 0 && speed < 65536)
		minimum_speed = speed;
	/* XXX The minimum speed is now on a per search basis */
	gui_update_minimum_speed(minimum_speed);

	return TRUE;
}

void on_button_search_clicked(GtkButton * button, gpointer user_data)
{
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_search)));

	/*
	 * Even though we might not be on_the_net() yet, record the search.
	 * There is a callback mechanism when a new node is connected, which
	 * will launch the search there if it has not been sent already.
	 *		--patch from Mark Schreiber, 10/01/2002
	 */

	g_strstrip(e);
	if (*e)
		new_search(minimum_speed, e);
	gtk_widget_grab_focus(clist_menu);

	g_free(e);
}

void on_entry_search_activate(GtkEditable * editable, gpointer user_data)
{
	on_button_search_clicked(NULL, user_data);
}

void on_entry_search_changed(GtkEditable * editable, gpointer user_data)
{
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_search)));
	g_strstrip(e);
	gtk_widget_set_sensitive(button_search, (gboolean) (*e));
	g_free(e);
}

void on_button_search_close_clicked(GtkButton * button, gpointer user_data)
{
	search_close_current();
}

void on_button_search_download_clicked(GtkButton * button,
									   gpointer user_data)
{
	search_download_files();
}

void on_button_search_stream_clicked(GtkButton * button,
									 gpointer user_data)
{

}


/*
 * Monitor
 */

void on_checkbutton_monitor_toggled(GtkToggleButton * togglebutton,
									gpointer user_data)
{
	monitor_enabled = gtk_toggle_button_get_active(togglebutton);
}

void on_entry_monitor_activate(GtkEditable * editable, gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_monitor_focus_out_event(GtkWidget * widget,
										  GdkEventFocus * event,
										  gpointer user_data)
{
	guint32 v;
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_monitor)));
	g_strstrip(e);
	v = atol(e);
	if (v > 0 && v < 512) {
		if (v < monitor_max_items) {
			gtk_clist_clear(GTK_CLIST(clist_monitor));
			monitor_items = 0;
		}
		monitor_max_items = v;
	}
	gui_update_monitor_max_items();
	g_free(e);

	return TRUE;
}

/* Monitor popup menu */

void on_popup_monitor_title_activate(GtkMenuItem * menuitem,
									 gpointer user_data)
{
}

gboolean on_clist_monitor_button_press_event(GtkWidget * widget,
											 GdkEventButton * event,
											 gpointer user_data)
{
	if (event->button != 3)
		return FALSE;

	gtk_clist_unselect_all(GTK_CLIST(clist_monitor));

	gtk_menu_popup(GTK_MENU(popup_monitor), NULL, NULL, NULL, NULL, 3, 0);

	return TRUE;
}

/*
 * Config
 */

/* While downloading, store files to */

GtkWidget *save_path_filesel = NULL;

gboolean fs_save_path_delete_event(GtkWidget * widget, GdkEvent * event,
								   gpointer user_data)
{
	gtk_widget_destroy(save_path_filesel);
	save_path_filesel = NULL;
	return TRUE;
}

void button_fs_save_path_clicked(GtkButton * button, gpointer user_data)
{
	if (user_data) {
		gchar *name =
			gtk_file_selection_get_filename(GTK_FILE_SELECTION
											(save_path_filesel));

		if (is_directory(name)) {
			g_free(save_file_path);
			save_file_path = g_strdup(name);
		}

		gui_update_save_file_path();
	}

	gtk_widget_destroy(save_path_filesel);
	save_path_filesel = NULL;
}

void on_button_config_save_path_clicked(GtkButton * button,
										gpointer user_data)
{
	if (!save_path_filesel) {
		save_path_filesel =
			gtk_file_selection_new
			("Please choose where to store files while downloading");

		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(save_path_filesel)->
							ok_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_save_path_clicked),
						   (gpointer) 1);
		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(save_path_filesel)->
							cancel_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_save_path_clicked),
						   NULL);
		gtk_signal_connect(GTK_OBJECT(save_path_filesel), "delete_event",
						   GTK_SIGNAL_FUNC(fs_save_path_delete_event),
						   NULL);

		gtk_widget_show(save_path_filesel);
	}
}

/* Move downloaded files to */

GtkWidget *move_path_filesel = (GtkWidget *) NULL;

gboolean fs_save_move_delete_event(GtkWidget * widget, GdkEvent * event,
								   gpointer user_data)
{
	gtk_widget_destroy(move_path_filesel);
	move_path_filesel = (GtkWidget *) NULL;
	return TRUE;
}

void button_fs_move_path_clicked(GtkButton * button, gpointer user_data)
{
	if (user_data) {
		gchar *name =
			gtk_file_selection_get_filename(GTK_FILE_SELECTION
											(move_path_filesel));

		if (is_directory(name)) {
			g_free(move_file_path);
			move_file_path = g_strdup(name);
		}

		gui_update_move_file_path();
	}

	gtk_widget_destroy(move_path_filesel);
	move_path_filesel = (GtkWidget *) NULL;
}

void on_button_config_move_path_clicked(GtkButton * button,
										gpointer user_data)
{
	if (!move_path_filesel) {
		move_path_filesel =
			gtk_file_selection_new
			("Please choose where to move files after download");

		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(move_path_filesel)->
							ok_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_move_path_clicked),
						   (gpointer) 1);
		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(move_path_filesel)->
							cancel_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_move_path_clicked),
						   NULL);
		gtk_signal_connect(GTK_OBJECT(move_path_filesel), "delete_event",
						   GTK_SIGNAL_FUNC(fs_save_move_delete_event),
						   NULL);

		gtk_widget_show(move_path_filesel);
	}
}

/* Local File DB Managment */

GtkWidget *add_dir_filesel = NULL;

gboolean fs_add_dir_delete_event(GtkWidget * widget, GdkEvent * event,
								 gpointer user_data)
{
	gtk_widget_destroy(add_dir_filesel);
	add_dir_filesel = NULL;
	return TRUE;
}

void button_fs_add_dir_clicked(GtkButton * button, gpointer user_data)
{
	if (user_data) {
		gchar *name =
			gtk_file_selection_get_filename(GTK_FILE_SELECTION
											(add_dir_filesel));

		if (is_directory(name))
			shared_dir_add(name);

		gui_update_save_file_path();
	}

	gtk_widget_destroy(add_dir_filesel);
	add_dir_filesel = NULL;
}

void on_button_config_add_dir_clicked(GtkButton * button,
									  gpointer user_data)
{
	if (!add_dir_filesel) {
		add_dir_filesel =
			gtk_file_selection_new("Please choose a directory to share");

		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(add_dir_filesel)->
							ok_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_add_dir_clicked),
						   (gpointer) 1);
		gtk_signal_connect(GTK_OBJECT
						   (GTK_FILE_SELECTION(add_dir_filesel)->
							cancel_button), "clicked",
						   GTK_SIGNAL_FUNC(button_fs_add_dir_clicked),
						   NULL);
		gtk_signal_connect(GTK_OBJECT(add_dir_filesel), "delete_event",
						   GTK_SIGNAL_FUNC(fs_add_dir_delete_event), NULL);

		gtk_widget_show(add_dir_filesel);
	}
}

void on_button_config_rescan_dir_clicked(GtkButton * button,
										 gpointer user_data)
{
	gtk_widget_set_sensitive(button_config_rescan_dir, FALSE);
	share_scan();
	gtk_widget_set_sensitive(button_config_rescan_dir, TRUE);
}

void on_entry_config_path_activate(GtkEditable * editable,
								   gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_path_focus_out_event(GtkWidget * widget,
											  GdkEventFocus * event,
											  gpointer user_data)
{
	shared_dirs_parse(gtk_entry_get_text(GTK_ENTRY(entry_config_path)));
	gui_update_shared_dirs();
	return TRUE;
}

void on_entry_config_extensions_activate(GtkEditable * editable,
										 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_extensions_focus_out_event(GtkWidget * widget,
													GdkEventFocus * event,
													gpointer user_data)
{
	parse_extensions(gtk_entry_get_text
					 (GTK_ENTRY(entry_config_extensions)));
	gui_update_scan_extensions();
	return TRUE;
}

void on_checkbutton_config_force_ip_toggled(GtkToggleButton * togglebutton,
											gpointer user_data)
{
	force_local_ip = gtk_toggle_button_get_active(togglebutton);
}

void on_entry_config_force_ip_changed(GtkEditable * editable,
									  gpointer user_data)
{
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(editable)));
	guint32 ip;
	g_strstrip(e);
	ip = gchar_to_ip(e);
	gtk_widget_set_sensitive(checkbutton_config_force_ip, ip);
	g_free(e);
}

void on_entry_config_force_ip_activate(GtkEditable * editable,
									   gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_force_ip_focus_out_event(GtkWidget * widget,
												  GdkEventFocus * event,
												  gpointer user_data)
{
	gchar *e;
	guint32 ip;
	e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_force_ip)));
	g_strstrip(e);
	ip = gchar_to_ip(e);
	if (ip != forced_local_ip)
		forced_local_ip = ip;
	gui_update_config_force_ip();
	g_free(e);
	return TRUE;
}

void config_port_update_request(void)
{
	guint16 p;
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_port)));
	g_strstrip(e);
	p = atoi(e);
	if (listen_port != p) {
		if (s_listen)
			socket_destroy(s_listen);
		if (p)
			s_listen = socket_listen(0, p, GTA_TYPE_CONTROL);
		else
			s_listen = NULL;
		if (s_listen)
			listen_port = p;
		else
			listen_port = 0;

		gtk_widget_set_sensitive(button_config_update_port, FALSE);
		gtk_widget_grab_focus(clist_menu);
	}
	gui_update_config_port();
	g_free(e);
}

void on_entry_config_port_activate(GtkEditable * editable,
								   gpointer user_data)
{
	config_port_update_request();
}

void on_entry_config_port_changed(GtkEditable * editable,
								  gpointer user_data)
{
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_port)));
	g_strstrip(e);
	g_snprintf(c_tmp, sizeof(c_tmp), "%d", listen_port);
	gtk_widget_set_sensitive(button_config_update_port,
							 (gboolean) g_strcasecmp(c_tmp, e));
	g_free(e);
}

void on_button_config_update_port_clicked(GtkButton * button,
										  gpointer user_data)
{
	config_port_update_request();
}

void on_checkbutton_config_throttle_toggled(GtkToggleButton * togglebutton,
											gpointer user_data)
{
}

void on_entry_config_maxttl_activate(GtkEditable * editable,
									 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_maxttl_focus_out_event(GtkWidget * widget,
												GdkEventFocus * event,
												gpointer user_data)
{
	guint32 v;
	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_maxttl)));
	g_strstrip(e);
	v = atoi(e);
	if (v > 0 && v < 255)
		max_ttl = v;
	gui_update_max_ttl();
	g_free(e);
	return TRUE;
}

void on_entry_config_myttl_activate(GtkEditable * editable,
									gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_myttl_focus_out_event(GtkWidget * widget,
											   GdkEventFocus * event,
											   gpointer user_data)
{
	guint32 v;
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_myttl)));
	g_strstrip(e);
	v = atoi(e);
	if (v > 0 && v < 255)
		my_ttl = v;
	gui_update_my_ttl();
	g_free(e);
	return TRUE;
}

void on_entry_config_speed_activate(GtkEditable * editable,
									gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_speed_focus_out_event(GtkWidget * widget,
											   GdkEventFocus * event,
											   gpointer user_data)
{
	guint32 v;
	gchar *e = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_speed)));
	g_strstrip(e);
	v = atol(e);
	if (v > 0 && v < 65535)
		connection_speed = v;
	gui_update_connection_speed();
	g_free(e);
	return TRUE;
}

void on_entry_config_search_items_activate(GtkEditable * editable,
										   gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_config_search_items_focus_out_event(GtkWidget * widget,
													  GdkEventFocus *
													  event,
													  gpointer user_data)
{
	gint32 v;
	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_config_search_items)));
	g_strstrip(e);
	v = atol(e);
	if (v >= -1 && v < 256)
		search_max_items = v;
	gui_update_search_max_items();
	g_free(e);
	return TRUE;
}

void on_button_extra_config_clicked(GtkButton * button, gpointer user_data)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook_main), 6);
}

void on_button_search_passive_clicked(GtkButton * button,
									  gpointer user_data)
{
	struct search *sch;
	sch = _new_search(minimum_speed, "Passive", SEARCH_PASSIVE);
	gtk_widget_grab_focus(clist_menu);
}

void on_checkbutton_never_push_toggled(GtkToggleButton * togglebutton,
									   gpointer user_data)
{
	send_pushes = !gtk_toggle_button_get_active(togglebutton);
}

void on_checkbutton_jump_to_downloads_toggled(GtkToggleButton *
											  togglebutton,
											  gpointer user_data)
{
	jump_to_downloads = gtk_toggle_button_get_active(togglebutton);
}

void on_checkbutton_autodownload_toggled(GtkToggleButton *togglebutton,
										 gpointer user_data)
{
	use_autodownload = gtk_toggle_button_get_active(togglebutton);
	if (use_autodownload)
		autodownload_init();
}

void on_entry_max_uploads_activate(GtkEditable * editable,
								   gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_max_uploads_focus_out_event(GtkWidget * widget,
											  GdkEventFocus * event,
											  gpointer user_data)
{
	gint v = atol(gtk_entry_get_text(GTK_ENTRY(entry_max_uploads)));
	if (v >= 0 && v < 512)
		max_uploads = v;

	gui_update_max_uploads();

	return TRUE;
}

static void search_reissue_timeout_changed(GtkEntry * entry)
{
	guint v = atol(gtk_entry_get_text(entry));

	if (v > 0 && v < 300)	/* v == 0 means: no reissue */
		v = 300;			/* Have to be reasonable -- RAM, 30/12/2001 */

	if (v < ((guint32) - 1) / 1000)
		search_update_reissue_timeout(v);

	search_reissue_timeout = v;

	gui_update_search_reissue_timeout();
}

void on_entry_search_reissue_timeout_activate(GtkEditable * editable,
											  gpointer user_data)
{
	search_reissue_timeout_changed(GTK_ENTRY(editable));
}

gboolean on_entry_search_reissue_timeout_focus_out_event(GtkWidget *
														 widget,
														 GdkEventFocus *
														 event,
														 gpointer
														 user_data)
{
	search_reissue_timeout_changed(GTK_ENTRY(widget));
	return TRUE;
}

void on_config_entry_socks_host_activate(GtkEditable * editable,
										 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_config_entry_socks_host_focus_out_event(GtkWidget * widget,
													GdkEventFocus * event,
													gpointer user_data)
{

	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(config_entry_socks_host)));
	g_strstrip(e);


	if (strlen(e) < 2)
		g_free(e);
	else {
		proxy_ip = g_strdup(e);
		g_free(e);
	}
	return TRUE;
}

void on_config_entry_socks_port_activate(GtkEditable * editable,
										 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_config_entry_socks_port_focus_out_event(GtkWidget * widget,
													GdkEventFocus * event,
													gpointer user_data)
{
	gint16 v;
	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(config_entry_socks_port)));
	g_strstrip(e);

	v = atoi(e);
	if (v >= -1 && v < 32000)
		proxy_port = v;

	return TRUE;

}

void on_config_entry_socks_username_activate(GtkEditable * editable,
											 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_config_entry_socks_username_focus_out_event(GtkWidget * widget,
														GdkEventFocus *
														event,
														gpointer user_data)
{

	gchar *e =
		g_strdup(gtk_entry_get_text
				 (GTK_ENTRY(config_entry_socks_username)));
	g_strstrip(e);

	socksv5_user = g_strdup(e);

	g_free(e);
	return TRUE;
}

void on_config_entry_socks_password_activate(GtkEditable * editable,
											 gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_config_entry_socks_password_focus_out_event(GtkWidget * widget,
														GdkEventFocus *
														event,
														gpointer user_data)
{

	gchar *e =
		g_strdup(gtk_entry_get_text
				 (GTK_ENTRY(config_entry_socks_password)));
	g_strstrip(e);

	socksv5_pass = g_strdup(e);

	g_free(e);
	return TRUE;
}


void on_checkbutton_proxy_connections_toggled(GtkToggleButton *
											  togglebutton,
											  gpointer user_data)
{
	proxy_connections = gtk_toggle_button_get_active(togglebutton);

}

void on_radio_socksv4_toggled(GtkToggleButton * togglebutton,
							  gpointer user_data)
{
	if (gtk_toggle_button_get_active(togglebutton))
		socks_protocol = 4;
}

void on_radio_socksv5_toggled(GtkToggleButton * togglebutton,
							  gpointer user_data)
{
	if (gtk_toggle_button_get_active(togglebutton))
		socks_protocol = 5;
}

void on_entry_max_connections_activate(GtkEditable * editable,
									   gpointer user_data)
{
	/* This will generate a focus out event (next func) */
	gtk_widget_grab_focus(clist_menu);
}

gboolean on_entry_max_connections_focus_out_event(GtkWidget * widget,
												  GdkEventFocus * event,
												  gpointer user_data)
{
	guint32 v;
	gchar *e =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_max_connections)));
	g_strstrip(e);
	v = atol(e);
	g_free(e);
	if (v >= 0 && v < 512 && v >= up_connections)
		max_connections = v;
	gui_update_max_connections();
	return TRUE;
}

/*
 * search callbacks
 */

/* ----------------------------------------- */

static gint search_results_compare_size(GtkCList * clist, gconstpointer ptr1,
								 gconstpointer ptr2)
{
	guint32 s1 = ((struct record *) ((GtkCListRow *) ptr1)->data)->size;
	guint32 s2 = ((struct record *) ((GtkCListRow *) ptr2)->data)->size;

	return (s1 == s2) ? 0 :
		(s1 > s2) ? +1 : -1;
}

static gint search_results_compare_speed(GtkCList * clist, gconstpointer ptr1,
								  gconstpointer ptr2)
{
	struct results_set *rs1 =
		((struct record *) ((GtkCListRow *) ptr1)->data)->results_set;
	struct results_set *rs2 =
		((struct record *) ((GtkCListRow *) ptr2)->data)->results_set;

	return (rs1->speed == rs2->speed) ? 0 :
		(rs1->speed > rs2->speed) ? +1 : -1;
}

static gint search_results_compare_host(GtkCList * clist, gconstpointer ptr1,
							   gconstpointer ptr2)
{
	struct results_set *rs1 =
		((struct record *) ((GtkCListRow *) ptr1)->data)->results_set;
	struct results_set *rs2 =
		((struct record *) ((GtkCListRow *) ptr2)->data)->results_set;

	if (rs1->ip == rs2->ip)
		return (gint) rs1->port - (gint) rs2->port;
	else
		return (rs1->ip > rs2->ip) ? +1 : -1;
}

/* ----------------------------------------- */

/* Row selected */

static gint select_all_lock = 0;

void on_clist_search_results_select_row(GtkCList * clist, gint row,
										gint column, GdkEvent * event,
										gpointer user_data)
{
	gtk_widget_set_sensitive(button_search_download, TRUE);

	if (search_pick_all) {		// config setting select all is on
		if (!select_all_lock) {
			struct record *rc, *rc2;
			gint x, i;
			// this will be called for each selection, so only do it here
			select_all_lock = 1;
			rc = (struct record *) gtk_clist_get_row_data(clist, row);
			x = 1;
			for (i = 0; i < clist->rows; i++) {
				if (i == row)
					continue;	// skip this one
				rc2 = (struct record *) gtk_clist_get_row_data(clist, i);
				// if name match and file is same or larger, select it
				if (rc2)
					if (!strcmp(rc2->name, rc->name)) {
						if (rc2->size >= rc->size) {
							gtk_clist_select_row(clist, i, 0);
							x++;
						}
					}
			}
			g_snprintf(c_tmp, sizeof(c_tmp),
					   "		(%d auto selected)", x);
			gtk_label_set(GTK_LABEL(label_left), c_tmp);
			select_all_lock = 0;		// we are done, un "lock" it
		}
	}
}

/* Row unselected */

void on_clist_search_results_unselect_row(GtkCList * clist, gint row,
										  gint column, GdkEvent * event,
										  gpointer user_data)
{
	gboolean sensitive;

	sensitive = current_search
		&& GTK_CLIST(current_search->clist)->selection;
	gtk_widget_set_sensitive(button_search_download, sensitive);
	if (search_pick_all)
		gtk_label_set(GTK_LABEL(label_left), "");
}

/* Column title clicked */

void on_clist_search_results_click_column(GtkCList * clist, gint column,
										  gpointer user_data)
{
	if (current_search == NULL)
		return;

	switch (column) {
	case 1:		/* Size */
		gtk_clist_set_compare_func(GTK_CLIST(current_search->clist),
								   search_results_compare_size);
		break;
	case 2:		/* Speed */
		gtk_clist_set_compare_func(GTK_CLIST(current_search->clist),
								   search_results_compare_speed);
		break;
	case 3:		/* Host */
		gtk_clist_set_compare_func(GTK_CLIST(current_search->clist),
								   search_results_compare_host);
		break;
	default:
		gtk_clist_set_compare_func(GTK_CLIST(current_search->clist), NULL);
	}

	if (column == current_search->sort_col) {
		current_search->sort_order =
			(current_search->sort_order > 0) ? -1 : 1;
	} else {
		current_search->sort_col = column;
		current_search->sort_order = 1;
	}

	gtk_clist_set_sort_type(GTK_CLIST(current_search->clist),
		(current_search->sort_order > 0) ?
			GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
	gtk_clist_set_sort_column(GTK_CLIST(current_search->clist), column);

	gtk_clist_sort(GTK_CLIST(current_search->clist));

	current_search->sort = TRUE;
}

/* Search results popup menu (glade puts funcs prototypes in callbacks.h) */

void on_popup_search_stop_sorting_activate(GtkMenuItem * menuitem,
										   gpointer user_data)
{
	if (current_search)
		current_search->sort = FALSE;
}

void on_popup_search_filters_activate(GtkMenuItem * menuitem,
									  gpointer user_data)
{
	filters_open_dialog();
}

void on_popup_search_close_activate(GtkMenuItem * menuitem,
									gpointer user_data)
{
	if (current_search)
		search_close_current();
}

void on_popup_search_toggle_tabs_activate(GtkMenuItem * menuitem,
										  gpointer user_data)
{
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook_search_results),
		(search_results_show_tabs = !search_results_show_tabs));
}

void on_popup_search_restart_activate(GtkMenuItem * menuitem,
									  gpointer user_data)
{
	if (current_search)
		search_restart(current_search);
}

void on_popup_search_duplicate_activate(GtkMenuItem * menuitem,
										gpointer user_data)
{
	if (current_search)
		new_search(current_search->speed, current_search->query);
}

void on_popup_search_stop_activate(GtkMenuItem * menuitem,
								   gpointer user_data)
{
	if (current_search) {
		gtk_widget_set_sensitive(popup_search_stop, FALSE);
		gtk_widget_set_sensitive(popup_search_resume, TRUE);
		search_stop(current_search);
	}
}

void on_popup_search_resume_activate(GtkMenuItem * menuitem,
									 gpointer user_data)
{
	if (current_search) {
		gtk_widget_set_sensitive(popup_search_stop, TRUE);
		gtk_widget_set_sensitive(popup_search_resume, FALSE);
		search_resume(current_search);
	}
}

gboolean on_clist_search_results_button_press_event(GtkWidget * widget,
													GdkEventButton * event,
													gpointer user_data)
{
	gint row = 0;
	gint column = 0;
	static guint click_time = 0;

	switch (event->button) {
	case 1:
		if (event->type == GDK_2BUTTON_PRESS) {
			gtk_signal_emit_stop_by_name(GTK_OBJECT(widget),
				"button_press_event");
			return FALSE;
		}
		if (event->type == GDK_BUTTON_PRESS) {
			if ((event->time - click_time) <= 250) {
				/*
				 * 2 clicks within 250 msec == doubleclick.
				 * Surpress further events
				 */
				gtk_signal_emit_stop_by_name(GTK_OBJECT(widget),
					"button_press_event");
				if (
					gtk_clist_get_selection_info(GTK_CLIST(widget), event->x,
						event->y, &row, &column)
				) {
					/*
					 * Manually reselect to force the autoselection to behave
					 * correctly.
					 */
					gtk_clist_select_row(GTK_CLIST(widget), row, column);
					search_download_files();

					/* Remove focus from the List widget. Purely aesthetic. */
					gtk_widget_grab_focus(GTK_WIDGET(button_search_download));
					return TRUE;
				}
			} else {
				click_time = event->time;
				return FALSE;
			}
		}
		return FALSE;

	case 3:
		gtk_widget_set_sensitive(popup_search_toggle_tabs,
			(gboolean) searches);
		gtk_widget_set_sensitive(popup_search_close, (gboolean) searches);
		gtk_widget_set_sensitive(popup_search_restart, (gboolean) searches);
		gtk_widget_set_sensitive(popup_search_duplicate, (gboolean) searches);

		if (current_search) {
			gtk_clist_unselect_all(GTK_CLIST(current_search->clist));
			gtk_widget_set_sensitive(popup_search_stop_sorting,
				current_search->sort);
			gtk_widget_set_sensitive(popup_search_stop,
				current_search->passive ?
					!current_search->frozen :
					current_search->reissue_timeout);
			gtk_widget_set_sensitive(popup_search_resume,
				current_search->passive ?
					current_search->frozen :
					!current_search->reissue_timeout);
			if (current_search->passive)
				gtk_widget_set_sensitive(popup_search_restart, FALSE);
			g_snprintf(c_tmp, sizeof(c_tmp), "%s", current_search->query);
		} else {
			gtk_widget_set_sensitive(popup_search_stop_sorting, FALSE);
			gtk_widget_set_sensitive(popup_search_stop, FALSE);
			gtk_widget_set_sensitive(popup_search_resume, FALSE);
			g_snprintf(c_tmp, sizeof(c_tmp), "No current search");
		}

		gtk_label_set(
			GTK_LABEL((GTK_MENU_ITEM(popup_search_title)->item.bin.child)),
			c_tmp);
		g_snprintf(c_tmp, sizeof(c_tmp),
			(search_results_show_tabs) ? "Hide tabs" : "Show tabs");
		gtk_label_set(
			GTK_LABEL((GTK_MENU_ITEM(popup_search_toggle_tabs)->item.bin.child)),
			c_tmp);
		gtk_menu_popup(GTK_MENU(popup_search), NULL, NULL, NULL, NULL, 3, 0);
		return TRUE;

	default:
		break;
	}

	return FALSE;
}

/* Column resize */

void on_clist_search_results_resize_column(GtkCList * clist, gint column,
										   gint width, gpointer user_data)
{
	static gboolean resizing = FALSE;
	GSList *l;

	if (resizing)
		return;

	resizing = TRUE;

	search_results_col_widths[column] = width;

	for (l = searches; l; l = l->next)
		gtk_clist_set_column_width(GTK_CLIST
								   (((struct search *) l->data)->clist),
								   column, width);

	resizing = FALSE;
}

void on_search_selected(GtkItem * i, gpointer data)
{
	search_selected = (struct search *) data;
}

static gboolean updating = FALSE;

void on_search_switch(struct search *sch)
{
	struct search *old_sch = current_search;
	g_return_if_fail(sch);

	current_search = sch;
	sch->unseen_items = 0;

	if (old_sch)
		gui_search_force_update_tab_label(old_sch);
	gui_search_force_update_tab_label(sch);

	gui_search_update_items(sch);
	gui_update_minimum_speed(sch->speed);
	gtk_widget_set_sensitive(button_search_download,
							 (gboolean) GTK_CLIST(sch->clist)->selection);

	if (sch->items == 0) {
		gtk_widget_set_sensitive(button_search_clear, FALSE);
		gtk_widget_set_sensitive(popup_search_clear_results, FALSE);
	} else {
		gtk_widget_set_sensitive(button_search_clear, TRUE);
		gtk_widget_set_sensitive(popup_search_clear_results, TRUE);
	}

	gtk_widget_set_sensitive(popup_search_restart, !sch->passive);
	gtk_widget_set_sensitive(popup_search_duplicate, !sch->passive);
	gtk_widget_set_sensitive(popup_search_stop, sch->passive ?
							 !sch->frozen : sch->reissue_timeout);
	gtk_widget_set_sensitive(popup_search_resume, sch->passive ?
							 sch->frozen : sch->reissue_timeout);
}

void on_search_popdown_switch(GtkWidget * w, gpointer data)
{
	struct search *sch = search_selected;
	if (!sch || updating)
		return;
	updating = TRUE;
	on_search_switch(sch);
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook_search_results),
						  gtk_notebook_page_num(GTK_NOTEBOOK
												(notebook_search_results),
												sch->scrolled_window));
	updating = FALSE;
}

void on_search_notebook_switch(GtkNotebook * notebook,
							   GtkNotebookPage * page, gint page_num,
							   gpointer user_data)
{
	struct search *sch =
		gtk_object_get_user_data((GtkObject *) page->child);
	g_return_if_fail(sch);
	if (updating)
		return;
	updating = TRUE;
	on_search_switch(sch);
	gtk_list_item_select(GTK_LIST_ITEM(sch->list_item));
	updating = FALSE;
}

void on_button_search_clear_clicked(GtkButton * button, gpointer user_data)
{
	gui_search_clear_results();

	gtk_widget_set_sensitive(button_search_clear, FALSE);
	gtk_widget_set_sensitive(popup_search_clear_results, FALSE);

}

void
on_popup_search_clear_results_activate(GtkMenuItem * menuitem,
									   gpointer user_data)
{
	gui_search_clear_results();

	gtk_widget_set_sensitive(button_search_clear, FALSE);
	gtk_widget_set_sensitive(popup_search_clear_results, FALSE);

}

/* vi: set ts=4: */
