#include <geanyplugin.h>
#include <curl/curl.h>
#include <sys/stat.h>
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
	FILEVIEW_COLUMN_DIR,
	FILEVIEW_COLUMN_INFO,
	FILEVIEW_N_COLUMNS
};

static GtkWidget *box;
static GtkWidget *file_view;
static GtkWidget *btn_connect;
static GtkTreeStore *file_store;
static GtkTreeIter parent;
static CURL *curl;
static gchar **all_profiles, **all_hosts;
static gsize all_profiles_length;
static gchar *profiles_file, *hosts_file, *tmp_dir;
static gchar *last_command;
static gboolean retried = FALSE;
static gboolean uploading = FALSE;

#define BASE64ENCODE_TIMES 8

GList *filelist;
GList *dirlist;

struct string
{
	char *ptr;
	int len;
};

struct upload_location
{
	char *from;
	char *to;
};

struct uploadf {
	FILE *fp;
	int transfered;
	struct stat st;
};

static struct
{
	GtkListStore *store;
	GtkTreeIter iter_store_new;
	GtkWidget *combo;
	GtkWidget *delete;
	GtkWidget *host;
	GtkWidget *port;
	GtkWidget *login;
	GtkWidget *passwd;
	GtkWidget *anon;
	GtkWidget *showpass;
} pref;

static struct
{
	GtkWidget *login;
	GtkWidget *password;
} retry;

static struct 
{
	char *url;
	int index;
	char *host;
	char *port;
	char *login;
	char *password;
} current_profile;

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

static void save_profiles(gint type);
static void save_hosts();
static void *disconnect(gpointer p);
static void *to_get_dir_listing(gpointer p);
static void on_retry_use_anonymous_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gboolean toggle = gtk_toggle_button_get_active(togglebutton);
	gtk_widget_set_sensitive(retry.login, !toggle);
	gtk_widget_set_sensitive(retry.password, !toggle);
	if (toggle) {
		gtk_entry_set_text(GTK_ENTRY(retry.login), "anonymous");
		gtk_entry_set_text(GTK_ENTRY(retry.password), "ftp@example.com");
	}
}

static void on_retry_show_password_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_entry_set_visibility(GTK_ENTRY(retry.password), gtk_toggle_button_get_active(togglebutton));
}

static void on_retry_dialog_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	if (response>0) {
		current_profile.login = g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(retry.login)));
		current_profile.password = g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(retry.password)));
		if (response == 2) {
			save_profiles(2);
		}
		to_get_dir_listing("");
	} else {
		disconnect(NULL);
	}
}

static gboolean on_retry_entry_keypress(GtkWidget *widget, GdkEventKey *event, gpointer dialog)
{
	if (event->keyval == 0xff0d || event->keyval == 0xff8d) { //RETURN AND KEY PAD ENTER
		gtk_dialog_response(GTK_DIALOG(dialog), 2);
	}
	return FALSE;
}

