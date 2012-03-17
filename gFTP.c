#include <geanyplugin.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "ftpparse.c"

GeanyPlugin *geany_plugin;
GeanyData *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO("gFTP", "FTP", "1.0", "Cai Guanhao <caiguanhao@gmail.com>");

enum
{
	FILEVIEW_COLUMN_ICON = 0,
	FILEVIEW_COLUMN_NAME,
	FILEVIEW_COLUMN_SIZE,
	FILEVIEW_COLUMN_FILENAME,
	FILEVIEW_N_COLUMNS
};

static GtkWidget *box;
static GtkWidget *file_view;
static GtkWidget *url_entry;
static GtkWidget *btn_connect, *btn_go_up;
static GtkListStore *file_store;
static CURL *curl;
static gchar *current_url = NULL;

GList *filelist;
GList *dirlist;

struct string {
	char *ptr;
	int len;
};

size_t write_function(void *ptr, size_t size, size_t nmemb, struct string *str)
{
	int new_len = str->len + size * nmemb;
	str->ptr = realloc(str->ptr, new_len + 1);
	memcpy(str->ptr + str->len, ptr, size * nmemb);
	str->ptr[new_len] = '\0';
	str->len = new_len;
	return size*nmemb;
}

static void msgwin_scroll_to_bottom()
{
	GtkTreeView *MsgWin;
	MsgWin = GTK_TREE_VIEW(ui_lookup_widget(geany->main_widgets->window, "treeview4"));
	int n = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(MsgWin),NULL);
	GtkTreePath *path;
	path = gtk_tree_path_new_from_string(g_strdup_printf("%d",n-1));
	gtk_tree_view_scroll_to_cell(MsgWin, path, NULL, FALSE, 0.0, 0.0);
}

static int ftp_log(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	char * odata;
	odata = g_strstrip(g_strdup_printf("%s", data));
	char * firstline;
	firstline = strtok(odata,"\r\n");
	int msgadd=0;
	gdk_threads_enter();
	switch (type) {
		case CURLINFO_TEXT:
			msgwin_msg_add(COLOR_BLUE, -1, NULL, "%s", firstline);
			msgadd=1;
			break;
		default:
			break;
		case CURLINFO_HEADER_OUT:
			msgwin_msg_add(COLOR_BLUE, -1, NULL, "%s", firstline);
			msgadd=1;
			break;
		case CURLINFO_DATA_OUT:
			break;
		case CURLINFO_SSL_DATA_OUT:
			break;
		case CURLINFO_HEADER_IN:
			msgwin_msg_add(COLOR_BLACK, -1, NULL, "%s", firstline);
			msgadd=1;
			break;
		case CURLINFO_DATA_IN:
			break;
		case CURLINFO_SSL_DATA_IN:
			break;
	}
	
	if (msgadd==1) {
		msgwin_scroll_to_bottom();
	}
	gdk_threads_leave();
	return 0;
}

static gboolean is_single_selection(GtkTreeSelection *treesel)
{
	if (gtk_tree_selection_count_selected_rows(treesel) == 1)
		return TRUE;

	ui_set_statusbar(FALSE, _("Too many items selected!"));
	return FALSE;
}

static gboolean is_folder_selected(GList *selected_items)
{
	GList *item;
	GtkTreeModel *model = GTK_TREE_MODEL(file_store);
	gboolean dir_found = FALSE;
	for (item = selected_items; item != NULL; item = g_list_next(item)) {
		gchar *icon;
		GtkTreeIter iter;
		GtkTreePath *treepath;
		treepath = (GtkTreePath*) item->data;
		gtk_tree_model_get_iter(model, &iter, treepath);
		gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_ICON, &icon, -1);
		if (utils_str_equal(icon, GTK_STOCK_DIRECTORY))	{
			dir_found = TRUE;
			g_free(icon);
			break;
		}
		g_free(icon);
	}
	return dir_found;
}

static int add_item(gpointer data, gboolean is_dir)
{
	gchar **parts=g_regex_split_simple("\n", data, 0, 0);
	if (strcmp(parts[0],".")==0||strcmp(parts[0],"..")==0) return 1;
	GtkTreeIter iter;
	gtk_list_store_append(file_store, &iter);
	if (strcmp(parts[1],"1")==0) { // for links with file name like mozilla -> .
		GRegex *regex;
		regex = g_regex_new("\\s->\\s", 0, 0, NULL);
		if (g_regex_match(regex, parts[0], 0, NULL)) {
			gchar **nameparts=g_regex_split(regex, parts[0], 0);
			sprintf(parts[0], "%s", nameparts[0]);
			g_strfreev(nameparts);
			sprintf(parts[1], "link");
		}
	} else if (strcmp(parts[1],"0")==0 && is_dir) {
		sprintf(parts[1], "%s", "");
	}
	gtk_list_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, is_dir?GTK_STOCK_DIRECTORY:GTK_STOCK_FILE,
	FILEVIEW_COLUMN_NAME, parts[0],
	FILEVIEW_COLUMN_SIZE, parts[1],
	FILEVIEW_COLUMN_FILENAME, g_strconcat(current_url, parts[0], "/", NULL),
	-1);
	g_strfreev(parts);
	return 0;
}

