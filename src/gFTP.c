#include <geanyplugin.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "ftpparse.c"
#include "gFTP.h"

GeanyPlugin *geany_plugin;
GeanyData *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO("gFTP", "FTP Plugin for Geany.", "1.0", "Cai Guanhao <caiguanhao@gmail.com>");

PLUGIN_KEY_GROUP(gFTP, KB_COUNT)

static void try_another_username_password()
{
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

static void fileview_scroll_to_iter(GtkTreeIter *iter)
{
	GtkTreePath *path;
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), iter);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(file_view), path, NULL, TRUE, 0.0, 0.0);
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
	if (gtk_list_store_iter_is_valid(pending_store, &current_pending)) {
		gint *pulse;
		gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 3, &pulse, -1);
		if (pulse>=0) {
			gtk_list_store_set(pending_store, &current_pending, 3, pulse + 1, -1);
		}
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

static void add_pending_item(gint type, gchar *n1, gchar *n2)
{
	GtkTreeIter iter;
	switch (type) {
		case 0: //upload
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_GO_UP, 
			1, "0%", 2, 0, 3, -1, 4, n1, 5, n2,
			-1);
			break;
		case 1: //download
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_GO_DOWN, 
			1, "0%", 2, 0, 3, -1, 4, n1, 5, n2,
			-1);
			break;
		case 2: //load dir
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_REFRESH, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, g_utf8_strlen(n2, -1)>0?n2:"",
			-1);
			break;
		case 3: //commands
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_EXECUTE, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, n2,
			-1);
			break;
		case 55: //create new file
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_NEW, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, "",
			-1);
			break;
	}
	execute();
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

static gboolean redefine_parent_iter(gchar *src, gboolean strict_mode)
{
	GtkTreeIter parent_iter;
	GtkTreeIter iter;
	gboolean valid;
	gchar *name;
	gchar *icon;
	gchar **srcs;
	gint i=0;
	srcs = g_strsplit(src, "/", 0);
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &parent_iter)) {
		p1:
		if (g_strcmp0("", srcs[i])==0) {
			parent = parent_iter;
			goto true;
		} else {
			if (gtk_tree_model_iter_children(GTK_TREE_MODEL(file_store), &iter, &parent_iter)) {
				p2:
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_NAME, &name, FILEVIEW_COLUMN_ICON, &icon, -1);
				if (utils_str_equal(icon, GTK_STOCK_DIRECTORY) && g_strcmp0(name, srcs[i])==0) {
					if (i<g_strv_length(srcs)-1) {
						i++;
						parent_iter = iter;
						goto p1;
					} else {
						parent = iter;
						goto true;
					}
				} else {
					valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(file_store), &iter);
					if (valid) goto p2; else if(!strict_mode) {
						parent = parent_iter;
						goto true;
					}
				}
			}
		}
	}
	g_free(name);
	g_free(icon);
	g_strfreev(srcs);
	return FALSE;
	true:
	g_free(name);
	g_free(icon);
	g_strfreev(srcs);
	return TRUE;
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
	gtk_list_store_clear(pending_store);
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
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

static int download_progress (void *p, double dltotal, double dlnow, double ultotal, double ulnow)
{
	if (to_abort) return 1;
	gdk_threads_enter();
	double done=0.0;
	int done2=0;
	if(dlnow!=0&&dltotal!=0)done=(double)dlnow/dltotal;
	if(dlnow!=0&&dltotal!=0)done2=(int)dlnow/dltotal*100;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), done);
	gchar *doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
	gtk_list_store_set(GTK_LIST_STORE(pending_store), &current_pending, 1, doneper, 2, done2, -1);
	g_free(doneper);
	gdk_threads_leave();
	return 0;
}

static int normal_progress (void *p, double dltotal, double dlnow, double ultotal, double ulnow)
{
	if (to_abort) return 1;
	return 0;
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

static size_t write_function(void *ptr, size_t size, size_t nmemb, struct string *str)
{
	int new_len = str->len + size * nmemb;
	str->ptr = realloc(str->ptr, new_len + 1);
	memcpy(str->ptr + str->len, ptr, size * nmemb);
	str->ptr[new_len] = '\0';
	str->len = new_len;
	return size*nmemb;
}

static size_t write_data (void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	return fwrite(ptr, size, nmemb, stream);
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *p)
{
	gdk_threads_enter();
	struct uploadf *file = (struct uploadf *)p;
	size_t retcode = fread(ptr, size, nmemb, file->fp);
	file->transfered+=retcode;
	double done=0.0;
	int done2=0;
	done=(double)file->transfered/file->filesize;
	done2=(int)(done*100);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), done);
	gchar *doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
	gtk_list_store_set(GTK_LIST_STORE(pending_store), &current_pending, 1, doneper, 2, done2, -1);
	g_free(doneper);
	gdk_threads_leave();
	return retcode;
}