static void try_another_username_password()
{
	retried = TRUE;
	gdk_threads_enter();
	GtkWidget *dialog, *vbox;
	dialog = gtk_dialog_new_with_buttons("Access denined", GTK_WINDOW(geany->main_widgets->window),
		GTK_DIALOG_DESTROY_WITH_PARENT, 
		"_Save & retry", 2, 
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, 
		"_Retry", 1, 
		NULL);
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));
	gtk_widget_set_name(dialog, "GeanyDialog");
	gtk_box_set_spacing(GTK_BOX(vbox), 6);
	
	GtkWidget *widget, *table;
	
	table = gtk_table_new(3, 3, FALSE);
	
	widget = gtk_label_new("Wrong Username or Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 0.5, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 3, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_label_new("Login");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(widget), current_profile.login);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(on_retry_entry_keypress), dialog);
	retry.login = widget;
	widget = gtk_check_button_new_with_label("Anonymous");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_retry_use_anonymous_toggled), NULL);
	
	widget = gtk_label_new("Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(widget), current_profile.password);
	gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(on_retry_entry_keypress), dialog);
	retry.password = widget;
	widget = gtk_check_button_new_with_label("Show");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_retry_show_password_toggled), NULL);
	
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	
	g_signal_connect(dialog, "response", G_CALLBACK(on_retry_dialog_response), NULL);
	
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	
	gdk_threads_leave();
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
	if (g_regex_match_simple("^PASS\\s(.*)$", firstline, 0, 0)) {
		firstline = g_strdup_printf("PASS %s", g_strnfill((gsize)(g_utf8_strlen(firstline, -1)-5), '*'));
	}
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
	if (g_regex_match_simple("^Connected\\sto\\s(.+?)\\s\\((.+?)\\)", firstline, 0, 0)) {
		GRegex *regex;
		GMatchInfo *match_info;
		regex = g_regex_new("^Connected\\sto\\s(.+?)\\s\\((.+?)\\)", G_REGEX_CASELESS, 0, NULL);
		g_regex_match(regex, firstline, 0, &match_info);
		gchar *thost = g_match_info_fetch(match_info, 1);
		gchar *tipaddr = g_match_info_fetch(match_info, 2);
		if (!g_hostname_is_ip_address(thost)) {
			gchar *xhosts = g_strjoinv("\n", all_hosts);
			xhosts = g_strjoin("\n", xhosts, g_strdup_printf("%s %s", thost, tipaddr), NULL);
			all_hosts = g_strsplit(xhosts, "\n", 0);
			save_hosts();
			log_new_str(COLOR_BLUE, "New host saved.");
			g_free(xhosts);
		}
		g_free(thost);
		g_free(tipaddr);
		g_match_info_free(match_info);
		g_regex_unref(regex);
	}
	gdk_threads_leave();
	if (g_regex_match_simple("^530", firstline, 0, 0)) {
		curl_easy_reset(curl);
		try_another_username_password();
	}
	return 0;
}

static gchar *find_host (gchar *src)
{
	gchar** hostsparts;
	gint i;
	for (i = 0; i < g_strv_length(all_hosts); i++) {
		hostsparts = g_strsplit(all_hosts[i], " ", 0);
		if (g_strcmp0(hostsparts[0], src)==0)
			src = g_strdup(hostsparts[1]);
		g_strfreev(hostsparts);
	}
	return src;
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
		g_regex_unref(regex);
		if (strcmp(parts[1],"0")==0) {
			sprintf(parts[1], "%s", "");
		}
	}
	gchar *parent_dir, *filename;
	if (gtk_tree_store_iter_is_valid(file_store, &parent)) {
		gtk_tree_model_get(GTK_TREE_MODEL(file_store), &parent, FILEVIEW_COLUMN_DIR, &parent_dir, -1);
		filename = g_strconcat(parent_dir, parts[0], is_dir?"/":"", NULL);
		g_free(parent_dir);
	} else {
		filename = g_strconcat(parts[0], is_dir?"/":"", NULL);
	}
	gint64 size = g_ascii_strtoll(parts[1], NULL, 0);
	gchar *tsize = g_format_size_for_display(size);
	char buffer[80];
	time_t mod_time = g_ascii_strtoll(parts[2], NULL, 0);
	strftime(buffer, 80, "%Y-%m-%d %H:%M:%S (%A)", gmtime(&mod_time));
	gtk_tree_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, is_dir?GTK_STOCK_DIRECTORY:GTK_STOCK_FILE, 
	FILEVIEW_COLUMN_NAME, parts[0], 
	FILEVIEW_COLUMN_DIR, filename, 
	FILEVIEW_COLUMN_INFO, g_strdup_printf("%s/%s\nSize:\t%s\nModified:\t%s", is_dir?"Folder:\t":"File:\t\t", filename, tsize, buffer) , 
	-1);
	g_free(filename);
	g_free(tsize);
	g_strfreev(parts);
	return 0;
}

static int file_cmp(gconstpointer a, gconstpointer b)
{
	return g_ascii_strncasecmp(a, b, -1);
}

static void clear_children()
{
	gdk_threads_enter();
	GtkTreeIter child;
	if (gtk_tree_store_iter_is_valid(file_store, &parent) && gtk_tree_model_iter_children(GTK_TREE_MODEL(file_store), &child, &parent)) {
		while (gtk_tree_store_remove(file_store, &child)) {}
	}
	gdk_threads_leave();
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
	gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_CONNECT);
	if (curl) {
		log_new_str(COLOR_RED, "Disconnected.");
		curl = NULL;
	}
	clear();
	return NULL;
}