static int file_cmp(gconstpointer a, gconstpointer b)
{
	return g_ascii_strncasecmp(a, b, -1);
}

static int to_list(const char *listdata)
{
	if (strlen(listdata)==0) return 1;
	char * odata;
	odata = g_strdup_printf("%s", listdata);
	char *pch;
	pch = strtok(odata, "\r\n");
	struct ftpparse ftp;
	while (pch != NULL)
	{
		if (ftp_parse(&ftp, pch, strlen(pch))) {
			char *fileinfo;
			fileinfo = g_strdup_printf("%s\n%ld\n%lu", ftp.name, ftp.size, (unsigned long)ftp.mtime);
			switch (ftp.flagtrycwd){
				case 1:
					dirlist = g_list_prepend(dirlist, fileinfo);
					break;
				default:
					filelist = g_list_prepend(filelist, fileinfo);
			}
		}
		pch = strtok(NULL, "\r\n");
	}
	dirlist = g_list_sort(dirlist, (GCompareFunc)file_cmp);
	filelist = g_list_sort(filelist, (GCompareFunc)file_cmp);
	g_list_foreach(dirlist, (GFunc)add_item, (gpointer)TRUE);
	g_list_foreach(filelist, (GFunc)add_item, (gpointer)FALSE);
	g_list_free(dirlist);
	g_list_free(filelist);
	dirlist=NULL;
	filelist=NULL;
	return 0;
}

static void clear()
{
	gtk_list_store_clear(file_store);
}

static void *disconnect(gpointer p)
{
	if (curl) {
		msgwin_msg_add(COLOR_RED, -1, NULL, "%s", "Disconnected.");
		msgwin_scroll_to_bottom();
		curl = NULL;
	}
	clear();
	return NULL;
}

static void *connect_ftp_server(gpointer p)
{
	const char *url = (const char *)p;
	struct string str;
	str.len = 0;
	str.ptr = malloc(1);
	str.ptr[0] = '\0';
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		//curl_easy_setopt(curl, CURLOPT_USERPWD, "");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		//curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
		curl_easy_perform(curl);
	}
	if (to_list(str.ptr)==0) {
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_DISCONNECT);
		gtk_widget_set_sensitive(btn_go_up, TRUE);
	} else {
		curl_easy_cleanup(curl);
		disconnect(NULL);
	}
	free(str.ptr);
	gdk_threads_enter();
	gtk_widget_set_sensitive(GTK_WIDGET(box), TRUE);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *to_connect(gpointer p)
{
	clear();
	gtk_widget_set_sensitive(GTK_WIDGET(box), FALSE);
	g_thread_create(&connect_ftp_server, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void on_connect_clicked(gpointer p)
{
	if (!curl) {
		current_url=g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(url_entry)));
		if (!g_regex_match_simple("^ftp://", current_url, G_REGEX_CASELESS, 0)) {
			current_url=g_strconcat("ftp://", current_url, NULL);
		}
		if (!g_str_has_suffix(current_url, "/")) {
			current_url=g_strconcat(current_url, "/", NULL);
		}
		gtk_entry_set_text(GTK_ENTRY(url_entry), current_url);
		
		gtk_paned_set_position(GTK_PANED(ui_lookup_widget(geany->main_widgets->window, "vpaned1")), 
			geany->main_widgets->window->allocation.height - 250);
		
		msgwin_clear_tab(MSG_MESSAGE);
		msgwin_switch_tab(MSG_MESSAGE, TRUE);
		msgwin_msg_add(COLOR_BLUE, -1, NULL, "%s", "Connecting...");
		
		curl = curl_easy_init();
		
		to_connect(current_url);
		
	} else {
		disconnect(NULL);
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_CONNECT);
		gtk_widget_set_sensitive(btn_go_up, FALSE);
	}
}

static void on_open_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model = GTK_TREE_MODEL(file_store);
	GList *list;
	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_view));
	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	
	if (is_folder_selected(list)) {
		if (is_single_selection(treesel)) {
			GtkTreePath *treepath = list->data;
			GtkTreeIter iter;
			gtk_tree_model_get_iter(model, &iter, treepath);
			gchar *name;
			gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_FILENAME, &name, -1);
			current_url=g_strdup_printf("%s", name);
			g_free(name);
		}
	} else {
		
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	
	gtk_entry_set_text(GTK_ENTRY(url_entry), current_url);
	to_connect(current_url);
}