static void *download_file(gpointer p)
{
	struct transfer *dl = (struct transfer *)p;
	gchar *dlto = g_strdup(dl->to);
	gchar *dlfrom = g_strdup(dl->from);
	if (curl) {
		FILE *fp;
		fp=fopen(dlto,"wb");
		dlfrom = g_strconcat(current_profile.url, dlfrom, NULL);
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, dlfrom);
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
	if (!to_abort && dlto)
		if (!document_open_file(dlto, FALSE, NULL, NULL))
			if (dialogs_show_question("Could not open the file in Geany. View it in file browser?"))
				open_external(g_path_get_dirname(dlto));
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
	g_free(dlto);
	g_free(dlfrom);
	execute_end(1);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *upload_file(gpointer p)
{
	struct transfer *ul = (struct transfer *)p;
	gchar *ulto = g_strdup(ul->to);
	gchar *ulfrom = g_strdup(ul->from);
	struct uploadf file;
	if (curl) {
		file.transfered=0;
		file.fp=fopen(ulfrom,"rb");
		fseek(file.fp, 0L, SEEK_END);
		file.filesize = ftell(file.fp);
		fseek(file.fp, 0L, SEEK_SET);
		ulto = g_strconcat(current_profile.url, ulto, NULL);
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, ulto);
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
	}
	gdk_threads_enter();
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
	g_free(ulto);
	g_free(ulfrom);
	execute_end(0);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *create_file(gpointer p)
{
	gchar *ulto = g_strdup((gchar *)p);
	if (curl) {
		ulto = g_strconcat(current_profile.url, ulto, NULL);
		if (!g_utf8_validate(ulto, -1, NULL)) {
			gdk_threads_enter();
			dialogs_show_msgbox(GTK_MESSAGE_ERROR, "Error occurred. Try again later.");
			gdk_threads_leave();
			goto end;
		}
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, ulto);
		curl_easy_setopt(curl, CURLOPT_PORT, port);
		curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
		curl_easy_perform(curl);
	}
	end:
	gdk_threads_enter();
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
	g_free(ulto);
	execute_end(55);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *get_dir_listing(gpointer p)
{
	struct transfer *ls = (struct transfer *)p;
	gchar *lsfrom = g_strdup(ls->from);
	gchar *lsto = g_strdup(ls->to);
	gboolean auto_scroll = FALSE;
	if (g_strcmp0(lsto, "")!=0) {
		if (!redefine_parent_iter(lsto, TRUE))
			goto end;
		else
			auto_scroll = TRUE;
	}
	clear_children();
	const char *url = g_strconcat(current_profile.url, lsfrom, NULL);
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
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, normal_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		if (current_settings.showhiddenfiles)
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST -a");
		curl_easy_perform(curl);
	}
	if (!to_abort) {
		if (to_list(str.ptr)==0) {
			gdk_threads_enter();
			gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_DISCONNECT);
			if (auto_scroll) fileview_scroll_to_iter(&parent);
			gdk_threads_leave();
		}
	}
	free(str.ptr);
	end:
	gdk_threads_enter();
	g_free(lsfrom);
	g_free(lsto);
	ui_progress_bar_stop();
	execute_end(2);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

static void *send_command(gpointer p)
{
	struct commands *comm = (struct commands *)p;
	gchar *name = g_strdup(comm->name);
	const char *url = g_strconcat(current_profile.url, name, NULL);
	struct curl_slist *headers = comm->list;
	if (curl) {
		gint64 port = g_ascii_strtoll(current_profile.port, NULL, 0);
		if (port<=0 || port>65535) port=21;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_PORT, port);
		curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, normal_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headers);
		curl_easy_perform(curl);
	}
	curl_slist_free_all(headers);
	gdk_threads_enter();
	ui_progress_bar_stop();
	execute_end(3);
	gdk_threads_leave();
	g_thread_exit(NULL);
	return NULL;
}

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
		add_pending_item(2, "", NULL);
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

static void *to_create_file(gpointer p)
{
	curl_easy_reset(curl);
	ui_progress_bar_start("Please wait...");
	g_thread_create(&create_file, (gpointer)p, FALSE, NULL);
	return NULL;
}

