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
static GtkWidget *btn_connect;
static GtkTreeStore *file_store;
static GtkTreeIter parent;
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

size_t write_data (void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	return fwrite(ptr, size, nmemb, stream);
}

static int download_progress (void *p, double dltotal, double dlnow, double ultotal, double ulnow)
{
	gdk_threads_enter();
	double done=0.0;
	if(dlnow!=0&&dltotal!=0)done=(double)dlnow/dltotal;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), done);
	gchar *doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
	g_free(doneper);
	gdk_threads_leave();
	return 0;
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

static void log_new_str(int color, char *text)
{
	GTimeVal ct;
	g_get_current_time(&ct);
	time_t tm1, tm2;
	struct tm *t1, *t2;
	long sec = 0;
	tm1 = time(NULL);
	t2 = gmtime(&tm1);
	tm2 = mktime(t2);
	t1 = localtime(&tm1);
	sec = ct.tv_sec + (long)(tm1 -tm2);
	msgwin_msg_add(color, -1, NULL, "[%02ld:%02ld:%02ld.%03.0f] %s", 
	(sec/3600)%24, (sec/60)%60, (sec)%60, (double)(ct.tv_usec)/ 1000, text);
	msgwin_scroll_to_bottom();
}

static int ftp_log(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	char * odata;
	odata = g_strstrip(g_strdup_printf("%s", data));
	char * firstline;
	firstline = strtok(odata,"\r\n");
	gdk_threads_enter();
	switch (type) {
		case CURLINFO_TEXT:
			log_new_str(COLOR_BLUE, firstline);
			break;
		default:
			break;
		case CURLINFO_HEADER_OUT:
			log_new_str(COLOR_BLUE, firstline);
			break;
		case CURLINFO_DATA_OUT:
			break;
		case CURLINFO_SSL_DATA_OUT:
			break;
		case CURLINFO_HEADER_IN:
			log_new_str(COLOR_BLACK, firstline);
			break;
		case CURLINFO_DATA_IN:
			break;
		case CURLINFO_SSL_DATA_IN:
			break;
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
	gtk_tree_store_append(file_store, &iter, gtk_tree_store_iter_is_valid(file_store, &parent)?&parent:NULL);
	if (is_dir) {
		GRegex *regex;
		regex = g_regex_new("\\s->\\s", 0, 0, NULL);
		if (g_regex_match(regex, parts[0], 0, NULL)) {
			gchar **nameparts=g_regex_split(regex, parts[0], 0);
			if (g_strcmp0(parts[1],g_strdup_printf("%ld",g_utf8_strlen(nameparts[1],-1)))==0) {
				sprintf(parts[0], "%s", nameparts[0]);
				sprintf(parts[1], "link");
			}
			g_strfreev(nameparts);
		}
		if (strcmp(parts[1],"0")==0) {
			sprintf(parts[1], "%s", "");
		}
	}
	gtk_tree_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, is_dir?GTK_STOCK_DIRECTORY:GTK_STOCK_FILE,
	FILEVIEW_COLUMN_NAME, parts[0],
	FILEVIEW_COLUMN_SIZE, parts[1],
	FILEVIEW_COLUMN_FILENAME, g_strconcat(current_url, parts[0], is_dir?"/":"", NULL),
	-1);
	g_strfreev(parts);
	return 0;
}

static int file_cmp(gconstpointer a, gconstpointer b)
{
	return g_ascii_strncasecmp(a, b, -1);
}

static void clear()
{
	gtk_tree_store_clear(file_store);
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
	if (gtk_tree_store_iter_is_valid(file_store, &parent))
		gtk_tree_view_expand_row(GTK_TREE_VIEW(file_view), gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), &parent), FALSE);
	return 0;
}

static void *disconnect(gpointer p)
{
	if (curl) {
		log_new_str(COLOR_RED, "Disconnected.");
		curl = NULL;
	}
	clear();
	return NULL;
}

