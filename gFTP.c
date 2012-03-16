#include <geanyplugin.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

GeanyPlugin *geany_plugin;
GeanyData *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO("gFTP", "FTP", "1.0", "Cai Guanhao <caiguanhao@gmail.com>");

enum
{
	FILEVIEW_COLUMN_ICON = 0,
	FILEVIEW_COLUMN_NAME,
	FILEVIEW_COLUMN_FILENAME,
	FILEVIEW_N_COLUMNS
};

static GtkWidget *box;
static GtkWidget *file_view;
static GtkWidget *url_entry;
static GtkListStore *file_store;

static CURL *curl;

struct string {
	char *ptr;
	int len;
};

char *ltrim(char *str)
{
	char *ptr;
	int  len;
	for (ptr = str; *ptr && isspace((int)*ptr); ++ptr);
	len = strlen(ptr);
	memmove(str, ptr, len + 1);
	return str;
}

size_t write_function(void *ptr, size_t size, size_t nmemb, struct string *str)
{
	int new_len = str->len + size * nmemb;
	str->ptr = realloc(str->ptr, new_len + 1);
	memcpy(str->ptr + str->len, ptr, size * nmemb);
	str->ptr[new_len] = '\0';
	str->len = new_len;
	return size*nmemb;
}

static int ftp_log(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
	char * odata;
	odata = ltrim(g_strdup_printf("%s", data));
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
		GtkTreeView *MsgWin;
		MsgWin = GTK_TREE_VIEW(ui_lookup_widget(geany->main_widgets->window, "treeview4"));
		int n = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(MsgWin),NULL);
		GtkTreePath *path;
		path = gtk_tree_path_new_from_string(g_strdup_printf("%d",n-1));
		gtk_tree_view_scroll_to_cell(MsgWin, path, NULL, FALSE, 0.0, 0.0);
	}
	gdk_threads_leave();
	return 0;
}

static void add_item(const gchar *name)
{
	GtkTreeIter iter;
	gtk_list_store_append(file_store, &iter);
	gtk_list_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, GTK_STOCK_DIRECTORY,
	FILEVIEW_COLUMN_NAME, name,
	FILEVIEW_COLUMN_FILENAME, name,
	-1);
	
}

static void fill_list(const char *listdata)
{
	char * odata;
	odata = g_strdup_printf("%s", listdata);
	char *pch;
	pch = strtok(odata, "\r\n");
	while (pch != NULL)
	{
		add_item(pch);
		pch = strtok(NULL, "\r\n");
	}
}

static void clear()
{
	gtk_list_store_clear(file_store);
}

static void *connect_ftp_server(void *p)
{
	const char *url = (const char *)p;
	if (curl) {
		struct string str;
		str.len = 0;
		str.ptr = malloc(1);
		str.ptr[0] = '\0';
		
		curl_easy_setopt(curl, CURLOPT_URL, url);
		//curl_easy_setopt(curl, CURLOPT_USERPWD, "");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
		curl_easy_perform(curl);
		
		fill_list(str.ptr);
		free(str.ptr);
		
		curl_easy_cleanup(curl);
	}
	
	gdk_threads_enter();
	gtk_widget_set_sensitive(GTK_WIDGET(box), TRUE);
	gdk_threads_leave();
	g_thread_exit(NULL);
	
	return NULL;
}

static void on_connect_clicked(GtkButton *button, gpointer user_data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(box), FALSE);
	clear();
	msgwin_clear_tab(MSG_MESSAGE);
	msgwin_switch_tab(MSG_MESSAGE, TRUE);
	msgwin_msg_add(COLOR_BLUE, -1, NULL, "%s", "Connecting...");
	curl = curl_easy_init();
	g_thread_create(&connect_ftp_server, (gpointer)gtk_entry_get_text(GTK_ENTRY(url_entry)), FALSE, NULL);
}

static void prepare_file_view()
{
	file_store = gtk_list_store_new(FILEVIEW_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(file_view), GTK_TREE_MODEL(file_store));
	g_object_unref(file_store);
	
	GtkCellRenderer *text_renderer, *icon_renderer;
	GtkTreeViewColumn *column;
	icon_renderer = gtk_cell_renderer_pixbuf_new();
	text_renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_set_attributes(column, icon_renderer, "stock-id", FILEVIEW_COLUMN_ICON, NULL);
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", FILEVIEW_COLUMN_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(file_view), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(file_view), FALSE);
}

void plugin_init(GeanyData *data)
{
	curl_global_init(CURL_GLOBAL_ALL);
	
	if (!g_thread_supported()) g_thread_init(NULL);
	
	gdk_threads_init();
	
	gdk_threads_enter();
	
	box = gtk_vbox_new(FALSE, 0);
	
	url_entry = gtk_entry_new_with_buffer(gtk_entry_buffer_new("ftp.mozilla.org/pub/",-1));
	gtk_box_pack_start(GTK_BOX(box), url_entry, FALSE, FALSE, 0);
	
	GtkWidget *widget;
	
	widget = gtk_button_new_with_label("Connect");
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_connect_clicked), NULL);
	
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

void plugin_cleanup(void)
{
	curl_global_cleanup();
	gtk_widget_destroy(box);
}