static void execute()
{
	g_return_if_fail(running==FALSE);
	running = TRUE;
	gchar *icon;
	gchar *remote;
	gchar *local;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pending_store), &current_pending)) {
		gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 
		0, &icon, 4, &remote, 5, &local, -1);
		if (utils_str_equal(icon, GTK_STOCK_GO_UP)) {
			struct transfer ul;
			ul.from = g_strdup(local);
			ul.to = g_strdup(remote);
			to_upload_file(&ul);
			log_new_str(COLOR_BLUE, "Upload request has been added to the pending list.");
		} else if (utils_str_equal(icon, GTK_STOCK_REFRESH)) {
			struct transfer ls;
			ls.from = g_strconcat(g_path_get_dirname(remote), "/", NULL);
			ls.to = g_strdup(local);
			to_get_dir_listing(&ls);
			log_new_str(COLOR_BLUE, "Get directory listing request has been added to the pending list.");
		} else if (utils_str_equal(icon, GTK_STOCK_GO_DOWN)) {
			struct transfer dl;
			dl.from = g_strdup(remote);
			dl.to = g_strdup(local);
			to_download_file(&dl);
			log_new_str(COLOR_BLUE, "Download request has been added to the pending list.");
		} else if (utils_str_equal(icon, GTK_STOCK_EXECUTE)) {
			gchar **comms = g_strsplit(local, "\n", 0);
			gint i;
			struct commands comm;
			comm.name = g_strdup(remote);
			comm.list = NULL;
			for (i=0; i<g_strv_length(comms); i++)
				comm.list = curl_slist_append(comm.list, comms[i]);
			g_strfreev(comms);
			to_send_commands(&comm);
			log_new_str(COLOR_BLUE, "Command request has been added to the pending list.");
		} else if (utils_str_equal(icon, GTK_STOCK_NEW)) {
			to_create_file(remote);
			log_new_str(COLOR_BLUE, "Create file request has been added to the pending list.");
		} else {
			running = FALSE;
		}
	}
	g_free(icon);
	g_free(remote);
	g_free(local);
}

static void execute_end(gint type)
{
	gboolean delete = FALSE;
	gchar *icon;
	if (gtk_list_store_iter_is_valid(pending_store, &current_pending)) {
		gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 0, &icon, -1);
		
		if (type==0 && utils_str_equal(icon, GTK_STOCK_GO_UP)) delete = TRUE;
		if (type==1 && utils_str_equal(icon, GTK_STOCK_GO_DOWN)) delete = TRUE;
		if (type==2 && utils_str_equal(icon, GTK_STOCK_REFRESH)) delete = TRUE;
		if (type==3 && utils_str_equal(icon, GTK_STOCK_EXECUTE)) delete = TRUE;
		if (type==55 && utils_str_equal(icon, GTK_STOCK_NEW)) delete = TRUE;
		
		if (delete) 
			gtk_list_store_remove(GTK_LIST_STORE(pending_store), &current_pending);
	}
	running = FALSE;
	if (!to_abort && gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pending_store), &current_pending)) {
		execute();
	}
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

static void to_connect(GtkMenuItem *menuitem, int p)
{
	to_abort = FALSE;
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
	
	on_open_clicked(NULL, current_profile.remote);
}

static void *disconnect(gpointer p)
{
	to_abort = TRUE;
	gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(btn_connect), GTK_STOCK_CONNECT);
	if (curl) {
		log_new_str(COLOR_RED, "Disconnected.");
		curl = NULL;
	}
	clear();
	return NULL;
}

static GtkWidget *create_popup_menu(void)
{
	GtkWidget *item, *menu;
	
	menu = gtk_menu_new();
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "_Open (Download/Edit)");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_open_clicked), NULL);
	
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu), item);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_DIRECTORY, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "Create _Folder");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)1);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "Delete _Empty Folder");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)2);
	
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu), item);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "Create _Blank File");
	
	GtkAccelGroup *kb_accel_group;
	kb_accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(geany->main_widgets->window), kb_accel_group);
	GeanyKeyBinding *kb = keybindings_get_item(plugin_key_group, KB_CREATE_BLANK_FILE);
	gtk_widget_add_accelerator(item, "activate", kb_accel_group, kb->key, kb->mods, GTK_ACCEL_VISIBLE);
	g_object_unref(kb_accel_group);
	
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)3);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_REMOVE, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "_Delete File");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)4);
	
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu), item);
	
	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_EDIT, NULL);
	gtk_menu_item_set_label(GTK_MENU_ITEM(item), "_Rename");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)5);
	
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu), item);
	
	item = gtk_check_menu_item_new_with_mnemonic("Show _Hidden Files");
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), current_settings.showhiddenfiles);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "toggled", G_CALLBACK(on_menu_item_clicked), (gpointer)99);
	
	gtk_widget_show_all(menu);
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