static void *download_file(gpointer p)
{
	const char *url = (const char *)p;
	char *filepath;
	if (curl) {
		FILE *fp;
		char *filedir = g_strdup_printf("%s/gFTP/",(char *)g_get_tmp_dir());
		g_mkdir_with_parents(filedir, 0777);
		filepath = g_strdup_printf("%s%s", filedir, g_path_get_basename(url));
		fp=fopen(filepath,"wb");
		curl_easy_setopt(curl, CURLOPT_URL, url);
		//curl_easy_setopt(curl, CURLOPT_USERPWD, "");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, download_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		//curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
		curl_easy_perform(curl);
		fclose(fp);
	}
	gdk_threads_enter();
	if (filepath) document_open_file(filepath, FALSE, NULL, NULL);
	gtk_widget_set_sensitive(GTK_WIDGET(box), TRUE);
	gtk_widget_hide(geany->main_widgets->progressbar);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *get_dir_listing(gpointer p)
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
	} else {
		gdk_threads_enter();
		dialogs_show_msgbox(GTK_MESSAGE_INFO, "This is an empty directory.");
		gdk_threads_leave();
	}
	free(str.ptr);
	gdk_threads_enter();
	gtk_widget_set_sensitive(GTK_WIDGET(box), TRUE);
	ui_progress_bar_stop();
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *to_download_file(gpointer p)
{
	gtk_widget_set_sensitive(GTK_WIDGET(box), FALSE);
	gtk_widget_show(geany->main_widgets->progressbar);
	g_thread_create(&download_file, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void *to_get_dir_listing(gpointer p)
{
	gtk_widget_set_sensitive(GTK_WIDGET(box), FALSE);
	ui_progress_bar_start("Please wait...");
	g_thread_create(&get_dir_listing, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void on_open_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model = GTK_TREE_MODEL(file_store);
	GList *list;
	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_view));
	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	
	if (is_single_selection(treesel)) {
		GtkTreePath *treepath = list->data;
		GtkTreeIter iter;
		gtk_tree_model_get_iter(model, &iter, treepath);
		gchar *name;
		gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_FILENAME, &name, -1);
		current_url=g_strdup_printf("%s", name);
		if (is_folder_selected(list)) {
			gtk_tree_model_get_iter(model, &parent, treepath);
			gtk_entry_set_text(GTK_ENTRY(url_entry), current_url);
			to_get_dir_listing(current_url);
		} else {
			to_download_file(current_url);
		}
		g_free(name);
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	
}

static void on_connect_clicked(gpointer p)
{
	if (!curl) {
		current_url=g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(url_entry)));
		current_url=g_strstrip(current_url);
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
		log_new_str(COLOR_BLUE, "Connecting...");
		
		GtkTreeIter iter;
		gtk_tree_store_append(file_store, &iter, NULL);
		gtk_tree_store_set(file_store, &iter,
		FILEVIEW_COLUMN_ICON, GTK_STOCK_DIRECTORY,
		FILEVIEW_COLUMN_NAME, current_url,
		FILEVIEW_COLUMN_SIZE, "-1",
		FILEVIEW_COLUMN_FILENAME, current_url,
		-1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(file_view), gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), &iter), NULL, FALSE);
		
		curl = curl_easy_init();
		
		on_open_clicked(NULL, NULL);
		
	} else {
		disconnect(NULL);
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_CONNECT);
	}
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

static void on_edit_preferences(void)
{
	plugin_show_configure(geany_plugin);
}

static void prepare_file_view()
{
	file_store = gtk_tree_store_new(FILEVIEW_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
	//text_renderer = gtk_cell_renderer_text_new();
	//g_object_set(text_renderer, "xalign", 1.0, NULL);
	//gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
	//gtk_tree_view_column_set_attributes(column, text_renderer, "text", FILEVIEW_COLUMN_SIZE, NULL);
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
	
	url_entry = gtk_entry_new_with_buffer(gtk_entry_buffer_new("137.189.4.14",-1));
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