static void open_external(const gchar *dir)
{
	gchar *cmd;
	gchar *locale_cmd;
	GError *error = NULL;
	cmd = g_strdup_printf("nautilus \"%s\"", dir);
	locale_cmd = utils_get_locale_from_utf8(cmd);
	if (!g_spawn_command_line_async(locale_cmd, &error))
	{
		ui_set_statusbar(TRUE, "Could not execute '%s' (%s).", cmd, error->message);
		g_error_free(error);
	}
	g_free(locale_cmd);
	g_free(cmd);
}

static void *download_file(gpointer p)
{
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, FALSE);
	gdk_threads_leave();
	const char *url = (const char *)p;
	char *filepath;
	char *filedir;
	if (curl) {
		FILE *fp;
		filedir = g_strconcat(tmp_dir, current_profile.login, "@", current_profile.host, "/", g_path_get_dirname(url), NULL);
		g_mkdir_with_parents(filedir, 0777);
		filepath = g_strdup_printf("%s/%s", filedir, g_path_get_basename(url));
		fp=fopen(filepath,"wb");
		url = g_strconcat(current_profile.url, (const char *)p, NULL);
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_PORT, port);
		curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, download_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_perform(curl);
		fclose(fp);
	}
	gdk_threads_enter();
	if (filepath) 
		if (!document_open_file(filepath, FALSE, NULL, NULL))
			if (dialogs_show_question("Could not open the file in Geany. View it in file browser?"))
				open_external(filedir);
	gtk_widget_set_sensitive(box, TRUE);
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *p)
{
	gdk_threads_enter();
	struct uploadf *file = (struct uploadf *)p;
	size_t retcode = fread(ptr, size, nmemb, file->fp);
	file->transfered+=retcode;
	double done=0.0;
	done=(double)file->transfered/file->st.st_size;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), done);
	gchar *doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
	g_free(doneper);
	gdk_threads_leave();
	return retcode;
}

static void *upload_file(gpointer p)
{
	struct upload_location *ul = (struct upload_location *)p;
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, FALSE);
	gdk_threads_leave();
	struct uploadf file;
	char *ulto = ul->to;
	if (stat(ul->from, &file.st)==0) {
		char *url = ulto;
		if (curl) {
			file.transfered=0;
			file.fp=fopen(ul->from,"rb");
			url = g_strconcat(current_profile.url, url, NULL);
			gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
			if (port<=0 || port>65535) port=21;
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_PORT, port);
			curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
			curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
			curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
			curl_easy_setopt(curl, CURLOPT_READDATA, &file);
			curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
			curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
			curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
			curl_easy_perform(curl);
			fclose(file.fp);
			uploading = FALSE;
		}
	}
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, TRUE);
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
	gdk_threads_leave();
	gchar *name = g_strconcat(g_path_get_dirname(ulto), "/", NULL);
	to_get_dir_listing(name);
	g_thread_exit(NULL);
	return NULL;
}

static void *get_dir_listing(gpointer p)
{
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, FALSE);
	gdk_threads_leave();
	clear_children();
	const char *url = g_strconcat(current_profile.url, (const char *)p, NULL);
	struct string str;
	str.len = 0;
	str.ptr = malloc(1);
	str.ptr[0] = '\0';
	if (curl) {
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_PORT, port);
		curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_perform(curl);
	}
	if (to_list(str.ptr)==0) {
		gdk_threads_enter();
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_DISCONNECT);
		gdk_threads_leave();
		retried = FALSE;
	}
	free(str.ptr);
	gdk_threads_enter();
	if (!retried) // prevent box sensitive when retry passwd.
		gtk_widget_set_sensitive(box, TRUE);
	ui_progress_bar_stop();
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *to_get_dir_listing(gpointer p);

static void *send_command(gpointer p)
{
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, FALSE);
	gdk_threads_leave();
	const char *url = g_strconcat(current_profile.url, (const char *)p, NULL);
	struct curl_slist *headers = NULL; 
	if (curl) {
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_PORT, port);
		curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		headers = curl_slist_append(headers, last_command); 
		curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headers);
		curl_easy_perform(curl);
	}
	curl_slist_free_all(headers);
	gdk_threads_enter();
	gtk_widget_set_sensitive(box, TRUE);
	ui_progress_bar_stop();
	gdk_threads_leave();
	to_get_dir_listing(p);
	g_thread_exit(NULL);
	return NULL;
}