static gchar *load_profile_property(gint index, gchar *field)
{
	GKeyFile *profiles = g_key_file_new();
	profiles_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, "gFTP", G_DIR_SEPARATOR_S, "profiles.conf", NULL);
	g_key_file_load_from_file(profiles, profiles_file, G_KEY_FILE_NONE, NULL);
	all_profiles = g_key_file_get_groups(profiles, &all_profiles_length);
	field = utils_get_setting_string(profiles, all_profiles[index], field, "");
	g_key_file_free(profiles);
	return field;
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
				4, utils_get_setting_string(profiles, all_profiles[i], "remote", ""), 
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
			current_profile.remote = utils_get_setting_string(profiles, all_profiles[i], "remote", "");
		}
	}
	g_key_file_free(profiles);
}

static void select_profile(gint index)
{
	GtkTreeIter iter;
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(pref.store), &iter, NULL, index+2)) {
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
		on_edit_profiles_changed();
	}
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
			gchar *unique_id = NULL;
			gchar *host = NULL;
			gchar *port = NULL;
			gchar *login = NULL;
			gchar *password = NULL;
			gchar *remote = NULL;
			while (valid) {
				gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
				0, &host, 
				1, &port, 
				2, &login, 
				3, &password, 
				4, &remote, 
				-1);
				host = g_strstrip(host);
				if (g_strcmp0(host, "")!=0) {
					port = g_strstrip(port);
					login = g_strstrip(login);
					password = encrypt(g_strstrip(password));
					remote = g_strstrip(remote);
					unique_id = g_strconcat(host, "\n", port, "\n", login, "\n", 
					password, "\n", remote, NULL);
					unique_id = g_compute_checksum_for_string(G_CHECKSUM_MD5, unique_id, g_utf8_strlen(unique_id, -1));
					g_key_file_set_string(profiles, unique_id, "host", host);
					g_key_file_set_string(profiles, unique_id, "port", port);
					g_key_file_set_string(profiles, unique_id, "login", login);
					g_key_file_set_string(profiles, unique_id, "password", password);
					g_key_file_set_string(profiles, unique_id, "remote", remote);
				}
				g_free(unique_id);
				g_free(host);
				g_free(port);
				g_free(login);
				g_free(password);
				g_free(remote);
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
			g_key_file_set_string(profiles, all_profiles[i], "remote", current_profile.remote);
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