static GtkWidget *create_popup_menu(void)
{
	GtkWidget *item, *menu;
	
	menu = gtk_menu_new();
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_open_clicked), NULL);
	
	return menu;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
		on_open_clicked(NULL, NULL);
		return TRUE;
	} else if (event->button == 3) {
		static GtkWidget *popup_menu = NULL;
		if (popup_menu==NULL) popup_menu = create_popup_menu();
		gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, NULL, NULL, event->button, event->time);
	}
	return FALSE;
}

static void on_go_up(void)
{
	gsize len = strlen(current_url);
	if (current_url[len-1] == G_DIR_SEPARATOR)
		current_url[len-1] = '\0';
	current_url=g_strconcat(g_path_get_dirname(current_url), "/", NULL);
	gtk_entry_set_text(GTK_ENTRY(url_entry), current_url);
	to_connect(current_url);
}

static void on_edit_preferences(void)
{
	plugin_show_configure(geany_plugin);
}

static void prepare_file_view()
{
	file_store = gtk_list_store_new(FILEVIEW_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(file_view), GTK_TREE_MODEL(file_store));
	g_object_unref(file_store);
	
	GtkCellRenderer *text_renderer, *icon_renderer;
	GtkTreeViewColumn *column;
	column = gtk_tree_view_column_new();
	icon_renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_set_attributes(column, icon_renderer, "stock-id", FILEVIEW_COLUMN_ICON, NULL);
	text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", FILEVIEW_COLUMN_NAME, NULL);
	text_renderer = gtk_cell_renderer_text_new();
	g_object_set(text_renderer, "xalign", 1.0, NULL);
	gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", FILEVIEW_COLUMN_SIZE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(file_view), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(file_view), FALSE);
	
	PangoFontDescription *pfd = pango_font_description_new();
	pango_font_description_set_size(pfd, 8 * PANGO_SCALE);
	gtk_widget_modify_font(file_view, pfd);
	
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(file_view), FILEVIEW_COLUMN_FILENAME);
	
	g_signal_connect(file_view, "button-press-event", G_CALLBACK(on_button_press), NULL);
}

static GtkWidget *make_toolbar(void)
{
	GtkWidget *wid, *toolbar;
	
	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar), GTK_ICON_SIZE_MENU);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	
	btn_connect = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_CONNECT));
	gtk_widget_set_tooltip_text(btn_connect, "Connect / Disconnect");
	//gtk_widget_set_sensitive(btn_connect, FALSE);
	g_signal_connect(btn_connect, "clicked", G_CALLBACK(on_connect_clicked), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), btn_connect);
	
	btn_go_up = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP));
	gtk_widget_set_tooltip_text(btn_go_up, "Go Up");
	gtk_widget_set_sensitive(btn_go_up, FALSE);
	g_signal_connect(btn_go_up, "clicked", G_CALLBACK(on_go_up), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), btn_go_up);
	
	wid = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_PREFERENCES));
	gtk_widget_set_tooltip_text(wid, "Preferences");
	g_signal_connect(wid, "clicked", G_CALLBACK(on_edit_preferences), NULL);
	gtk_container_add(GTK_CONTAINER(toolbar), wid);
	
	return toolbar;
}

void plugin_init(GeanyData *data)
{
	curl_global_init(CURL_GLOBAL_ALL);
	
	if (!g_thread_supported()) g_thread_init(NULL);
	
	gdk_threads_init();
	
	gdk_threads_enter();
	
	box = gtk_vbox_new(FALSE, 0);
	
	url_entry = gtk_entry_new_with_buffer(gtk_entry_buffer_new("ftp.microsoft.com",-1));
	gtk_box_pack_start(GTK_BOX(box), url_entry, FALSE, FALSE, 0);
	
	GtkWidget *widget;
	
	widget = make_toolbar();
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
	
	file_view = gtk_tree_view_new();
	prepare_file_view();
	
	widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(widget), file_view);
	gtk_box_pack_start(GTK_BOX(box), widget, TRUE, TRUE, 0);
	
	gtk_widget_show_all(box);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), box, gtk_label_new(_("FTP")));
	gdk_threads_leave();
}

GtkWidget *plugin_configure(GtkDialog *dialog)
{
	GtkWidget *widget, *vbox;
	
	vbox = gtk_vbox_new(FALSE, 6);
	
	widget = gtk_label_new("Profiles:");
	gtk_misc_set_alignment(GTK_MISC(widget), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	
	widget = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "New profile...");
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
	gtk_widget_set_tooltip_text(widget, "Choose a profile to edit or choose New profile to create one.");
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	
	gtk_widget_show_all(vbox);
	
	return vbox;
}

void plugin_cleanup(void)
{
	curl_global_cleanup();
	gtk_widget_destroy(box);
}