static void *to_download_file(gpointer p)
{
	curl_easy_reset(curl);
	gtk_widget_show(geany->main_widgets->progressbar);
	g_thread_create(&download_file, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void *to_upload_file(gpointer p)
{
	curl_easy_reset(curl);
	gtk_widget_show(geany->main_widgets->progressbar);
	g_thread_create(&upload_file, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void *to_get_dir_listing(gpointer p)
{
	curl_easy_reset(curl);
	ui_progress_bar_start("Please wait...");
	g_thread_create(&get_dir_listing, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void *to_send_commands(gpointer p)
{
	curl_easy_reset(curl);
	ui_progress_bar_start("Please wait...");
	g_thread_create(&send_command, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void on_menu_item_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	gint type = (int)user_data;
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
		
		last_command = NULL;
		
		switch (type) {
			case 1:
				if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
					if (is_folder_selected(list)) {
						gtk_tree_model_get_iter(model, &parent, treepath);
						gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_DIR, &name, -1);
					} else {
						gtk_tree_model_get(model, &parent, FILEVIEW_COLUMN_DIR, &name, -1);
					}
				} else {
					gtk_tree_model_get_iter(model, &parent, treepath);
					name="";
				}
				last_command = dialogs_show_input("New Folder", GTK_WINDOW(geany->main_widgets->window), "Name", "New Folder");
				if (last_command) {
					last_command = g_strdup_printf("MKD %s", last_command);
					to_send_commands(name);
				}
				break;
			case 2:
				if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
					if (is_folder_selected(list)) {
						gtk_tree_model_get(model, &parent, FILEVIEW_COLUMN_DIR, &name, -1);
						//gtk_tree_model_get_iter(model, &parent, treepath);
						gchar *dirname;
						gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_NAME, &dirname, -1);
						last_command = g_strdup_printf("RMD %s", dirname);
						g_free(dirname);
						to_send_commands(name);
					}
				}
				break;
		}
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
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
		gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_DIR, &name, -1);
		if (is_folder_selected(list)) {
			gtk_tree_model_get_iter(model, &parent, treepath);
			to_get_dir_listing(name);
		} else {
			to_download_file(name);
		}
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
}

static void menu_position_func(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
	GdkWindow *gdk_window;
	GtkAllocation allocation;
	gdk_window = gtk_widget_get_window(btn_connect);
	gdk_window_get_origin(gdk_window, x, y);
	gtk_widget_get_allocation(btn_connect, &allocation);
	*x += allocation.x;
	*y += allocation.y + allocation.height;
	*push_in = FALSE;
}

static void load_profiles(gint type);

static void to_connect(GtkMenuItem *menuitem, int p)
{
	current_profile.index = p;
	load_profiles(2);
	current_profile.url=g_strdup_printf("%s", find_host(current_profile.host));
	current_profile.url=g_strstrip(current_profile.url);
	if (!g_regex_match_simple("^ftp://", current_profile.url, G_REGEX_CASELESS, 0)) {
		current_profile.url=g_strconcat("ftp://", current_profile.url, NULL);
	}
	if (!g_str_has_suffix(current_profile.url, "/")) {
		current_profile.url=g_strconcat(current_profile.url, "/", NULL);
	}
	
	gtk_paned_set_position(GTK_PANED(ui_lookup_widget(geany->main_widgets->window, "vpaned1")), 
		geany->main_widgets->window->allocation.height - 250);
	
	msgwin_clear_tab(MSG_MESSAGE);
	msgwin_switch_tab(MSG_MESSAGE, TRUE);
	log_new_str(COLOR_BLUE, "Connecting...");
	
	GtkTreeIter iter;
	gtk_tree_store_append(file_store, &iter, NULL);
	gtk_tree_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, GTK_STOCK_DIRECTORY,
	FILEVIEW_COLUMN_NAME, current_profile.url,
	FILEVIEW_COLUMN_DIR, "",
	FILEVIEW_COLUMN_INFO, current_profile.url, 
	-1);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(file_view), gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), &iter), NULL, FALSE);
	
	curl = curl_easy_init();
	
	on_open_clicked(NULL, NULL);
}