static void save_settings()
{
	GKeyFile *config = g_key_file_new();
	gchar *data;
	gchar *config_dir = g_path_get_dirname(config_file);
	g_key_file_load_from_file(config, config_file, G_KEY_FILE_NONE, NULL);
	
	g_key_file_set_boolean(config, "gFTP-main", "show_hidden_files", current_settings.showhiddenfiles);
	
	if (g_file_test(config_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(config_dir, TRUE) == 0) {
		data = g_key_file_to_data(config, NULL, NULL);
		utils_write_file(config_file, data);
		g_free(data);
	}
	g_free(config_dir);
	g_key_file_free(config);
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
	GKeyFile *settings = g_key_file_new();
	config_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, "gFTP", G_DIR_SEPARATOR_S, "settings.conf", NULL);
	g_key_file_load_from_file(settings, config_file, G_KEY_FILE_NONE, NULL);
	current_settings.showhiddenfiles = utils_get_setting_boolean(settings, "gFTP-main", "show_hidden_files", FALSE);
	g_key_file_free(settings);
	
	load_profiles(0);
	load_hosts();
	tmp_dir = g_strdup_printf("%s/gFTP/",(char *)g_get_tmp_dir());
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

static void on_menu_item_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	g_return_if_fail(adding==FALSE);
	adding = TRUE;
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
		switch (type) {
			case 1:
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				if (!is_folder_selected(list)) {
					name = g_path_get_dirname(name);
				}
				if (g_strcmp0(name, ".")==0) name = "";
				gchar *cmd = NULL;
				cmd = dialogs_show_input("New Folder", GTK_WINDOW(geany->main_widgets->window), "Please input folder name:\n(to create multi-level folders,\nuse Create Blank File instead)", "New Folder");
				if (cmd && g_utf8_strlen(cmd, -1)>0) {
					add_pending_item(3, name, g_strdup_printf("MKD %s", cmd));
					if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
					g_free(cmd);
				}
				break;
			case 2:
				if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
					if (is_folder_selected(list)) {
						gtk_tree_model_get(model, &parent, FILEVIEW_COLUMN_DIR, &name, -1);
						gchar *dirname = NULL;
						if (gtk_tree_store_iter_is_valid(file_store, &iter)) {
							gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_NAME, &dirname, -1);
							if (dirname) {
								add_pending_item(3, name, g_strdup_printf("RMD %s", dirname));
								if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
								g_free(dirname);
							}
						}
					}
				}
				break;
			case 3:
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				if (!is_folder_selected(list)) {
					name = g_path_get_dirname(name);
				}
				if (g_strcmp0(name, ".")==0) name = "";
				gchar *filename = NULL;
				filename = dialogs_show_input("Create Blank File", GTK_WINDOW(geany->main_widgets->window), "Please input file name:\n(recursively create missing folders,\neg. multi/level/folder.htm)", "New File");
				gchar *filepath;
				if (filename && g_utf8_strlen(filename, -1)>0) {
					filepath = g_strdup_printf("%s%s", name, filename);
					add_pending_item(55, filepath, "");
					if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
					g_free(filepath);
					g_free(filename);
				}
				break;
			case 4:
				if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
					if (!is_folder_selected(list)) {
						gtk_tree_model_get(model, &parent, FILEVIEW_COLUMN_DIR, &name, -1);
						gchar *dirname = NULL;
						if (gtk_tree_store_iter_is_valid(file_store, &iter)) {
							gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_NAME, &dirname, -1);
							if (dirname) {
								add_pending_item(3, name, g_strdup_printf("DELE %s", dirname));
								if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
								g_free(dirname);
							}
						}
					}
				}
				break;
			case 5:
				if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
					gtk_tree_model_get(model, &parent, FILEVIEW_COLUMN_DIR, &name, -1);
					gchar *dirname = NULL;
					if (gtk_tree_store_iter_is_valid(file_store, &iter)) {
						gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_NAME, &dirname, -1);
						if (dirname) {
							gchar *reto;
							reto = dialogs_show_input("Rename To", GTK_WINDOW(geany->main_widgets->window), "Please input new folder name:", dirname);
							if (reto && g_utf8_strlen(reto, -1)>0) {
								if (g_strcmp0(reto, dirname)!=0) {
									add_pending_item(3, name, g_strdup_printf("RNFR %s\nRNTO %s", dirname, reto));
									if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
								}
								g_free(reto);
							}
							g_free(dirname);
						}
					}
				}
				break;
			case 99:
				current_settings.showhiddenfiles = 	gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem));
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				if (!is_folder_selected(list)) {
					name = g_path_get_dirname(name);
				}
				if (g_strcmp0(name, ".")==0) name = "";
				if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
				break;
		}
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	adding = FALSE;
}

static void on_open_clicked(GtkMenuItem *menuitem, gpointer p)
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
			add_pending_item(2, name, NULL);
			if (p) {
				gchar *remote = (gchar *)p;
				gchar **remoteparts = g_strsplit(remote, "/", 0);
				gint i;
				remote = g_strdup("");
				for (i=0; i<g_strv_length(remoteparts); i++) {
					if (g_strcmp0(remoteparts[i], "")!=0) {
						remote = g_strconcat(remote, remoteparts[i], "/", NULL);
						add_pending_item(2, remote, remote);
					}
				}
			}
		} else {
			gchar *filepath;
			filepath = g_strconcat(tmp_dir, current_profile.login, "@", current_profile.host, NULL);
			if (g_strcmp0(g_path_get_dirname(name), ".")!=0)
				filepath = g_strconcat(filepath, "/", g_path_get_dirname(name), NULL);
			g_mkdir_with_parents(filepath, 0777);
			filepath = g_strdup_printf("%s/%s", filepath, g_path_get_basename(name));
			add_pending_item(1, name, filepath);
		}
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
}

static void on_connect_clicked(gpointer p)
{
	if (!curl) {
		GtkWidget *item, *menu;
		menu = gtk_menu_new();
		gsize i;
		for (i = 0; i < all_profiles_length; i++) {
			item = gtk_menu_item_new_with_label(load_profile_property(i, "host"));
			gtk_widget_show(item);
			gtk_container_add(GTK_CONTAINER(menu), item);
			g_signal_connect(item, "activate", G_CALLBACK(to_connect), (gpointer)i);
		}
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_func, NULL, 0, GDK_CURRENT_TIME);
	} else {
		disconnect(NULL);
	}
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model = GTK_TREE_MODEL(file_store);
	GList *list;
	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(file_view));
	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	
	if (is_single_selection(treesel)) {
		if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
			on_open_clicked(NULL, NULL);
			return TRUE;
		} else if (event->button == 3) {
			static GtkWidget *popup_menu = NULL;
			if (popup_menu==NULL) popup_menu = create_popup_menu();
			gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, NULL, NULL, event->button, event->time);
		}
	}
	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	return FALSE;
}

static void on_edit_preferences(void)
{
	plugin_show_configure(geany_plugin);
}

static void *on_host_login_password_changed(GtkWidget *widget, GdkEventKey *event, GtkDialog *dialog)
{
	if (dialog && (event->keyval == 0xff0d || event->keyval == 0xff8d)) { //RETURN AND KEY PAD ENTER
		gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
		return FALSE;
	}
	if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(pref.host)), "")!=0) {
		if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &pref.iter_store_new) || is_edit_profiles_selected_nth_item(&pref.iter_store_new, "0")) {
			gtk_list_store_append(GTK_LIST_STORE(pref.store), &pref.iter_store_new);
		}
		gtk_list_store_set(GTK_LIST_STORE(pref.store), &pref.iter_store_new, 
		0, gtk_entry_get_text(GTK_ENTRY(pref.host)), 
		1, gtk_entry_get_text(GTK_ENTRY(pref.port)), 
		2, gtk_entry_get_text(GTK_ENTRY(pref.login)), 
		3, gtk_entry_get_text(GTK_ENTRY(pref.passwd)), 
		4, gtk_entry_get_text(GTK_ENTRY(pref.remote)), 
		-1);
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &pref.iter_store_new);
	}
	return FALSE;
}

static void on_use_current_clicked(GtkButton *button, gpointer user_data)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(file_view), &path, NULL);
	if (path) {
		gchar *name = "";
		gchar *icon;
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(file_store), &iter, path)) {
			gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_ICON, &icon, FILEVIEW_COLUMN_DIR, &name, -1);
			if (!utils_str_equal(icon, GTK_STOCK_DIRECTORY))
				name = g_path_get_dirname(name);
		}
		if (g_strcmp0(name, ".")==0) name = g_strconcat("", NULL);
		if (g_utf8_strlen(name, -1)>0) {
			name = g_strconcat("/", name, NULL);
		}
		gtk_entry_set_text(GTK_ENTRY(pref.remote), name);
		on_host_login_password_changed(NULL, NULL, NULL);
		g_free(name);
		g_free(icon);
	}
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
	gchar *remote = g_strdup_printf("%s", "");
	if (!is_edit_profiles_selected_nth_item(&iter, "0")) {
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &iter)) {
			gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
			0, &host, 
			1, &port, 
			2, &login, 
			3, &password, 
			4, &remote, 
			-1);
		}
	}
	gtk_entry_set_text(GTK_ENTRY(pref.host), host);
	gtk_entry_set_text(GTK_ENTRY(pref.port), port);
	gtk_entry_set_text(GTK_ENTRY(pref.login), login);
	gtk_entry_set_text(GTK_ENTRY(pref.passwd), password);
	gtk_entry_set_text(GTK_ENTRY(pref.remote), remote);
	g_free(host);
	g_free(port);
	g_free(login);
	g_free(password);
	g_free(remote);
	
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
		gchar *filepath;
		gchar *tmpdir;
		gchar *dst;
		filepath = document_get_current()->real_path;
		tmpdir = g_strconcat(tmp_dir, current_profile.login, "@", current_profile.host, "/", NULL);
		dst = g_file_get_relative_path(g_file_new_for_path(tmpdir), g_file_new_for_path(filepath));
		add_pending_item(0, dst, filepath);
		if (redefine_parent_iter(dst, FALSE)) add_pending_item(2, dst, NULL);
		g_free(tmpdir);
	}
}

static void on_configure_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY)
	{
		save_settings();
		save_profiles(1);
	}
}