static void on_connect_clicked(gpointer p)
{
	retried = FALSE;
	if (!curl) {
		GtkWidget *item, *menu;
		menu = gtk_menu_new();
		gsize i;
		for (i = 0; i < all_profiles_length; i++) {
			item = gtk_menu_item_new_with_label(all_profiles[i]);
			gtk_widget_show(item);
			gtk_container_add(GTK_CONTAINER(menu), item);
			g_signal_connect(item, "activate", G_CALLBACK(to_connect), (gpointer)i);
		}
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_func, NULL, 0, GDK_CURRENT_TIME);
	} else {
		disconnect(NULL);
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
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_DIRECTORY, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "Create _Folder");
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)1);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "Delete Empty _Folder");
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)2);
	
	return menu;
}

static gchar *decrypt(gchar *src)
{
	gchar *p1 = NULL;
	gchar *p2 = NULL;
	gchar *p3 = NULL;
	glong t, l1, l2;
	gsize len;
	l1 = g_utf8_strlen(src, -1);
	l2 = 64L;
	if (l1<=l2) return "";
	p1 = "";
	p2 = "";
	for (t=0; t<(l1>l2*2?l2:l1-l2); t++) {
		p1 = g_strconcat(p1, g_strndup(src + t * 2, 1), NULL);
		p2 = g_strconcat(p2, g_strndup(src + t * 2 + 1, 1), NULL);
	}
	if (l1>l2*2) {
		p1 = g_strconcat(p1, g_strndup(src + l2 * 2, l1), NULL);
	} else {
		p2 = g_strconcat(p2, g_strndup(src + (l1 - l2) * 2, l1), NULL);
	}
	p3 = g_compute_checksum_for_string(G_CHECKSUM_SHA256, p1, g_utf8_strlen(p1, -1));
	for (t=0; t<BASE64ENCODE_TIMES; t++)
		p1 = (gchar *)g_base64_decode(p1, &len);
	if (g_strcmp0(p2, p3)!=0) {
		p1 = g_strconcat("", NULL);
	}
	src = g_strdup(p1);
	g_free(p1);
	g_free(p2);
	g_free(p3);
	return src;
}

static gchar *encrypt(gchar *src)
{
	gchar *p1 = src;
	gchar *p2 = NULL;
	glong t, l1, l2;
	for (t=0; t<BASE64ENCODE_TIMES; t++)
	p1 = g_base64_encode((guchar *)p1, g_utf8_strlen(p1, -1));
	l1 = g_utf8_strlen(p1, -1);
	p2 = g_compute_checksum_for_string(G_CHECKSUM_SHA256, p1, g_utf8_strlen(p1, -1));
	l2 = g_utf8_strlen(p2, -1);
	src = "";
	for (t=0; t<(l2>l1?l1:l2); t++)
	src = g_strconcat(src, g_strndup(p1 + t, 1), g_strndup(p2 + t, 1), NULL);
	if (l2>l1) {
		src = g_strconcat(src, g_strndup(p2 + l1, l2), NULL);
	} else {
		src = g_strconcat(src, g_strndup(p1 + l2, l1), NULL);
	}
	g_free(p1);
	g_free(p2);
	return src;
}

static void load_profiles(gint type)
{
	GKeyFile *profiles = g_key_file_new();
	profiles_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, "gFTP", G_DIR_SEPARATOR_S, "profiles.conf", NULL);
	g_key_file_load_from_file(profiles, profiles_file, G_KEY_FILE_NONE, NULL);
	all_profiles = g_key_file_get_groups(profiles, &all_profiles_length);
	switch (type) {
		case 1:{
			GtkTreeIter iter;
			gsize i;
			for (i = 0; i < all_profiles_length; i++) {
				gtk_list_store_append(GTK_LIST_STORE(pref.store), &iter);
				gtk_list_store_set(GTK_LIST_STORE(pref.store), &iter, 
				0, utils_get_setting_string(profiles, all_profiles[i], "host", ""), 
				1, utils_get_setting_string(profiles, all_profiles[i], "port", "21"), 
				2, utils_get_setting_string(profiles, all_profiles[i], "login", ""), 
				3, decrypt(utils_get_setting_string(profiles, all_profiles[i], "password", "")), 
				-1);
			}
			break;
		}
		case 2:{
			gint i;
			i = current_profile.index;
			current_profile.host = utils_get_setting_string(profiles, all_profiles[i], "host", "");
			current_profile.port = utils_get_setting_string(profiles, all_profiles[i], "port", "");
			current_profile.login = utils_get_setting_string(profiles, all_profiles[i], "login", "");
			current_profile.password = decrypt(utils_get_setting_string(profiles, all_profiles[i], "password", ""));
		}
	}
	g_key_file_free(profiles);
}