static void on_window_drag_data_received(GtkWidget *widget, GdkDragContext *drag_context, gint x, gint y, GtkSelectionData *data, guint target_type, guint event_time, gpointer user_data)
{
	gboolean success = FALSE;

	if (curl && data->length > 0 && data->format == 8) {
		gchar *filenames = g_strndup((gchar *) data->data, data->length);
		gchar *filename;
		filename = strtok(filenames, "\r\n");
		
		GtkTreePath *path;
		GtkTreeIter iter;
		gchar *name;
		gchar *icon;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(file_view), &path, NULL);
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(file_store), &iter, path)) {
			gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_ICON, &icon, FILEVIEW_COLUMN_DIR, &name, -1);
			if (!utils_str_equal(icon, GTK_STOCK_DIRECTORY))
				name = g_path_get_dirname(name);
		} else {
			name = "";
		}
		if (!g_str_has_suffix(name, "/")) name = g_strconcat(name, "/", NULL);
		if (g_strcmp0(name, "./")==0 || g_strcmp0(name, "/")==0) name = "";
		gchar *dst;
		gchar *filepath;
		redefine_parent_iter(name, FALSE);
		while (filename!=NULL) {
			filepath = g_filename_from_uri(filename, NULL, NULL);
			dst = g_strconcat(name, g_path_get_basename(filepath), NULL);
			add_pending_item(0, dst, filepath);
			filename = strtok(NULL, "\r\n");
		}
		add_pending_item(2, dst, NULL);
		g_free(filenames);
		g_free(filename);
		success = TRUE;
	}
	gtk_drag_finish(drag_context, success, FALSE, event_time);
}

static gboolean drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint t, gpointer user_data)
{
	GtkTreePath *path;
	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(file_view), x, y, &path, NULL, NULL, NULL);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(file_view), path, NULL, FALSE);
	return TRUE;
}

static gboolean profiles_treeview_row_is_separator(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	return is_edit_profiles_selected_nth_item(iter, "1");
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
	pango_font_description_free(pfd);
	
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(file_view), FILEVIEW_COLUMN_INFO);
	
	g_signal_connect(file_view, "button-press-event", G_CALLBACK(on_button_press), NULL);
	
	const GtkTargetEntry drag_dest_types[] = {
		{ "STRING",			0, 0 },
		{ "UTF8_STRING",	0, 0 },
		{ "text/plain",		0, 0 },
		{ "text/uri-list",	0, 0 }
	};
	gtk_drag_dest_set(file_view, GTK_DEST_DEFAULT_ALL, drag_dest_types, G_N_ELEMENTS(drag_dest_types), GDK_ACTION_COPY);
	
	g_signal_connect(file_view, "drag-data-received", G_CALLBACK(on_window_drag_data_received), NULL);
	g_signal_connect(file_view, "drag-motion", G_CALLBACK(drag_motion), NULL);
}

static void prepare_pending_view()
{
	PangoFontDescription *pfd = pango_font_description_new();
	pango_font_description_set_size(pfd, 8 * PANGO_SCALE);
	gtk_widget_modify_font(pending_view, pfd);
	
	pending_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(pending_view), GTK_TREE_MODEL(pending_store));
	g_object_unref(pending_store);
	
	GtkCellRenderer *text_renderer, *icon_renderer, *progress_renderer;
	GtkTreeViewColumn *column;
	GtkWidget *widget;
	
	column = gtk_tree_view_column_new();
	icon_renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	widget = gtk_image_new_from_stock(GTK_STOCK_FILE, GTK_ICON_SIZE_MENU);
	gtk_widget_show(widget);
	gtk_tree_view_column_set_widget(column, widget);
	gtk_tree_view_column_set_attributes(column, icon_renderer, "stock-id", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pending_view), column);
	
	column = gtk_tree_view_column_new();
	progress_renderer = gtk_cell_renderer_progress_new();
	gtk_tree_view_column_pack_start(column, progress_renderer, TRUE);
	widget = gtk_label_new("Progress");
	gtk_widget_modify_font(widget, pfd);
	gtk_widget_show(widget);
	gtk_tree_view_column_set_widget(column, widget);
	gtk_tree_view_column_set_attributes(column, progress_renderer, "text", 1, "value", 2, "pulse", 3, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pending_view), column);
	
	column = gtk_tree_view_column_new();
	text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
	widget = gtk_label_new("Remote");
	gtk_widget_modify_font(widget, pfd);
	gtk_widget_show(widget);
	gtk_tree_view_column_set_widget(column, widget);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", 4, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pending_view), column);
	
	column = gtk_tree_view_column_new();
	text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
	widget = gtk_label_new("Local");
	gtk_widget_modify_font(widget, pfd);
	gtk_widget_show(widget);
	gtk_tree_view_column_set_widget(column, widget);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", 5, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pending_view), column);
	
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pending_view), TRUE);
	
	pango_font_description_free(pfd);
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

static void kb_activate(guint key_id)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), page_number);
	switch (key_id)
	{
		case KB_CREATE_BLANK_FILE:
			on_menu_item_clicked(NULL, (gpointer)3);
			break;
	}
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
	
	GtkWidget *vpaned = gtk_vpaned_new();
	
	file_view = gtk_tree_view_new();
	prepare_file_view();
	
	pending_view = gtk_tree_view_new();
	prepare_pending_view();
	
	GtkWidget *frame1 = gtk_frame_new(NULL);
	GtkWidget *frame2 = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame1), GTK_SHADOW_IN);
	gtk_frame_set_shadow_type(GTK_FRAME(frame2), GTK_SHADOW_IN);
	gtk_paned_pack1(GTK_PANED(vpaned), frame1, TRUE, FALSE);
	gtk_widget_set_size_request(frame1, -1, 150);
	gtk_paned_pack2(GTK_PANED(vpaned), frame2, FALSE, FALSE);
	gtk_widget_set_size_request(frame2, -1, 150);
	
	widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(widget), file_view);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(widget), file_view);
	gtk_container_add(GTK_CONTAINER(frame1), widget);
	fileview_scroll = widget;
	
	widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(widget), pending_view);
	gtk_container_add(GTK_CONTAINER(frame2), widget);
	
	gtk_box_pack_start(GTK_BOX(box), vpaned, TRUE, TRUE, 0);
	
	load_settings();
	
	gtk_widget_show_all(box);
	page_number = gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), box, gtk_label_new("FTP"));
	gdk_threads_leave();
	
	keybindings_set_item(plugin_key_group, KB_CREATE_BLANK_FILE, kb_activate, 0x04e, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "create_blank_file", "Create Blank File", NULL);
	
	plugin_signal_connect(geany_plugin, NULL, "document-save", TRUE, G_CALLBACK(on_document_save), NULL);
}

GtkWidget *plugin_configure(GtkDialog *dialog)
{
	GtkWidget *widget, *vbox, *hbox, *table, *notebook;
	
	vbox = gtk_vbox_new(FALSE, 6);
	
	notebook = gtk_notebook_new();
	
	GtkListStore *store;
	store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
	
	table = gtk_table_new(5, 4, FALSE);
	
	gtk_table_attach(GTK_TABLE(table), widget, 0, 2, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	gtk_widget_set_tooltip_text(widget, "Delete current profile.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_delete_profile_clicked), NULL);
	pref.delete = widget;
	check_delete_button_sensitive(NULL);
	
	widget = gtk_label_new("Host");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "FTP hostname.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.host = widget;
	
	widget = gtk_label_new("Port");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Default FTP port number is 21.");
	gtk_widget_set_size_request(widget, 40, -1);
	gtk_entry_set_text(GTK_ENTRY(widget), "21");
	gtk_table_attach(GTK_TABLE(table), widget, 3, 4, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.port = widget;
	
	widget = gtk_label_new("Login");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Username for FTP login. Can be left blank.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.login = widget;
	widget = gtk_check_button_new_with_label("Anonymous");
	gtk_widget_set_tooltip_text(widget, "Use anonymous username and password.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_use_anonymous_toggled), NULL);
	pref.anon = widget;
	
	widget = gtk_label_new("Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Password for FTP login. Can be left blank.");
	gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.passwd = widget;
	widget = gtk_check_button_new_with_label("Show");
	gtk_widget_set_tooltip_text(widget, "Show password.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_show_password_toggled), NULL);
	pref.showpass = widget;
	
	widget = gtk_label_new("Remote");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Initial remote directory.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.remote = widget;
	widget = gtk_button_new_with_mnemonic("_Use Current");
	if (!curl) gtk_widget_set_sensitive(widget, FALSE);
	gtk_widget_set_tooltip_text(widget, "Use location of currently selected item as initial remote directory.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_use_current_clicked), NULL);
	pref.usecurrent = widget;
	
	hbox = gtk_hbox_new(FALSE, 6);
	widget = gtk_image_new_from_stock(GTK_STOCK_DND_MULTIPLE, GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	widget = gtk_label_new("Profiles");
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	gtk_widget_show_all(hbox);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table, hbox);
	
	gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);
	
	gtk_widget_show_all(vbox);
	
	g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);
	
	if (curl) 
		select_profile(current_profile.index);
	return vbox;
}

void plugin_cleanup(void)
{
	curl_global_cleanup();
	save_settings();
	g_free(config_file);
	g_free(profiles_file);
	g_free(hosts_file);
	g_free(tmp_dir);
	g_strfreev(all_profiles);
	gtk_widget_destroy(box);
}