static void load_hosts()
{
	GKeyFile *hosts = g_key_file_new();
	hosts_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, "gFTP", G_DIR_SEPARATOR_S, "hosts.conf", NULL);
	g_key_file_load_from_file(hosts, hosts_file, G_KEY_FILE_NONE, NULL);
	gsize i;
	all_hosts = g_key_file_get_groups(hosts, &i);
	for (i = 0; i < g_strv_length(all_hosts); i++) {
		all_hosts[i] = g_strconcat(all_hosts[i], " ", utils_get_setting_string(hosts, all_hosts[i], "ip_address", ""), NULL);
	}
	g_key_file_free(hosts);
}

static void load_settings(void)
{
	load_profiles(0);
	load_hosts();
	tmp_dir = g_strdup_printf("%s/gFTP/",(char *)g_get_tmp_dir());
}

static void save_profiles(gint type)
{
	GKeyFile *profiles = g_key_file_new();
	gchar *data;
	gchar *profiles_dir = g_path_get_dirname(profiles_file);
	switch (type) {
		case 1: {
			GtkTreeModel *model;
			model = gtk_combo_box_get_model(GTK_COMBO_BOX(pref.combo));
			GtkTreeIter iter;
			gboolean valid;
			valid = gtk_tree_model_get_iter_from_string(model, &iter, "2");
			gchar *host = NULL;
			gchar *port = NULL;
			gchar *login = NULL;
			gchar *password = NULL;
			while (valid) {
				gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
				0, &host, 
				1, &port, 
				2, &login, 
				3, &password, 
				-1);
				host = g_strstrip(host);
				if (g_strcmp0(host, "")!=0) {
					port = g_strstrip(port);
					login = g_strstrip(login);
					password = encrypt(g_strstrip(password));
					g_key_file_set_string(profiles, host, "host", host);
					g_key_file_set_string(profiles, host, "port", port);
					g_key_file_set_string(profiles, host, "login", login);
					g_key_file_set_string(profiles, host, "password", password);
				}
				g_free(host);
				g_free(port);
				g_free(login);
				g_free(password);
				valid = gtk_tree_model_iter_next(model, &iter);
			}
			break;
		}
		case 2: {
			g_key_file_load_from_file(profiles, profiles_file, G_KEY_FILE_NONE, NULL);
			gint i;
			i = current_profile.index;
			g_key_file_set_string(profiles, all_profiles[i], "host", current_profile.host);
			g_key_file_set_string(profiles, all_profiles[i], "port", current_profile.port);
			g_key_file_set_string(profiles, all_profiles[i], "login", current_profile.login);
			g_key_file_set_string(profiles, all_profiles[i], "password", encrypt(current_profile.password));
			break;
		}
	}
	if (!g_file_test(profiles_dir, G_FILE_TEST_IS_DIR) || utils_mkdir(profiles_dir, TRUE)!=0) {
		dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Plugin configuration directory could not be created.");
	} else {
		all_profiles = g_key_file_get_groups(profiles, &all_profiles_length);
		data = g_key_file_to_data(profiles, NULL, NULL);
		utils_write_file(profiles_file, data);
		g_free(data);
	}
	g_free(profiles_dir);
	g_key_file_free(profiles);
}

static void save_hosts()
{
	GKeyFile *hosts = g_key_file_new();
	gchar *data;
	gchar *hosts_dir = g_path_get_dirname(hosts_file);
	gsize i;
	gchar** hostsparts;
	for (i = 0; i < g_strv_length(all_hosts); i++) {
		hostsparts = g_strsplit(all_hosts[i], " ", 0);
		g_key_file_set_string(hosts, hostsparts[0], "ip_address", hostsparts[1]);
		g_strfreev(hostsparts);
	}
	if (g_file_test(hosts_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(hosts_dir, TRUE)==0) {
		data = g_key_file_to_data(hosts, NULL, NULL);
		utils_write_file(hosts_file, data);
		g_free(data);
	}
	g_free(hosts_dir);
	g_key_file_free(hosts);
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

static gboolean is_edit_profiles_selected_nth_item(GtkTreeIter *iter, char *num)
{
	return gtk_tree_path_compare(gtk_tree_path_new_from_string(gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pref.store), iter)), gtk_tree_path_new_from_string(num))==0;
}

static void is_select_profiles_use_anonymous(GtkTreeIter *iter)
{
	gboolean toggle = FALSE;
	gchar *login = g_strdup_printf("%s", "");
	if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(pref.store), iter, 2, &login, -1);
		if (g_strcmp0(login, "anonymous")==0) {
			toggle = TRUE;
		}
	}
	g_free(login);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pref.anon), toggle);
	gtk_widget_set_sensitive(pref.login, !toggle);
	gtk_widget_set_sensitive(pref.passwd, !toggle);
}

static void check_delete_button_sensitive(GtkTreeIter *iter)
{
	gtk_widget_set_sensitive(pref.delete, !is_edit_profiles_selected_nth_item(iter, "0"));
}

static void *on_host_login_password_changed(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(pref.host)), "")!=0) {
		if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &pref.iter_store_new) || is_edit_profiles_selected_nth_item(&pref.iter_store_new, "0")) {
			gtk_list_store_append(GTK_LIST_STORE(pref.store), &pref.iter_store_new);
		}
		gtk_list_store_set(GTK_LIST_STORE(pref.store), &pref.iter_store_new, 
		0, gtk_entry_get_text(GTK_ENTRY(pref.host)), 
		1, gtk_entry_get_text(GTK_ENTRY(pref.port)), 
		2, gtk_entry_get_text(GTK_ENTRY(pref.login)), 
		3, gtk_entry_get_text(GTK_ENTRY(pref.passwd)), 
		-1);
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &pref.iter_store_new);
	}
	return FALSE;
}

static void on_use_anonymous_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gboolean toggle = gtk_toggle_button_get_active(togglebutton);
	gtk_widget_set_sensitive(pref.login, !toggle);
	gtk_widget_set_sensitive(pref.passwd, !toggle);
	if (toggle) {
		gtk_entry_set_text(GTK_ENTRY(pref.login), "anonymous");
		gtk_entry_set_text(GTK_ENTRY(pref.passwd), "ftp@example.com");
	}
	on_host_login_password_changed(NULL, NULL, NULL);
}

static void on_show_password_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_entry_set_visibility(GTK_ENTRY(pref.passwd), gtk_toggle_button_get_active(togglebutton));
}

static void on_edit_profiles_changed(void)
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &pref.iter_store_new);
	gchar *host = g_strdup_printf("%s", "");
	gchar *port = g_strdup_printf("%s", "21");
	gchar *login = g_strdup_printf("%s", "");
	gchar *password = g_strdup_printf("%s", "");
	if (!is_edit_profiles_selected_nth_item(&iter, "0")) {
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &iter)) {
			gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
			0, &host, 
			1, &port, 
			2, &login, 
			3, &password, 
			-1);
		}
	}
	gtk_entry_set_text(GTK_ENTRY(pref.host), host);
	gtk_entry_set_text(GTK_ENTRY(pref.port), port);
	gtk_entry_set_text(GTK_ENTRY(pref.login), login);
	gtk_entry_set_text(GTK_ENTRY(pref.passwd), password);
	g_free(host);
	g_free(port);
	g_free(login);
	g_free(password);
	
	is_select_profiles_use_anonymous(&iter);
	check_delete_button_sensitive(&iter);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pref.showpass), FALSE);
}

static void on_delete_profile_clicked()
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
	if (!is_edit_profiles_selected_nth_item(&iter, "0")) {
		gtk_list_store_remove(GTK_LIST_STORE(pref.store), &iter);
		int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pref.store),NULL);
		if (n==2) n=1;
		gtk_combo_box_set_active(GTK_COMBO_BOX(pref.combo), n-1);
	}
}

static void on_document_save()
{
	if (curl) {
		if (uploading) {
			dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Please wait until last file upload is complete.");
		} else {
			uploading = TRUE;
			struct upload_location ul;
			ul.from = document_get_current()->real_path;
			ul.to = g_path_get_basename(ul.from);
			char *tmpdir = g_strconcat(tmp_dir, current_profile.login, "@", current_profile.host, "/", NULL);
			ul.to = g_file_get_relative_path(g_file_new_for_path(tmpdir), g_file_new_for_path(ul.from));
			to_upload_file(&ul);
		}
	}
}

static gboolean profiles_treeview_row_is_separator(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	return is_edit_profiles_selected_nth_item(iter, "1");
}

static void on_configure_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY)
	{
		save_profiles(1);
	}
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
	gtk_tree_view_append_column(GTK_TREE_VIEW(file_view), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(file_view), FALSE);
	
	PangoFontDescription *pfd = pango_font_description_new();
	pango_font_description_set_size(pfd, 8 * PANGO_SCALE);
	gtk_widget_modify_font(file_view, pfd);
	
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(file_view), FILEVIEW_COLUMN_INFO);
	
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
	
	load_settings();
	
	gtk_widget_show_all(box);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), box, gtk_label_new(_("FTP")));
	gdk_threads_leave();
	
	plugin_signal_connect(geany_plugin, NULL, "document-save", TRUE, G_CALLBACK(on_document_save), NULL);
}

GtkWidget *plugin_configure(GtkDialog *dialog)
{
	GtkWidget *widget, *vbox, *table;
	
	vbox = gtk_vbox_new(FALSE, 6);
	
	widget = gtk_label_new("<b>Profiles</b>");
	gtk_label_set_use_markup(GTK_LABEL(widget), TRUE);
	gtk_misc_set_alignment(GTK_MISC(widget), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	
	GtkListStore *store;
	store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkTreeIter iter;
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "New profile...", -1);
	widget = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	gtk_widget_set_size_request(widget, 200, -1);
	gtk_list_store_append(store, &iter); //for separator
	pref.store = store;
	pref.combo = widget;
	g_object_unref(G_OBJECT(store));
	
	GtkCellRenderer *renderer;
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widget), renderer, TRUE);
	gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(widget), renderer, "text", 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
	gtk_widget_set_tooltip_text(widget, "Choose a profile to edit or choose New profile to create one.");
	gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(widget), (GtkTreeViewRowSeparatorFunc)profiles_treeview_row_is_separator, NULL, NULL);
	g_signal_connect(widget, "changed", G_CALLBACK(on_edit_profiles_changed), NULL);
	
	load_profiles(1);
	
	table = gtk_table_new(4, 4, FALSE);
	
	gtk_table_attach(GTK_TABLE(table), widget, 0, 2, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_delete_profile_clicked), NULL);
	pref.delete = widget;
	check_delete_button_sensitive(NULL);
	
	widget = gtk_label_new("Host");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), NULL);
	pref.host = widget;
	
	widget = gtk_label_new("Port");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_size_request(widget, 40, -1);
	gtk_entry_set_text(GTK_ENTRY(widget), "21");
	gtk_table_attach(GTK_TABLE(table), widget, 3, 4, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), NULL);
	pref.port = widget;
	
	widget = gtk_label_new("Login");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), NULL);
	pref.login = widget;
	widget = gtk_check_button_new_with_label("Anonymous");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_use_anonymous_toggled), NULL);
	pref.anon = widget;
	
	widget = gtk_label_new("Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), NULL);
	pref.passwd = widget;
	widget = gtk_check_button_new_with_label("Show");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_show_password_toggled), NULL);
	pref.showpass = widget;
	
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	
	gtk_widget_show_all(vbox);
	
	g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);
	return vbox;
}

void plugin_cleanup(void)
{
	curl_global_cleanup();
	g_free(profiles_file);
	g_free(tmp_dir);
	g_strfreev(all_profiles);
	gtk_widget_destroy(box);
}
