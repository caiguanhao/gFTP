#include <geanyplugin.h>
#include <openssl/ssl.h>
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

static void try_another_username_password(gchar *raw)
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
	gtk_box_set_spacing(GTK_BOX(vbox), 6);
	
	GtkWidget *widget, *table;
	
	table = gtk_table_new(5, 3, FALSE);
	
	widget = gtk_label_new("<b>Wrong Username or Password</b>");
	gtk_label_set_use_markup(GTK_LABEL(widget), TRUE);
	gtk_misc_set_alignment(GTK_MISC(widget), 0.5, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 3, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), widget, 0, 3, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 4);
	widget = gtk_label_new("Message");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_label_new(raw);
	gtk_misc_set_alignment(GTK_MISC(widget), 0, 0);
	gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 3, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_label_new("Login");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(widget), current_profile.login);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(on_retry_entry_keypress), dialog);
	retry.login = widget;
	widget = gtk_check_button_new_with_label("Anonymous");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_retry_use_anonymous_toggled), NULL);
	
	widget = gtk_label_new("Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(widget), current_profile.password);
	gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-press-event", G_CALLBACK(on_retry_entry_keypress), dialog);
	retry.password = widget;
	widget = gtk_check_button_new_with_label("Show");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
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

static void log_new_str(gint color, gchar *text)
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

static int ftp_log(CURL *handle, curl_infotype type, gchar *data, size_t size, void *userp)
{
	if (to_abort) return 0;
	gchar *odata;
	odata = g_strstrip(g_strdup_printf("%s", data));
	gchar *firstline;
	firstline = strtok(odata,"\r\n");
	if (firstline==NULL) return 0;
	gdk_threads_enter();
	if (g_regex_match_simple("^PASS\\s(.*)$", firstline, 0, 0)) {
		firstline = g_strdup_printf("PASS %s", g_strnfill((gsize)(g_utf8_strlen(firstline, -1)-5), '*'));
	}
	switch (type) {
		case CURLINFO_TEXT:
			if (IS_CURRENT_PROFILE_SFTP) log_new_str(COLOR_BLUE, firstline);
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
	if (g_regex_match_simple("^Connected\\sto\\s(.+?)\\s\\((.+?)\\)", firstline, G_REGEX_CASELESS, 0)) {
		GRegex *regex;
		GMatchInfo *match_info;
		regex = g_regex_new("^Connected\\sto\\s(.+?)\\s\\((.+?)\\)", G_REGEX_CASELESS, 0, NULL);
		g_regex_match(regex, firstline, 0, &match_info);
		gchar *thost = g_match_info_fetch(match_info, 1);
		gchar *tipaddr = g_match_info_fetch(match_info, 2);
		if (!g_hostname_is_ip_address(thost) && g_utf8_strlen(tipaddr, -1)>=8) {
			gchar *xhosts = g_strjoinv("\n", all_hosts);
			if (!g_regex_match_simple(g_regex_escape_string(thost, -1), xhosts, G_REGEX_MULTILINE, 0)) {
				xhosts = g_strjoin("\n", xhosts, g_strdup_printf("%s %s", thost, tipaddr), NULL);
				all_hosts = g_strsplit(xhosts, "\n", 0);
				save_hosts();
				log_new_str(COLOR_BLUE, "New host saved.");
			}
			g_free(xhosts);
		}
		g_free(thost);
		g_free(tipaddr);
		g_match_info_free(match_info);
		g_regex_unref(regex);
	}
	if (IS_CURRENT_PROFILE_SFTP) {
		if (g_regex_match_simple("authentication(.*)fail", firstline, 0, 0))
			dialogs_show_msgbox(GTK_MESSAGE_WARNING, "%s\n\nYou may:\n- Check the username.\n- Check the keys on server.", firstline);
		if (g_regex_match_simple("failure.*establish", firstline, G_REGEX_CASELESS, 0))
			dialogs_show_msgbox(GTK_MESSAGE_WARNING, "%s\n\nYou may:\n- Check the hostname or port number.", firstline);
	}
	if (gtk_list_store_iter_is_valid(pending_store, &current_pending)) {
		gint pulse;
		gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 3, &pulse, -1);
		if (pulse>=0) {
			gtk_list_store_set(pending_store, &current_pending, 3, pulse + 1, -1);
		}
	}
	gdk_threads_leave();
	if (g_regex_match_simple("^530|500\\sUSER", firstline, 0, 0)) {
		curl_easy_reset(curl);
		try_another_username_password(firstline);
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
	g_return_if_fail(to_abort==FALSE);
	GtkTreeIter iter;
	switch (type) {
		case 0: //upload
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_GO_UP, 
			1, "0%", 2, 0, 3, -1, 4, n1, 5, n2, 6, 0, 
			-1);
			break;
		case 1: //download
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_GO_DOWN, 
			1, "0%", 2, 0, 3, -1, 4, n1, 5, n2, 6, 1, 
			-1);
			break;
		case 2: //load dir
		case 22: //load dir after previous command (used to abort together)
		case 222: //load dir (cache enabled)
		case 2222: //load dir after previous command (cache enabled)
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_REFRESH, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, n2==NULL?"":n2, 6, type, 
			-1);
			break;
		case 3: //commands
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_EXECUTE, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, n2, 6, 3, 
			-1);
			break;
		case 55: //create new file
			gtk_list_store_append(pending_store, &iter);
			gtk_list_store_set(pending_store, &iter,
			0, GTK_STOCK_NEW, 
			1, "loading", 2, 0, 3, 0, 4, n1, 5, "", 6, 55, 
			-1);
			break;
	}
	execute();
}

static int add_item(gpointer data, gboolean is_dir)
{
	gchar **parts=g_regex_split_simple("\n", data, 0, 0);
	gboolean valid = gtk_tree_store_iter_is_valid(file_store, &parent);
	if (strcmp(parts[0],".")==0||strcmp(parts[0],"..")==0) return 1;
	GtkTreeIter iter;
	gtk_tree_store_append(file_store, &iter, valid?&parent:NULL);
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
	if (valid) {
		gtk_tree_model_get(GTK_TREE_MODEL(file_store), &parent, FILEVIEW_COLUMN_DIR, &parent_dir, -1);
		filename = g_strconcat(parent_dir, parts[0], is_dir?"/":"", NULL);
		g_free(parent_dir);
	} else {
		filename = g_strconcat(parts[0], is_dir?"/":"", NULL);
	}
	gint64 size = g_ascii_strtoll(parts[1], NULL, 0);
	gchar *tsize = g_format_size_for_display(size);
	gchar buffer[80];
	time_t mod_time = g_ascii_strtoll(parts[2], NULL, 0);
	strftime(buffer, 80, "%Y-%m-%d %H:%M:%S (%A)", gmtime(&mod_time));
	gtk_tree_store_set(file_store, &iter,
	FILEVIEW_COLUMN_ICON, is_dir?GTK_STOCK_DIRECTORY:GTK_STOCK_FILE, 
	FILEVIEW_COLUMN_NAME, parts[0], 
	FILEVIEW_COLUMN_DIR, filename, 
	FILEVIEW_COLUMN_INFO, g_strdup_printf("/%s\n%s\n%s", filename, tsize, buffer) , 
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
	if (g_strcmp0(src, NULL)==0) return TRUE; //bug fixed in some machine
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
			return TRUE;
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
						return TRUE;
					}
				} else {
					valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(file_store), &iter);
					if (valid) goto p2; else if(!strict_mode) {
						parent = parent_iter;
						return TRUE;
					}
				}
			}
		}
	}
	return FALSE;
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
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(file_view));
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(pending_view));
	gtk_widget_hide(geany->main_widgets->progressbar);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), 0.0);
}

static int to_list(const gchar *listdata, const gchar *lsf)
{
	if (to_abort) return 1;
	if (strlen(listdata)==0) return 1;
	gchar *odata;
	gtk_tree_model_get(GTK_TREE_MODEL(file_store), &parent, FILEVIEW_COLUMN_DIR, &odata, -1);
	if (g_strcmp0(lsf, "./")!=0 && g_strcmp0(lsf, odata)!=0) return 1;
	odata = g_strdup_printf("%s", listdata);
	gchar *pch;
	pch = strtok(odata, "\r\n");
	struct ftpparse ftp;
	while (pch != NULL)
	{
		if (ftp_parse(&ftp, pch, strlen(pch))) {
			gchar *fileinfo;
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
	gchar *doneper;
	doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_list_store_set(GTK_LIST_STORE(pending_store), &current_pending, 1, doneper, 2, done2, -1);
	struct rate *dlspeed = (struct rate *)p;
	GTimeVal now;
	g_get_current_time(&now);
	double now_us = (double)(now.tv_sec + now.tv_usec/10E5);
	double prev_us = (double)(dlspeed->prev_t.tv_sec + dlspeed->prev_t.tv_usec/10E5);
	double speed;
	if (dlnow!=0 && now_us - prev_us > 0.1) {
		speed = (dlnow - dlspeed->prev_s)/(now_us - prev_us);
		doneper = g_strdup_printf("%s (%s/s)", g_format_size_for_display(dlnow), g_format_size_for_display(speed));
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
		
		dlspeed->prev_s = dlnow;
		g_get_current_time(&dlspeed->prev_t);
	}
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
	if (to_abort) return 0;
	gdk_threads_enter();
	struct uploadf *file = (struct uploadf *)p;
	size_t retcode = fread(ptr, size, nmemb, file->fp);
	file->transfered+=retcode;
	double done=0.0;
	int done2=0;
	done=(double)file->transfered/file->filesize;
	done2=(int)(done*100);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), done);
	gchar *doneper;
	doneper = g_strdup_printf("%.2f%%", done*100);
	gtk_list_store_set(GTK_LIST_STORE(pending_store), &current_pending, 1, doneper, 2, done2, -1);
	GTimeVal now;
	g_get_current_time(&now);
	double now_us = (double)(now.tv_sec + now.tv_usec/10E5);
	double prev_us = (double)(file->prev_t.tv_sec + file->prev_t.tv_usec/10E5);
	double speed;
	if (now_us - prev_us > 0.1) {
		speed = (file->transfered - file->prev_s)/(now_us - prev_us);
		doneper = g_strdup_printf("%s (%s/s)", g_format_size_for_display(file->transfered), g_format_size_for_display(speed));
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(geany->main_widgets->progressbar), doneper);
		
		file->prev_s = file->transfered;
		g_get_current_time(&file->prev_t);
	}
	g_free(doneper);
	gdk_threads_leave();
	return retcode;
}

static gboolean port_config(gchar *str_port)
{
	gint64 port = g_ascii_strtoll(str_port, NULL, 0);
	if (port<=0 || port>65535) port=21;
	return port;
}

static gboolean proxy_config()
{
	if (current_settings.current_proxy>0) {
		gchar *name;
		gchar **nameparts;
		name = g_strdup(current_settings.proxy_profiles[current_settings.current_proxy-1]);
		nameparts = parse_proxy_string(name);
		if (g_strv_length(nameparts)==4) {
			switch (to_proxy_type(nameparts[1])) {
				case 0:
					curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
					break;
				case 1:
					curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
					break;
				case 2:
					curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
					break;
				default:
					return FALSE;
			}
			curl_easy_setopt(curl, CURLOPT_PROXY, nameparts[2]);
			curl_easy_setopt(curl, CURLOPT_PROXYPORT, port_config(nameparts[3]));
			return TRUE;
		}
	}
	return FALSE;
}

static CURLcode sslctx_function(CURL *curl, void *sslctx, void *parm) {
	if (g_regex_match_simple("^-----BEGIN\\sCERTIFICATE-----(.+?)-----END\\sCERTIFICATE-----\n$", current_profile.cert, G_REGEX_MULTILINE | G_REGEX_DOTALL, 0)) {
		X509_STORE *store;
		X509 *cert=NULL;
		BIO *bio;
		char *mypem = current_profile.cert;
		bio=BIO_new_mem_buf(mypem, -1);
		PEM_read_bio_X509(bio, &cert, 0, NULL);
		if (cert == NULL) {
			gdk_threads_enter();
			log_new_str(COLOR_RED, "Error in reading certificates.");
			gdk_threads_leave();
		}
		store = SSL_CTX_get_cert_store((SSL_CTX *)sslctx);
		if (X509_STORE_add_cert(store, cert)==0) {
			gdk_threads_enter();
			log_new_str(COLOR_RED, "Error in adding certificates.");
			gdk_threads_leave();
		}
	}
	return CURLE_OK ;
}

static gchar *certificate_date(const ASN1_UTCTIME *tm) { //from OpenSSL source: /crypto/asn1/t_x509.c
	const char *v;
	int gmt = 0;
	int i;
	int y = 0, M = 0, d = 0, h = 0, m = 0, s = 0;
	i = tm->length;
	v = (const char *)tm->data;
	if (i < 10) return NULL;
	if (v[i-1] == 'Z') gmt=1;
	for (i=0; i<10; i++)
		if ((v[i] > '9') || (v[i] < '0')) return NULL;
	y = (v[0]-'0')*10+(v[1]-'0');
	if (y < 50) y += 100;
	M = (v[2]-'0')*10+(v[3]-'0');
	if ((M > 12) || (M < 1)) return NULL;
	d = (v[4]-'0')*10+(v[5]-'0');
	h = (v[6]-'0')*10+(v[7]-'0');
	m = (v[8]-'0')*10+(v[9]-'0');
	if (tm->length >=12 && (v[10] >= '0') && (v[10] <= '9') && (v[11] >= '0') && (v[11] <= '9'))
		s=(v[10]-'0')*10+(v[11]-'0');
	char *mon[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
	return g_strdup_printf("%s %2d %02d:%02d:%02d %d%s", mon[M-1],d,h,m,s,y+1900,(gmt)?" GMT":"");
}

static gint show_certificates(gchar *raw)
{
	GtkWidget *dialog, *vbox;
	GtkWidget *notebook, *notebook_chains, *widget, *table, *frame, *box;
	gint ret = 0;
	dialog = gtk_dialog_new_with_buttons("Unknown certificate", GTK_WINDOW(geany->main_widgets->window),
		GTK_DIALOG_DESTROY_WITH_PARENT, 
		GTK_STOCK_NO, GTK_RESPONSE_NO, 
		GTK_STOCK_YES, GTK_RESPONSE_YES, 
		NULL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(gtk_dialog_get_action_area(GTK_DIALOG(dialog))), GTK_BUTTONBOX_SPREAD);
	gtk_widget_grab_focus(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO));
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));
	gtk_box_set_spacing(GTK_BOX(vbox), 6);
	
	box = gtk_table_new(1, 2, FALSE);
	widget = gtk_image_new_from_stock(GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_BUTTON);
	gtk_table_attach(GTK_TABLE(box), widget, 0, 1, 0, 1, GTK_SHRINK, GTK_SHRINK, 2, 2);
	widget = gtk_label_new(g_strdup_printf("<b>The certificate from %s:%s is unknown. Make sure this server can be trusted.</b>", current_profile.host, current_profile.port));
	gtk_label_set_use_markup(GTK_LABEL(widget), TRUE);
	gtk_table_attach(GTK_TABLE(box), widget, 1, 2, 0, 1, GTK_SHRINK, GTK_SHRINK, 2, 2);
	gtk_box_pack_start(GTK_BOX(vbox), box, TRUE, TRUE, 0);
	
	gchar *de_fields[] = {"Issued On", "Expires On", "Serial Number", "SHA1 Fingerprint", "MD5 Fingerprint", NULL};
	gchar *is_fields[] = {"Common Name (CN)", "Organization (O)", "Organizational Unit (OU)", "Country (C)", "State/Province (ST)", "Locality (L)", "E-mail (emailAddress)", NULL};
	gchar *cert_keys[] = {"CN","O","OU","C","ST","L","emailAddress", NULL};
	gchar *deta_vals[5];
	gchar *subj_vals[7];
	gchar *issu_vals[7];
	
	GSList *certs = NULL;
	GRegex *regex;
	GMatchInfo *match_info;
	regex = g_regex_new("-----BEGIN\\sCERTIFICATE-----(.+?)-----END\\sCERTIFICATE-----\n", G_REGEX_MULTILINE | G_REGEX_DOTALL, 0, NULL);
	g_regex_match(regex, raw, 0, &match_info);
	while (g_match_info_matches(match_info)) {
		certs = g_slist_prepend(certs, g_match_info_fetch(match_info, 0));
		g_match_info_next(match_info, NULL);
	}
	if (g_slist_length(certs)==0) return 1;
	certs = g_slist_reverse(certs);
	if (g_slist_length(certs)>1) notebook_chains = gtk_notebook_new();
	
	int c, s, i;
	X509 *cert;
	BIO *bio;
	for(c=0; c<g_slist_length(certs); c++) {
		for (i=0; i<5; i++) {
			deta_vals[i] = "";
		}
		for (i=0; i<7; i++) {
			subj_vals[i] = "";
			issu_vals[i] = "";
		}
		
		bio = BIO_new_mem_buf(g_slist_nth_data(certs, c), -1);
		cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
		
		deta_vals[0] = certificate_date(X509_get_notBefore(cert));
		deta_vals[1] = certificate_date(X509_get_notAfter(cert));
		
		char buf[BUFSIZ];
		gchar *sidata;
		int pos;
		
		for (s=0; s<2; s++) {
			if (s==0) {
				X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof buf);
			} else {
				X509_NAME_oneline(X509_get_issuer_name(cert), buf, sizeof buf);
			}
			sidata = g_strdup_printf("%s/CN=", buf);
			for (i=0; i<7; i++) {
				regex = g_regex_new(g_strdup_printf("/%s=(.+?)/(%s)=", cert_keys[i], g_strjoinv("|", cert_keys)), 0, 0, NULL);
				pos = 0;
				while (g_regex_match_full(regex, sidata, -1, pos, 0, &match_info, NULL)) {
					while (g_match_info_matches(match_info)) {
						if (s==0) {
							if (g_utf8_strlen(subj_vals[i], -1)>0) subj_vals[i] = g_strconcat(subj_vals[i], "\n", NULL);
							subj_vals[i] = g_strconcat(subj_vals[i], g_match_info_fetch(match_info, 1), NULL);
						} else {
							if (g_utf8_strlen(issu_vals[i], -1)>0) issu_vals[i] = g_strconcat(issu_vals[i], "\n", NULL);
							issu_vals[i] = g_strconcat(issu_vals[i], g_match_info_fetch(match_info, 1), NULL);
						}
						g_match_info_fetch_pos(match_info, 1, NULL, &pos);
						g_match_info_next(match_info, NULL);
					}
				}
			}
		}
		
		ASN1_INTEGER *num;
		num=X509_get_serialNumber(cert);
		int j;
		for (j=0; j<num->length; j++) {
			if (j==0) 
				deta_vals[2] = g_strdup_printf("%02X", num->data[j]);
			else
				deta_vals[2] = g_strdup_printf("%s:%02X", deta_vals[2], num->data[j]);
		}
		// from openssl source (/apps/x509.c)
		unsigned int n;
		unsigned char md[EVP_MAX_MD_SIZE];
		gchar *digest[2] = {"sha1", "md5"};
		for (i=0; i<2; i++) {
			if (X509_digest(cert, EVP_get_digestbyname(digest[i]), md, &n)) {
				for (j=0; j<(int)n; j++) {
					if (j==0) 
						deta_vals[3+i] = g_strdup_printf("%02X", md[j]);
					else
						deta_vals[3+i] = g_strdup_printf("%s:%02X", deta_vals[3+i], md[j]);
				}
			}
		}
		
		notebook = gtk_notebook_new();
		box = gtk_table_new(2, 2, FALSE);
		
		frame = gtk_frame_new("Details");
		table = gtk_table_new(5, 2, FALSE);
		for (i=0; i<5; i++) {
			widget = gtk_label_new(de_fields[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 1, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 0, 1, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
			widget = gtk_label_new(deta_vals[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 0, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 1, 2, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
		}
		gtk_container_add(GTK_CONTAINER(frame), table);
		gtk_table_attach(GTK_TABLE(box), frame, 0, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 5, 5);
		
		frame = gtk_frame_new("Subject");
		table = gtk_table_new(7, 2, FALSE);
		for (i=0; i<7; i++) {
			widget = gtk_label_new(is_fields[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 1, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 0, 1, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
			widget = gtk_label_new(subj_vals[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 0, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 1, 2, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
		}
		gtk_container_add(GTK_CONTAINER(frame), table);
		gtk_table_attach(GTK_TABLE(box), frame, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 5, 5);
		
		frame = gtk_frame_new("Issuer");
		table = gtk_table_new(7, 2, FALSE);
		for (i=0; i<7; i++) {
			widget = gtk_label_new(is_fields[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 1, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 0, 1, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
			widget = gtk_label_new(issu_vals[i]);
			gtk_misc_set_alignment(GTK_MISC(widget), 0, 0);
			gtk_table_attach(GTK_TABLE(table), widget, 1, 2, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
		}
		gtk_container_add(GTK_CONTAINER(frame), table);
		gtk_table_attach(GTK_TABLE(box), frame, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 5, 5);
		
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, gtk_label_new("General"));
		
		box = gtk_table_new(1, 1, FALSE);
		GtkWidget *txtview;
		GtkTextIter iter;
		GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
		widget = gtk_text_view_new_with_buffer(buffer);
		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert(buffer, &iter, g_slist_nth_data(certs, c), -1);
		PangoFontDescription *pfd = pango_font_description_new();
		pango_font_description_set_size(pfd, 8 * PANGO_SCALE);
		pango_font_description_set_family(pfd, "Monospace");
		gtk_widget_modify_font(widget, pfd);
		pango_font_description_free(pfd);
		gtk_text_view_set_editable(GTK_TEXT_VIEW(widget), FALSE);
		txtview = widget;
		
		widget = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(widget), txtview);
		
		gtk_table_attach(GTK_TABLE(box), widget, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 5, 5);
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, gtk_label_new("Raw"));
		if (g_slist_length(certs)>1) 
			gtk_notebook_append_page(GTK_NOTEBOOK(notebook_chains), notebook, gtk_label_new(g_strdup_printf("Cert. %d", c)));
	}
	
	if (g_slist_length(certs)>1)
		gtk_box_pack_start(GTK_BOX(vbox), notebook_chains, TRUE, TRUE, 0);
	else
		gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
	
	gchar *verify = "N/A";
	regex = g_regex_new("verify\\sreturn\\scode:\\s(\\d+)\\s\\((.+?)\\)", G_REGEX_CASELESS, 0, NULL);
	g_regex_match(regex, raw, 0, &match_info);
	if (g_match_info_matches(match_info)) {
		verify = g_strdup_printf("%s - %s", g_match_info_fetch(match_info, 1), g_utf8_strup(g_match_info_fetch(match_info, 2), -1));
	}
	
	frame = gtk_frame_new("Certificate Verify Result");
	gtk_frame_set_label_align(GTK_FRAME(frame), 0.5, 0.5);
	box = gtk_table_new(1, 1, FALSE);
	widget = gtk_label_new(verify);
	gtk_table_attach(GTK_TABLE(box), widget, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 5, 5);
	gtk_container_add(GTK_CONTAINER(frame), box);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	
	box = gtk_table_new(1, 2, FALSE);
	widget = gtk_image_new_from_stock(GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_BUTTON);
	gtk_table_attach(GTK_TABLE(box), widget, 0, 1, 0, 1, GTK_SHRINK, GTK_SHRINK, 5, 5);
	widget = gtk_label_new("<b>Do you trust this certificate on the server?</b>\nIf you are not sure, it is recommended that you choose No.");
	gtk_label_set_use_markup(GTK_LABEL(widget), TRUE);
	gtk_table_attach(GTK_TABLE(box), widget, 1, 2, 0, 1, GTK_SHRINK, GTK_SHRINK, 5, 5);
	table = gtk_table_new(1, 1, FALSE);
	gtk_table_attach(GTK_TABLE(table), box, 0, 1, 0, 1, GTK_EXPAND, GTK_EXPAND, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
	
	gtk_widget_show_all(dialog);
	ret = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	
	if (ret==GTK_RESPONSE_YES) ret=0; else ret=1;
	if (ret==0) current_profile.cert = g_slist_nth_data(certs, 0);
	
	g_slist_free(certs);
	g_match_info_free(match_info);
	g_regex_unref(regex);
	return ret;
}

static int auth_config()
{
	if (IS_CURRENT_PROFILE_SFTP) {
		gchar *sshkeydir;
		gchar *sshkey;
		gchar *sshkey_public;
		GFile *testfile;
		GFile *testfile2;
		sshkey = g_strdup(current_profile.privatekey);
		sshkey_public = g_strdup_printf("%s.pub", sshkey);
		testfile = g_file_new_for_path(sshkey);
		testfile2 = g_file_new_for_path(sshkey_public);
		sshkeydir = g_strdup_printf("%s/.ssh", g_get_home_dir());
		if (g_utf8_strlen(sshkey, -1)<1 || !g_file_query_exists(testfile, NULL)) {
			sshkey = g_strdup_printf("%s/id_rsa", sshkeydir);
		}
		if (!g_file_query_exists(testfile2, NULL)) {
			sshkey_public = g_strdup_printf("%s/id_rsa.pub", sshkeydir);
		}
		curl_easy_setopt(curl, CURLOPT_SSH_PRIVATE_KEYFILE, sshkey);
		curl_easy_setopt(curl, CURLOPT_SSH_PUBLIC_KEYFILE, sshkey_public);
		g_free(sshkeydir);
		g_free(sshkey_public);
		g_free(sshkey);
	} else {
		gint type = to_auth_type(current_profile.auth);
		switch (type) {
			case 2:
			case 3:
				if (!g_regex_match_simple("^-----BEGIN\\sCERTIFICATE-----(.+?)-----END\\sCERTIFICATE-----\n$", current_profile.cert, G_REGEX_MULTILINE | G_REGEX_DOTALL, 0)) {
					gdk_threads_enter();
					log_new_str(COLOR_BLUE, "Initializing certificates...");
					gdk_threads_leave();

					GError *error = NULL;
					gchar *result = NULL;
					gchar **argv;
					gchar *command;
					command = g_strdup_printf("openssl s_client -connect %s:%s -showcerts -starttls ftp", current_profile.host, current_profile.port);
					g_shell_parse_argv(command, NULL, &argv, NULL);
					if (g_spawn_sync("/usr/bin", argv, NULL, G_SPAWN_LEAVE_DESCRIPTORS_OPEN, NULL, NULL, &result, NULL, NULL, &error)) {
						if (to_abort) return 1;
						gint ret = 1;
						gdk_threads_enter();
						log_new_str(COLOR_BLUE, "Verifying certificate...");
						if (g_regex_match_simple("-----BEGIN\\sCERTIFICATE-----(.+?)-----END\\sCERTIFICATE-----\n", result, G_REGEX_MULTILINE | G_REGEX_DOTALL, 0)) {
							ret = show_certificates(result);
							if (ret==0) log_new_str(COLOR_BLUE, "Certificate verified by user.");
						}
						gdk_threads_leave();
						if (ret!=0) return 1;
					} else {
						gdk_threads_enter();
						log_new_str(COLOR_RED, g_strdup_printf("Error: %s", error->message));
						dialogs_show_msgbox(GTK_MESSAGE_WARNING, "Unable to run /usr/bin/openssl to verify certificate. Make sure OpenSSL is properly installed.");
						gdk_threads_leave();
						g_error_free(error);
						return 1;
					}
				}
				curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
				if (type==2) {
					curl_easy_setopt(curl, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_TLS);
					curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
				} else if (type==3) {
					curl_easy_setopt(curl, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_SSL);
					curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);
				}
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
				curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
				curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
				curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, *sslctx_function);
				break;
		}
	}
	curl_easy_setopt(curl, CURLOPT_PORT, port_config(current_profile.port));
	curl_easy_setopt(curl, CURLOPT_USERNAME, current_profile.login);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, current_profile.password);
	return 0;
}

static void *download_file(gpointer p)
{
	struct transfer *dl = (struct transfer *)p;
	struct rate dlspeed;
	dlspeed.prev_s = 0.0;
	g_get_current_time(&dlspeed.prev_t);
	gchar *dlto = g_strdup(dl->to);
	gchar *dlfrom = g_strdup(dl->from);
	if (curl) {
		FILE *fp;
		fp=fopen(dlto,"wb");
		dlfrom = g_strconcat(current_profile.url, dlfrom, NULL);
		curl_easy_setopt(curl, CURLOPT_URL, find_host(dlfrom));
		proxy_config();
		auth_config();
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, download_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &dlspeed);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		curl_easy_perform(curl);
		fclose(fp);
	}
	gdk_threads_enter();
	if (!to_abort) {
		double val;
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &val);
		if (val>0) log_new_str(COLOR_BLUE, g_strdup_printf("Total download time: %0.3f s.", val));
		curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &val);
		if (val>0) log_new_str(COLOR_BLUE, g_strdup_printf("Average download speed: %s/s.", g_format_size_for_display(val)));
		if (!to_abort && dlto)
			if (!document_open_file(dlto, FALSE, NULL, NULL))
				if (dialogs_show_question("Could not open the file in Geany. View it in file browser?"))
					open_external(g_path_get_dirname(dlto));
	}
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
	gchar *ulto = ul->to;
	gchar *ulfrom = ul->from;
	struct uploadf file;
	g_get_current_time(&file.prev_t);
	if (curl) {
		file.transfered=0;
		if (!(file.fp=fopen(ulfrom,"rb"))) goto end;
		fseek(file.fp, 0L, SEEK_END);
		file.filesize = ftell(file.fp);
		fseek(file.fp, 0L, SEEK_SET);
		ulto = g_strconcat(current_profile.url, ulto, NULL);
		curl_easy_setopt(curl, CURLOPT_URL, find_host(ulto));
		proxy_config();
		auth_config();
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
		curl_easy_setopt(curl, CURLOPT_READDATA, &file);
		curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		curl_easy_perform(curl);
		fclose(file.fp);
	}
	gdk_threads_enter();
	if (!to_abort) {
		double val;
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &val);
		if (val>0) log_new_str(COLOR_BLUE, g_strdup_printf("Total upload time: %0.3f s.", val));
		curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &val);
		if (val>0) log_new_str(COLOR_BLUE, g_strdup_printf("Average upload speed: %s/s.", g_format_size_for_display(val)));
	}
	gdk_threads_leave();
	end:
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
	gchar *ulto = (gchar *)p;
	if (curl) {
		ulto = g_strconcat(current_profile.url, ulto, NULL);
		curl_easy_setopt(curl, CURLOPT_URL, find_host(ulto));
		proxy_config();
		auth_config();
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		curl_easy_perform(curl);
	}
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
	g_mutex_lock(mutex);
	struct transfer *ls = (struct transfer *)p;
	gchar *lsfrom = g_strdup(ls->from);
	gchar *lsto = g_strdup(ls->to);
	gboolean cache_enabled = FALSE;
	gboolean auto_scroll = FALSE;
	gboolean download_listing = TRUE;
	if (ls->cache_enabled==TRUE) cache_enabled = TRUE;
	if (g_strcmp0(lsto, "")!=0) {
		if (!redefine_parent_iter(lsto, TRUE))
			goto end;
		else
			auto_scroll = TRUE;
	}
	const gchar *url = g_strconcat(current_profile.url, lsfrom, NULL);
	struct string str;
	str.len = 0;
	str.ptr = malloc(1);
	str.ptr[0] = '\0';
	GKeyFile *cachekeyfile = g_key_file_new();
	gint unixtime = (gint)time(NULL);
	gchar *listing;
	gchar *ofilename = g_strconcat(current_profile.login, "@", current_profile.host, NULL);
	if (curl) {
		if (current_settings.cache) {
			cache_file = g_build_filename(g_get_user_cache_dir(), "geany", "gFTP", NULL);
			if (g_mkdir_with_parents(cache_file, 0777)==0) {
				gchar *filename = g_compute_checksum_for_string(G_CHECKSUM_MD5, ofilename, g_utf8_strlen(ofilename, -1));
				cache_file = g_build_filename(cache_file, filename, NULL);
				g_free(filename);
				g_key_file_load_from_file(cachekeyfile, cache_file, G_KEY_FILE_NONE, NULL);
				if (cache_enabled) {
					gint ctime;
					ctime = utils_get_setting_integer(cachekeyfile, lsfrom, "_____time", 0);
					if (unixtime-ctime > 7*24*60*60) {
						download_listing = TRUE;
					} else {
						gchar *content = utils_get_setting_string(cachekeyfile, lsfrom, "__rawdata", "");
						gchar *_checksum = utils_get_setting_string(cachekeyfile, lsfrom, "_checksum", "");
						gsize len;
						content = (gchar *)g_base64_decode(content, &len);
						gchar *all = g_strdup_printf("%s\n%s\n%d", lsfrom, content, ctime);
						gchar *checksum = g_compute_checksum_for_string(G_CHECKSUM_MD5, all, g_utf8_strlen(all, -1));
						if (g_strcmp0(checksum, _checksum)==0) {
							download_listing = FALSE;
							listing = g_strdup(content);
							time_t ptime = (time_t)ctime;
							log_new_str(COLOR_BLUE, g_strdup_printf("Reading directory listing cached on %s. [checksum=%s]", utils_get_date_time("%Y-%m-%d %H:%M:%S", &ptime), _checksum));
						} else {
							download_listing = TRUE;
						}
						g_free(all);
						g_free(checksum);
						g_free(_checksum);
						g_free(content);
					}
				} else {
					download_listing = TRUE;
				}
			}
		}
		
		if (download_listing) {
			curl_easy_setopt(curl, CURLOPT_URL, find_host((gchar *)url));
			proxy_config();
			if (auth_config()!=0) {
				gdk_threads_enter();
				log_new_str(COLOR_RED, "Authentication failed!");
				gdk_threads_leave();
				goto end;
			}
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, normal_progress);
			curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
			curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
			curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
			if (current_settings.showhiddenfiles) {
				if (IS_CURRENT_PROFILE_SFTP) 
					curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "ls -a");
				else
					curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST -a");
			}
			curl_easy_perform(curl);
		}
	}
	
	if (!to_abort) {
		
		if (curl && download_listing) {
			if (current_settings.cache) {
				g_key_file_set_comment(cachekeyfile, NULL, NULL, ofilename, NULL);
				gchar *encoded = g_base64_encode((guchar *)str.ptr, str.len);
				gchar *all = g_strdup_printf("%s\n%s\n%d", lsfrom, str.ptr, unixtime);
				gchar *checksum = g_compute_checksum_for_string(G_CHECKSUM_MD5, all, g_utf8_strlen(all, -1));
				g_key_file_set_integer(cachekeyfile, lsfrom, "_____time", unixtime);
				g_key_file_set_string(cachekeyfile, lsfrom, "_checksum", checksum);
				g_key_file_set_string(cachekeyfile, lsfrom, "__rawdata", encoded);
				gchar *data = g_key_file_to_data(cachekeyfile, NULL, NULL);
				utils_write_file(cache_file, data);
				g_free(data);
				g_free(all);
				g_free(checksum);
				g_free(encoded);
			}
			listing = g_strdup(str.ptr);
		}
		
		clear_children();
		if (to_list(listing, lsfrom)==0) {
			gdk_threads_enter();
			gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(toolbar.connect), GTK_STOCK_DISCONNECT);
			if (auto_scroll) fileview_scroll_to_iter(&parent);
			gdk_threads_leave();
		}
	}
	free(str.ptr);
	end:
	gdk_threads_enter();
	// don't g_free(listing) / g_free(ofilename) / g_key_file_free(cachekeyfile)
	// as it will cause crash after try_another_username_password();
	g_free(lsfrom);
	g_free(lsto);
	ui_progress_bar_stop();
	execute_end(2);
	gdk_threads_leave();
	g_mutex_unlock(mutex);
	g_thread_exit(NULL);
	return NULL;
}

static void *send_command(gpointer p)
{
	gchar *comm = (gchar *)p;
	gchar **comms = g_strsplit(comm, "\n", 0);
	gint i;
	const gchar *url = g_strconcat(current_profile.url, comms[0], NULL);
	struct curl_slist *headers = NULL;
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, find_host((gchar *)url));
		proxy_config();
		auth_config();
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, normal_progress);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftp_log);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_SINGLECWD);
		for (i=1; i<g_strv_length(comms); i++)
			headers = curl_slist_append(headers, comms[i]);
		curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headers);
		curl_easy_perform(curl);
	}
	gdk_threads_enter();
	ui_progress_bar_stop();
	execute_end(3);
	gdk_threads_leave();
	g_strfreev(comms);
	g_free(comm);
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
	} else if(g_strcmp0(gtk_entry_get_text(GTK_ENTRY(retry.login)), "anonymous")==0) {
		gtk_entry_set_text(GTK_ENTRY(retry.login), "");
		gtk_entry_set_text(GTK_ENTRY(retry.password), "");
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
	if (running==FALSE) {
		running = TRUE;
		gtk_widget_set_sensitive(toolbar.abort, TRUE);
		gint type;
		gchar *remote;
		gchar *local;
		if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pending_store), &current_pending)) {
			gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 
			4, &remote, 5, &local, 6, &type, -1);
			switch (type) {
				case 0: {
					struct transfer ul;
					ul.from = g_strdup(local);
					ul.to = g_strdup(remote);
					to_upload_file(&ul);
					break;
				}
				case 1: {
					struct transfer dl;
					dl.from = g_strdup(remote);
					dl.to = g_strdup(local);
					to_download_file(&dl);
					break;
				}
				case 2:
				case 22:
				case 222:
				case 2222: {
					struct transfer ls;
					ls.from = g_strconcat(g_path_get_dirname(remote), "/", NULL);
					ls.to = g_strdup(local);
					if (type==222 || type==2222) ls.cache_enabled = TRUE;
					to_get_dir_listing(&ls);
					break;
				}
				case 3: {
					gchar *comm;
					comm = g_strconcat(remote, "\n", local, NULL);
					to_send_commands(comm);
					break;
				}
				case 55: {
					gchar *cf;
					cf = g_strconcat(remote, NULL);
					to_create_file(cf);
					break;
				}
				default: {
					running = FALSE;
					gtk_widget_set_sensitive(toolbar.abort, FALSE);
				}
			}
			gint i=0,y=0; //Used to stop an unknown error.
			for(i=0; i<100000; i++){y+=1;}
		}
		g_free(remote);
		g_free(local);
	}
}

static gboolean execute_wait(gpointer data)
{
	execute();
	return FALSE;
}

static void execute_end(gint type)
{
	gboolean delete = FALSE;
	gint ptype;
	if (gtk_list_store_iter_is_valid(pending_store, &current_pending)) {
		gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 6, &ptype, -1);
		
		if (type==ptype) delete = TRUE;
		if (type==2 && (ptype==2222 || ptype==222 || ptype==22)) delete = TRUE;
		
		if (delete) {
			gtk_list_store_remove(GTK_LIST_STORE(pending_store), &current_pending);
			gtk_tree_view_columns_autosize(GTK_TREE_VIEW(pending_view));
			if (to_abort) {
				while (gtk_list_store_iter_is_valid(pending_store, &current_pending)) {
					gtk_tree_model_get(GTK_TREE_MODEL(pending_store), &current_pending, 6, &ptype, -1);
					if (ptype && (ptype==22 || ptype==2222)) 
						gtk_list_store_remove(GTK_LIST_STORE(pending_store), &current_pending);
					else
						break;
				}
			}
		}
	}
	running = FALSE;
	gtk_widget_set_sensitive(toolbar.abort, FALSE);
	if (!to_abort && gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pending_store), &current_pending)) {
		g_timeout_add(200, (GSourceFunc)execute_wait, NULL);
	}
}

static void menu_position_func(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer p)
{
	GtkWidget *widget = p;
	GdkWindow *gdk_window;
	GtkAllocation allocation;
	gdk_window = gtk_widget_get_window(widget);
	gdk_window_get_origin(gdk_window, x, y);
	gtk_widget_get_allocation(widget, &allocation);
	*x += allocation.x;
	*y += allocation.y + allocation.height;
	*push_in = FALSE;
}

static void to_use_proxy(GtkMenuItem *menuitem, int p)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
		current_settings.current_proxy = p;
	}
}

static void to_connect(GtkMenuItem *menuitem, int p)
{
	to_abort = FALSE;
	current_profile.cert = "";
	current_profile.index = p;
	load_profiles(2);
	current_profile.url=g_strdup_printf("%s", current_profile.host);
	current_profile.url=g_strstrip(current_profile.url);
	if (!g_regex_match_simple("^s?ftp://", current_profile.url, G_REGEX_CASELESS, 0)) {
		current_profile.url=g_strconcat("ftp://", current_profile.url, NULL);
	}
	GRegex *regex = g_regex_new("^s", G_REGEX_CASELESS, 0, NULL);
	if (g_regex_match(regex, current_profile.url, 0, NULL)) 
		current_profile.url=g_regex_replace(regex, current_profile.url, -1, 0, "", 0, NULL);
	g_regex_unref(regex);
	if (IS_CURRENT_PROFILE_SFTP) 
		current_profile.url=g_strconcat("s", current_profile.url, NULL);
	if (!g_str_has_suffix(current_profile.url, "/")) {
		current_profile.url=g_strconcat(current_profile.url, "/", NULL);
	}
	
	gtk_paned_set_position(GTK_PANED(ui_lookup_widget(geany->main_widgets->window, "vpaned1")), 
		geany->main_widgets->window->allocation.height - 250);
	
	msgwin_clear_tab(MSG_MESSAGE);
	msgwin_switch_tab(MSG_MESSAGE, TRUE);
	log_new_str(COLOR_BLUE, "Initializing...");
	
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
	gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(toolbar.connect), GTK_STOCK_CONNECT);
	if (curl) {
		log_new_str(COLOR_RED, "Disconnected.");
		curl = NULL;
	}
	clear();
	gtk_widget_set_sensitive(toolbar.abort, FALSE);
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
	
	item = gtk_menu_item_new_with_mnemonic("_View on the Web");
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_clicked), (gpointer)999);
	
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
	
	item = gtk_check_menu_item_new_with_mnemonic("_Cache Directory Listings");
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), current_settings.cache);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "toggled", G_CALLBACK(on_menu_item_clicked), (gpointer)100);
	
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

static gchar *local_or_tmp_directory()
{
	load_profiles(2);
	GFile *dir;
	gchar *path;
	gchar *path2;
	path = g_strconcat(tmp_dir, current_profile.login, "@", current_profile.host, NULL);
	if (g_utf8_strlen(current_profile.local, -1)>0) {
		dir = g_file_new_for_path(current_profile.local);
		if (g_file_query_exists(dir, NULL)) {
			path = current_profile.local;
			path2 = current_profile.remote;
			if (g_str_has_suffix(path, "/")) 
				path = g_path_get_dirname(path);
			if (g_str_has_suffix(path2, "/")) 
				path2 = g_path_get_dirname(path2);
			if (!g_str_has_prefix(path2, "/")) 
				path2 = g_strconcat("/", path2, NULL);
			if (g_str_has_suffix(path, path2)) {
				GRegex *regex;
				GMatchInfo *match_info;
				regex = g_regex_new("/", 0, 0, NULL);
				g_regex_match(regex, path2, 0, &match_info);
				while (g_match_info_matches(match_info)) {
					path = g_path_get_dirname(path);
					g_match_info_next(match_info, NULL);
				}
				g_match_info_free(match_info);
				g_regex_unref(regex);
			}
		}
	}
	return path;
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
			gchar *name;
			gchar *host;
			for (i = 0; i < all_profiles_length; i++) {
				name = utils_get_setting_string(profiles, all_profiles[i], "name", "");
				host = utils_get_setting_string(profiles, all_profiles[i], "host", "");
				if (g_utf8_strlen(host, -1)>0) {
					if (g_utf8_strlen(name, -1)==0) name = g_strdup(host);
					gtk_list_store_append(GTK_LIST_STORE(pref.store), &iter);
					gtk_list_store_set(GTK_LIST_STORE(pref.store), &iter, 
					0, name, 
					1, host, 
					2, utils_get_setting_string(profiles, all_profiles[i], "port", "21"), 
					3, utils_get_setting_string(profiles, all_profiles[i], "login", ""), 
					4, decrypt(utils_get_setting_string(profiles, all_profiles[i], "password", "")), 
					5, utils_get_setting_string(profiles, all_profiles[i], "remote", ""), 
					6, utils_get_setting_string(profiles, all_profiles[i], "local", ""), 
					7, utils_get_setting_string(profiles, all_profiles[i], "webhost", ""), 
					8, utils_get_setting_string(profiles, all_profiles[i], "prefix", ""), 
					9, utils_get_setting_string(profiles, all_profiles[i], "auth", ""), 
					10, utils_get_setting_string(profiles, all_profiles[i], "privatekey", ""), 
					-1);
				}
				g_free(name);
				g_free(host);
			}
			break;
		}
		case 2:{
			gint i;
			i = current_profile.index;
			current_profile.name = utils_get_setting_string(profiles, all_profiles[i], "name", "");
			current_profile.host = utils_get_setting_string(profiles, all_profiles[i], "host", "");
			if (g_utf8_strlen(current_profile.name, -1)==0) current_profile.name = g_strdup(current_profile.host);
			current_profile.port = utils_get_setting_string(profiles, all_profiles[i], "port", "");
			current_profile.login = utils_get_setting_string(profiles, all_profiles[i], "login", "");
			current_profile.password = decrypt(utils_get_setting_string(profiles, all_profiles[i], "password", ""));
			current_profile.remote = utils_get_setting_string(profiles, all_profiles[i], "remote", "");
			current_profile.local = utils_get_setting_string(profiles, all_profiles[i], "local", "");
			current_profile.webhost = utils_get_setting_string(profiles, all_profiles[i], "webhost", "");
			current_profile.prefix = utils_get_setting_string(profiles, all_profiles[i], "prefix", "");
			current_profile.auth = utils_get_setting_string(profiles, all_profiles[i], "auth", "");
			current_profile.privatekey = utils_get_setting_string(profiles, all_profiles[i], "privatekey", "");
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
			gchar *name = NULL;
			gchar *host = NULL;
			gchar *port = NULL;
			gchar *login = NULL;
			gchar *password = NULL;
			gchar *remote = NULL;
			gchar *local = NULL;
			gchar *webhost = NULL;
			gchar *prefix = NULL;
			gchar *auth = NULL;
			gchar *privatekey = NULL;
			while (valid) {
				gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
				0, &name, 
				1, &host, 
				2, &port, 
				3, &login, 
				4, &password, 
				5, &remote, 
				6, &local, 
				7, &webhost, 
				8, &prefix, 
				9, &auth, 
				10, &privatekey, 
				-1);
				name = g_strstrip(name);
				host = g_strstrip(host);
				if (g_utf8_strlen(host, -1)>0) {
					if (g_utf8_strlen(name, -1)==0) name = g_strdup(host);
					port = g_strstrip(port);
					login = g_strstrip(login);
					password = encrypt(g_strstrip(password));
					remote = g_strstrip(remote);
					local = g_strstrip(local);
					webhost = g_strstrip(webhost);
					prefix = g_strstrip(prefix);
					auth = g_strstrip(auth);
					privatekey = g_strstrip(privatekey);
					unique_id = g_strconcat(name, "\n", host, "\n", port, "\n", login, "\n", 
					password, "\n", remote, "\n", local, "\n", webhost, "\n", prefix, "\n", auth, "\n", privatekey, NULL);
					unique_id = g_compute_checksum_for_string(G_CHECKSUM_MD5, unique_id, g_utf8_strlen(unique_id, -1));
					g_key_file_set_string(profiles, unique_id, "name", name);
					g_key_file_set_string(profiles, unique_id, "host", host);
					g_key_file_set_string(profiles, unique_id, "port", port);
					g_key_file_set_string(profiles, unique_id, "login", login);
					g_key_file_set_string(profiles, unique_id, "password", password);
					g_key_file_set_string(profiles, unique_id, "remote", remote);
					g_key_file_set_string(profiles, unique_id, "local", local);
					g_key_file_set_string(profiles, unique_id, "webhost", webhost);
					g_key_file_set_string(profiles, unique_id, "prefix", prefix);
					g_key_file_set_string(profiles, unique_id, "auth", auth);
					g_key_file_set_string(profiles, unique_id, "privatekey", privatekey);
					g_free(unique_id);
					g_free(port);
					g_free(login);
					g_free(password);
					g_free(remote);
					g_free(local);
					g_free(webhost);
					g_free(prefix);
					g_free(auth);
					g_free(privatekey);
				}
				g_free(name);
				g_free(host);
				valid = gtk_tree_model_iter_next(model, &iter);
			}
			break;
		}
		case 2: {
			g_key_file_load_from_file(profiles, profiles_file, G_KEY_FILE_NONE, NULL);
			gint i;
			i = current_profile.index;
			g_key_file_set_string(profiles, all_profiles[i], "name", current_profile.name);
			g_key_file_set_string(profiles, all_profiles[i], "host", current_profile.host);
			g_key_file_set_string(profiles, all_profiles[i], "port", current_profile.port);
			g_key_file_set_string(profiles, all_profiles[i], "login", current_profile.login);
			g_key_file_set_string(profiles, all_profiles[i], "password", encrypt(current_profile.password));
			g_key_file_set_string(profiles, all_profiles[i], "remote", current_profile.remote);
			g_key_file_set_string(profiles, all_profiles[i], "local", current_profile.local);
			g_key_file_set_string(profiles, all_profiles[i], "webhost", current_profile.webhost);
			g_key_file_set_string(profiles, all_profiles[i], "prefix", current_profile.prefix);
			g_key_file_set_string(profiles, all_profiles[i], "auth", current_profile.auth);
			g_key_file_set_string(profiles, all_profiles[i], "privatekey", current_profile.privatekey);
			break;
		}
	}
	if (g_mkdir_with_parents(profiles_dir, 0777)!=0) {
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
	
	g_key_file_set_boolean(config, "gFTP-main", "cache", current_settings.cache);
	g_key_file_set_boolean(config, "gFTP-main", "show_hidden_files", current_settings.showhiddenfiles);
	g_key_file_set_boolean(config, "gFTP-main", "auto_nav", current_settings.autonav);
	g_key_file_set_integer(config, "gFTP-main", "proxy", current_settings.current_proxy);
	
	if (pref.proxy_combo) {
		GtkTreeModel *model;
		model = gtk_combo_box_get_model(GTK_COMBO_BOX(pref.proxy_combo));
		if (model) {
			g_key_file_remove_group(config, "gFTP-proxy", NULL);
			GtkTreeIter iter;
			gboolean valid;
			valid = gtk_tree_model_get_iter_from_string(model, &iter, "2");
			gchar *name = NULL;
			gchar *uid = NULL;
			gchar **nameparts;
			while (valid) {
				gtk_tree_model_get(GTK_TREE_MODEL(pref.proxy_store), &iter, 
				0, &name, 
				-1);
				name = g_strstrip(name);
				uid = g_compute_checksum_for_string(G_CHECKSUM_MD5, name, g_utf8_strlen(name, -1));
				uid = g_strconcat("proxy_", uid, NULL);
				nameparts = parse_proxy_string(name);
				if (g_strv_length(nameparts)==4) {
					g_key_file_set_string(config, "gFTP-proxy", uid, name);
				}
				g_strfreev(nameparts);
				g_free(name);
				g_free(uid);
				valid = gtk_tree_model_iter_next(model, &iter);
			}
		}
	}
	if (g_mkdir_with_parents(config_dir, 0777) == 0) {
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
		if (g_regex_match_simple("^(.*)\\s(.*)$", all_hosts[i], 0, 0)) {
			hostsparts = g_strsplit(all_hosts[i], " ", 0);
			if (g_hostname_is_ip_address(hostsparts[1])) 
				g_key_file_set_string(hosts, hostsparts[0], "ip_address", hostsparts[1]);
		}
	}
	g_strfreev(hostsparts);
	if (g_mkdir_with_parents(hosts_dir, 0777)==0) {
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

static void load_settings(gint type)
{
	GKeyFile *settings = g_key_file_new();
	config_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, "gFTP", G_DIR_SEPARATOR_S, "settings.conf", NULL);
	g_key_file_load_from_file(settings, config_file, G_KEY_FILE_NONE, NULL);
	current_settings.cache = utils_get_setting_boolean(settings, "gFTP-main", "cache", FALSE);
	current_settings.showhiddenfiles = utils_get_setting_boolean(settings, "gFTP-main", "show_hidden_files", FALSE);
	current_settings.autonav = utils_get_setting_boolean(settings, "gFTP-main", "auto_nav", FALSE);
	if (current_settings.current_proxy == -1)
		current_settings.current_proxy = utils_get_setting_integer(settings, "gFTP-main", "proxy", 0);
	current_settings.proxy_profiles = g_key_file_get_keys(settings, "gFTP-proxy", NULL, NULL);
	switch (type) {
		case 1:{
			GtkTreeIter iter;
			gsize i;
			gchar *name;
			gchar **nameparts;
			for (i=0; i<g_strv_length(current_settings.proxy_profiles); i++)  {
				name = utils_get_setting_string(settings, "gFTP-proxy", current_settings.proxy_profiles[i], "");
				if (g_utf8_strlen(name, -1)>0) {
					nameparts = parse_proxy_string(name);
					if (g_strv_length(nameparts)==4) {
						gtk_list_store_append(GTK_LIST_STORE(pref.proxy_store), &iter);
						gtk_list_store_set(GTK_LIST_STORE(pref.proxy_store), &iter, 
						0, name, 
						1, nameparts[1], 
						2, nameparts[2], 
						3, nameparts[3], 
						-1);
					}
					g_strfreev(nameparts);
				}
				g_free(name);
			}
			break;
		}
		case 2: {
			gint i;
			for (i=0; i<g_strv_length(current_settings.proxy_profiles); i++) 
				current_settings.proxy_profiles[i] = utils_get_setting_string(settings, "gFTP-proxy", current_settings.proxy_profiles[i], "");
			break;
		}
	}
	g_key_file_free(settings);
	
	load_profiles(0);
	load_hosts();
	tmp_dir = g_strdup_printf("%s/gFTP/",(gchar *)g_get_tmp_dir());
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

static gboolean is_edit_profiles_selected_nth_item(GtkTreeIter *iter, gchar *num)
{
	return gtk_tree_path_compare(gtk_tree_path_new_from_string(gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pref.store), iter)), gtk_tree_path_new_from_string(num))==0;
}

static gboolean is_edit_profiles_selected_nth_item_proxy(GtkTreeIter *iter, gchar *num)
{
	return gtk_tree_path_compare(gtk_tree_path_new_from_string(gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pref.proxy_store), iter)), gtk_tree_path_new_from_string(num))==0;
}

static void is_select_profiles_use_anonymous(GtkTreeIter *iter)
{
	gboolean toggle = FALSE;
	gchar *login = g_strdup_printf("%s", "");
	if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(pref.store), iter, 3, &login, -1);
		if (g_strcmp0(login, "anonymous")==0) {
			toggle = TRUE;
		}
	}
	g_free(login);
	
	g_signal_handler_block(pref.anon, pref.anon_handler_id);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pref.anon), toggle);
	g_signal_handler_unblock(pref.anon, pref.anon_handler_id);
	gtk_widget_set_sensitive(pref.login, !toggle);
	gtk_widget_set_sensitive(pref.passwd, !toggle);
}

static void check_delete_button_sensitive(GtkTreeIter *iter)
{
	gtk_widget_set_sensitive(pref.delete, !is_edit_profiles_selected_nth_item(iter, "0"));
}

static void check_delete_button_sensitive_proxy(GtkTreeIter *iter)
{
	gtk_widget_set_sensitive(pref.proxy_delete, !is_edit_profiles_selected_nth_item_proxy(iter, "0"));
}

static void check_private_key_browse_sensitive()
{
	gboolean privatekey_sensitive = (g_strcmp0(gtk_combo_box_get_active_text(GTK_COMBO_BOX(pref.auth)), "SFTP")==0);
	gtk_widget_set_sensitive(pref.privatekey, privatekey_sensitive);
	gtk_widget_set_sensitive(pref.browsekey, privatekey_sensitive);
}

static int to_auth_type(gchar *type)
{
	if (g_strcmp0(type, "FTP")==0) return 0;
	if (g_strcmp0(type, "SFTP")==0) return 1;
	if (g_strcmp0(type, "TLSv1 (FTPS)")==0) return 2;
	if (g_strcmp0(type, "SSLv3 (FTPS)")==0) return 3;
	return 0;
}

static int to_proxy_type(gchar *type)
{
	if (g_strcmp0(type, "HTTP")==0) return 0;
	if (g_strcmp0(type, "SOCKS4")==0) return 1;
	if (g_strcmp0(type, "SOCKS5")==0) return 2;
	return -1;
}

static gchar **parse_proxy_string(gchar *name)
{
	gchar **results;
	GRegex *regex;
	GMatchInfo *match_info;
	regex = g_regex_new("^(HTTP|SOCKS4|SOCKS5),\\s(.*):(.*)$", 0, 0, NULL);
	g_regex_match(regex, name, 0, &match_info);
	results = g_match_info_fetch_all(match_info);
	g_match_info_free(match_info);
	g_regex_unref(regex);
	return results;
}

static gboolean show_only_profiles(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	return !(is_edit_profiles_selected_nth_item(iter, "0") || is_edit_profiles_selected_nth_item(iter, "1"));
}

static gchar *run_file_chooser(const gchar *title, GtkFileChooserAction action, const gchar *utf8_path)
{ //FROM GEANY SOURCE (ui_utils.c)
	GtkWidget *dialog = gtk_file_chooser_dialog_new(title, GTK_WINDOW(geany->main_widgets->window), action, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
	gchar *locale_path;
	gchar *ret_path = NULL;
	
	gtk_widget_set_name(dialog, "GeanyDialog");
	locale_path = utils_get_locale_from_utf8(utf8_path);
	if (action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) {
		if (g_path_is_absolute(locale_path) && g_file_test(locale_path, G_FILE_TEST_IS_DIR))
			gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), locale_path);
	}
	else if (action == GTK_FILE_CHOOSER_ACTION_OPEN) {
		if (g_path_is_absolute(locale_path))
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), locale_path);
	}
	g_free(locale_path);
	
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		gchar *dir_locale;
		
		dir_locale = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		ret_path = utils_get_utf8_from_locale(dir_locale);
		g_free(dir_locale);
	}
	gtk_widget_destroy(dialog);
	return ret_path;
}

static gchar *return_web_url(gchar *name, gboolean is_dir)
{
	gchar *url;
	gchar *prefix;
	gchar *rel;
	url = g_strdup(current_profile.webhost);
	prefix = g_strdup(current_profile.prefix);
	
	if (g_utf8_strlen(url, -1)==0)
		url = g_strdup(current_profile.url);
	else if (!g_regex_match_simple("^https?://", url, G_REGEX_CASELESS, 0)) 
		url = g_strconcat("http://", url, NULL);
	
	if (!g_str_has_suffix(url, "/")) 
		url = g_strconcat(url, "/", NULL);
	
	rel = g_file_get_relative_path(g_file_new_for_path(prefix), g_file_new_for_path(name));
	if (!rel) rel = g_file_get_relative_path(g_file_new_for_path(prefix), g_file_new_for_path(g_strconcat("/", name, NULL)));
	if (!rel) {
		g_return_val_if_fail(dialogs_show_question("You have an invalid prefix or are viewing outside prefix. You may check your profile settings. Continue with the maybe invalid URL?"), NULL);
		rel = g_strdup(name);
		goto end;
	}
	if (rel && is_dir)
		rel = g_strconcat(rel, "/", NULL);
	end:
	return g_strdup_printf("%s%s", url, rel);
}

static gboolean on_abort_check_aborted(gpointer user_data)
{
	if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pending_store), NULL)==0) {
		log_new_str(COLOR_RED, "Aborted by user.");
		to_abort = FALSE;
		return FALSE;
	}
	return TRUE;
}

static void on_abort_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
	g_return_if_fail(to_abort==FALSE);
	to_abort = TRUE;
	log_new_str(COLOR_RED, "Aborting...");
	g_timeout_add(100, (GSourceFunc)on_abort_check_aborted, NULL);
}

static void on_menu_item_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
	g_return_if_fail(adding==FALSE);
	g_return_if_fail(to_abort==FALSE);
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
					if (IS_CURRENT_PROFILE_SFTP)
						add_pending_item(3, name, g_strdup_printf("mkdir /%s%s", name, cmd));
					else
						add_pending_item(3, name, g_strdup_printf("MKD %s", cmd));
					if (redefine_parent_iter(name, FALSE)) add_pending_item(22, name, NULL);
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
								if (IS_CURRENT_PROFILE_SFTP)
									add_pending_item(3, name, g_strdup_printf("rmdir /%s%s", name, dirname));
								else
									add_pending_item(3, name, g_strdup_printf("RMD %s", dirname));
								if (redefine_parent_iter(name, FALSE)) add_pending_item(22, name, NULL);
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
					if (redefine_parent_iter(name, FALSE)) add_pending_item(22, name, NULL);
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
								if (IS_CURRENT_PROFILE_SFTP)
									add_pending_item(3, name, g_strdup_printf("rm /%s%s", name, dirname));
								else
									add_pending_item(3, name, g_strdup_printf("DELE %s", dirname));
								if (redefine_parent_iter(name, FALSE)) add_pending_item(22, name, NULL);
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
									if (IS_CURRENT_PROFILE_SFTP)
										add_pending_item(3, name, g_strdup_printf("rename /%s%s /%s%s", name, dirname, name, reto));
									else
										add_pending_item(3, name, g_strdup_printf("RNFR %s\nRNTO %s", dirname, reto));
									if (redefine_parent_iter(name, FALSE)) add_pending_item(22, name, NULL);
								}
								g_free(reto);
							}
							g_free(dirname);
						}
					}
				}
				break;
			case 99:
				current_settings.showhiddenfiles = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem));
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				if (!is_folder_selected(list)) {
					name = g_path_get_dirname(name);
				}
				if (g_strcmp0(name, ".")==0) name = "";
				if (redefine_parent_iter(name, FALSE)) add_pending_item(2, name, NULL);
				break;
			case 100:
				current_settings.cache = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem));
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				if (!is_folder_selected(list)) {
					name = g_path_get_dirname(name);
				}
				if (g_strcmp0(name, ".")==0) name = "";
				if (redefine_parent_iter(name, FALSE)) add_pending_item(current_settings.cache?222:2, name, NULL);
				break;
			case 999: {
				load_profiles(2);
				gtk_tree_model_get(GTK_TREE_MODEL(file_store), &iter, FILEVIEW_COLUMN_DIR, &name, -1);
				gchar *url;
				url = return_web_url(name, is_folder_selected(list));
				if (url) {
					GError* error = NULL;
					gchar *argv[] = {"xdg-open", (gchar*) url, NULL};
					g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
				}
				break;
			}
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
			if (!p || current_settings.autonav) add_pending_item(2222, name, NULL);
			if (p) {
				gchar *remote = (gchar *)p;
				gchar **remoteparts = g_strsplit(remote, "/", 0);
				gint i;
				remote = g_strdup("");
				GtkTreeIter iter2;
				for (i=0; i<g_strv_length(remoteparts); i++) {
					if (g_strcmp0(remoteparts[i], "")!=0) {
						remote = g_strconcat(remote, remoteparts[i], "/", NULL);
						if (current_settings.autonav) {
							add_pending_item(2222, remote, remote);
						} else {
							gtk_tree_store_append(file_store, &iter2, &iter);
							gtk_tree_store_set(file_store, &iter2,
							FILEVIEW_COLUMN_ICON, GTK_STOCK_DIRECTORY,
							FILEVIEW_COLUMN_NAME, remoteparts[i],
							FILEVIEW_COLUMN_DIR, remote,
							FILEVIEW_COLUMN_INFO, g_strdup_printf("/%s", remote), 
							-1);
							gtk_tree_model_get_iter(model, &iter, gtk_tree_model_get_path(model, &iter2));
							gtk_tree_model_get_iter(model, &parent, gtk_tree_model_get_path(model, &iter));
						}
					}
				}
				if (!current_settings.autonav) {
					gtk_tree_model_get_iter_first(GTK_TREE_MODEL(file_store), &iter);
					gtk_tree_view_expand_row(GTK_TREE_VIEW(file_view), gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), &iter), TRUE);
					treepath = gtk_tree_model_get_path(GTK_TREE_MODEL(file_store), &parent);
					gtk_tree_view_set_cursor(GTK_TREE_VIEW(file_view), treepath, NULL, FALSE);
					add_pending_item(2222, remote, NULL);
				}
			}
		} else {
			gchar *filepath;
			filepath = local_or_tmp_directory();
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
		if (all_profiles_length>0) {
			GtkWidget *item, *menu;
			menu = gtk_menu_new();
			gsize i;
			for (i = 0; i < all_profiles_length; i++) {
				if (to_auth_type(load_profile_property(i, "auth"))>0) {
					item = gtk_image_menu_item_new_from_stock(GTK_STOCK_DIALOG_AUTHENTICATION, NULL);
					gtk_menu_item_set_label(GTK_MENU_ITEM(item), load_profile_property(i, "name"));
					gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
				} else {
					item = gtk_menu_item_new_with_label(load_profile_property(i, "name"));
				}
				gtk_widget_show(item);
				gtk_container_add(GTK_CONTAINER(menu), item);
				g_signal_connect(item, "activate", G_CALLBACK(to_connect), (gpointer)i);
			}
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_func, toolbar.connect, 0, GDK_CURRENT_TIME);
		} else {
			on_edit_preferences(NULL, 1);
		}
	} else {
		disconnect(NULL);
	}
}

static void on_proxy_profiles_clicked(gpointer p)
{
	load_settings(2);
	GSList *group = NULL;
	gint length = g_strv_length(current_settings.proxy_profiles);
	gint i;
	if (length>0) {
		GtkWidget *item, *menu;
		menu = gtk_menu_new();
		item = gtk_radio_menu_item_new_with_label(group, "No Proxy");
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), (current_settings.current_proxy==0));
		g_signal_connect(item, "activate", G_CALLBACK(to_use_proxy), (gpointer)0);
		for (i = 0; i < length; i++) {
			group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
			item = gtk_radio_menu_item_new_with_label(group, current_settings.proxy_profiles[i]);
			gtk_widget_show(item);
			gtk_container_add(GTK_CONTAINER(menu), item);
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), (current_settings.current_proxy==i+1));
			g_signal_connect(item, "activate", G_CALLBACK(to_use_proxy), (gpointer)i+1);
		}
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc)menu_position_func, toolbar.proxy, 0, GDK_CURRENT_TIME);
	} else {
		on_edit_preferences(NULL, 2);
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

static void on_edit_preferences(GtkToolButton *toolbutton, gint page_num)
{
	if (page_num>0) config_page_number = page_num - 1;
	plugin_show_configure(geany_plugin);
}

static void *on_host_login_password_changed(GtkWidget *widget, GdkEventKey *event, GtkDialog *dialog)
{
	if (dialog && (event->keyval == 0xff0d || event->keyval == 0xff8d)) { //RETURN AND KEY PAD ENTER
		gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
		return FALSE;
	}
	gchar *name;
	gchar *host;
	name = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref.name)));
	host = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref.host)));
	if (g_object_get_data(G_OBJECT(pref.name), "name-edited")==FALSE) {
		gtk_entry_set_text(GTK_ENTRY(pref.name), host);
		name = g_strdup(host);
	}
	if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &pref.iter_store_new) || is_edit_profiles_selected_nth_item(&pref.iter_store_new, "0")) {
		gtk_list_store_append(GTK_LIST_STORE(pref.store), &pref.iter_store_new);
	}
	gtk_list_store_set(GTK_LIST_STORE(pref.store), &pref.iter_store_new, 
	0, name, 
	1, host, 
	2, gtk_entry_get_text(GTK_ENTRY(pref.port)), 
	3, gtk_entry_get_text(GTK_ENTRY(pref.login)), 
	4, gtk_entry_get_text(GTK_ENTRY(pref.passwd)), 
	5, gtk_entry_get_text(GTK_ENTRY(pref.remote)), 
	6, gtk_entry_get_text(GTK_ENTRY(pref.local)), 
	7, gtk_entry_get_text(GTK_ENTRY(pref.webhost)), 
	8, gtk_entry_get_text(GTK_ENTRY(pref.prefix)), 
	9, gtk_combo_box_get_active_text(GTK_COMBO_BOX(pref.auth)), 
	10, gtk_entry_get_text(GTK_ENTRY(pref.privatekey)), 
	-1);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &pref.iter_store_new);
	return FALSE;
}

static void *on_proxy_profile_entry_changed(GtkWidget *widget, GdkEventKey *event, GtkDialog *dialog)
{
	gchar *type = g_strdup(gtk_combo_box_get_active_text(GTK_COMBO_BOX(pref.proxy_type)));
	gchar *host = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref.proxy_host)));
	gchar *port = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref.proxy_port)));
	gchar *name = g_strdup(type);
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(pref.proxy_type))>-1) {
		if (g_utf8_strlen(host, -1)>0) name = g_strconcat(name, ", ", host, NULL);
		if (g_utf8_strlen(port, -1)>0) name = g_strconcat(name, ":", port, NULL);
		if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.proxy_store), &pref.proxy_iter_store_new) || is_edit_profiles_selected_nth_item_proxy(&pref.proxy_iter_store_new, "0")) {
			gtk_list_store_append(GTK_LIST_STORE(pref.proxy_store), &pref.proxy_iter_store_new);
		}
		gtk_list_store_set(GTK_LIST_STORE(pref.proxy_store), &pref.proxy_iter_store_new, 
		0, name, 
		1, type, 
		2, host, 
		3, port, 
		-1);
	}
	g_free(name);
	g_free(type);
	g_free(host);
	g_free(port);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.proxy_combo), &pref.proxy_iter_store_new);
	return FALSE;
}

static void *on_name_edited(GtkWidget *widget, GdkEventKey *event, GtkDialog *dialog)
{
	if (dialog && (event->keyval == 0xff0d || event->keyval == 0xff8d)) { //RETURN AND KEY PAD ENTER
		gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
		return FALSE;
	}
	g_object_set_data(G_OBJECT(pref.name), "name-edited", (gpointer)(g_strcmp0(gtk_entry_get_text(GTK_ENTRY(pref.name)), "")!=0));
	return on_host_login_password_changed(NULL, NULL, NULL);
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

static void on_browse_local_clicked(GtkButton *button, gpointer user_data)
{
	gchar *path;
	path = g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(pref.local)));
	path = run_file_chooser("Browse local directory", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, path);
	gtk_entry_set_text(GTK_ENTRY(pref.local), path);
	on_host_login_password_changed(NULL, NULL, NULL);
	g_free(path);
}

static void on_browse_private_key_clicked(GtkButton *button, gpointer user_data)
{
	gchar *path;
	path = g_strdup_printf("%s", gtk_entry_get_text(GTK_ENTRY(pref.privatekey)));
	path = run_file_chooser("Browse private key", GTK_FILE_CHOOSER_ACTION_OPEN, path);
	gtk_entry_set_text(GTK_ENTRY(pref.privatekey), path);
	on_host_login_password_changed(NULL, NULL, NULL);
	g_free(path);
}

static void on_use_anonymous_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gboolean toggle = gtk_toggle_button_get_active(togglebutton);
	gtk_widget_set_sensitive(pref.login, !toggle);
	gtk_widget_set_sensitive(pref.passwd, !toggle);
	if (toggle) {
		gtk_entry_set_text(GTK_ENTRY(pref.login), "anonymous");
		gtk_entry_set_text(GTK_ENTRY(pref.passwd), "ftp@example.com");
	} else if(g_strcmp0(gtk_entry_get_text(GTK_ENTRY(pref.login)), "anonymous")==0) {
		gtk_entry_set_text(GTK_ENTRY(pref.login), "");
		gtk_entry_set_text(GTK_ENTRY(pref.passwd), "");
	}
	on_host_login_password_changed(NULL, NULL, NULL);
}

static void on_show_password_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	gtk_entry_set_visibility(GTK_ENTRY(pref.passwd), gtk_toggle_button_get_active(togglebutton));
}

static void on_auth_changed(void)
{
	on_host_login_password_changed(NULL, NULL, NULL);
	check_private_key_browse_sensitive();
}

static void on_edit_profiles_changed(void)
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &pref.iter_store_new);
	gchar *name = g_strdup_printf("%s", "");
	gchar *host = g_strdup_printf("%s", "");
	gchar *port = g_strdup_printf("%s", "21");
	gchar *login = g_strdup_printf("%s", "");
	gchar *password = g_strdup_printf("%s", "");
	gchar *remote = g_strdup_printf("%s", "");
	gchar *local = g_strdup_printf("%s", "");
	gchar *webhost = g_strdup_printf("%s", "");
	gchar *prefix = g_strdup_printf("%s", "");
	gchar *auth = g_strdup_printf("%s", "");
	gchar *privatekey = g_strdup_printf("%s", "");
	if (is_edit_profiles_selected_nth_item(&iter, "0")) {
		g_object_set_data(G_OBJECT(pref.name), "name-edited", (gpointer)FALSE);
		focus_widget(pref.host);
	} else {
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &iter)) {
			gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 
			0, &name, 
			1, &host, 
			2, &port, 
			3, &login, 
			4, &password, 
			5, &remote, 
			6, &local, 
			7, &webhost, 
			8, &prefix, 
			9, &auth, 
			10, &privatekey, 
			-1);
		}
		if (g_strcmp0(name, host)!=0)
		g_object_set_data(G_OBJECT(pref.name), "name-edited", (gpointer)TRUE);
	}
	gtk_entry_set_text(GTK_ENTRY(pref.name), name);
	gtk_entry_set_text(GTK_ENTRY(pref.host), host);
	gtk_entry_set_text(GTK_ENTRY(pref.port), port);
	gtk_entry_set_text(GTK_ENTRY(pref.login), login);
	gtk_entry_set_text(GTK_ENTRY(pref.passwd), password);
	gtk_entry_set_text(GTK_ENTRY(pref.remote), remote);
	gtk_entry_set_text(GTK_ENTRY(pref.local), local);
	gtk_entry_set_text(GTK_ENTRY(pref.webhost), webhost);
	gtk_entry_set_text(GTK_ENTRY(pref.prefix), prefix);
	g_signal_handler_block(pref.auth, pref.auth_handler_id);
	gtk_combo_box_set_active(GTK_COMBO_BOX(pref.auth), to_auth_type(auth));
	check_private_key_browse_sensitive();
	g_signal_handler_unblock(pref.auth, pref.auth_handler_id);
	gtk_entry_set_text(GTK_ENTRY(pref.privatekey), privatekey);
	g_free(name);
	g_free(host);
	g_free(port);
	g_free(login);
	g_free(password);
	g_free(remote);
	g_free(local);
	g_free(webhost);
	g_free(prefix);
	g_free(auth);
	g_free(privatekey);
	
	is_select_profiles_use_anonymous(&iter);
	check_delete_button_sensitive(&iter);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pref.showpass), FALSE);
}

static void on_edit_proxy_profiles_changed(void)
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.proxy_combo), &iter);
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.proxy_combo), &pref.proxy_iter_store_new);
	gchar *type = g_strdup_printf("%s", "");
	gchar *host = g_strdup_printf("%s", "");
	gchar *port = g_strdup_printf("%s", "");
	if (is_edit_profiles_selected_nth_item_proxy(&iter, "0")) {
		focus_widget(pref.proxy_host);
		gtk_combo_box_set_active(GTK_COMBO_BOX(pref.proxy_type), -1);
	} else {
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.proxy_store), &iter)) {
			gtk_tree_model_get(GTK_TREE_MODEL(pref.proxy_store), &iter, 
			1, &type, 
			2, &host, 
			3, &port, 
			-1);
		}
	}
	g_signal_handler_block(pref.proxy_type, pref.type_handler_id);
	gtk_combo_box_set_active(GTK_COMBO_BOX(pref.proxy_type), to_proxy_type(type));
	g_signal_handler_unblock(pref.proxy_type, pref.type_handler_id);
	gtk_entry_set_text(GTK_ENTRY(pref.proxy_host), host);
	gtk_entry_set_text(GTK_ENTRY(pref.proxy_port), port);
	g_free(type);
	g_free(host);
	g_free(port);
	
	check_delete_button_sensitive_proxy(&iter);
}

static gchar* new_names(gchar *name)
{
	gchar *oname = g_strdup(name);
	oname = g_strstrip(oname);
	
	GRegex *regex;
	GMatchInfo *match_info;
	regex = g_regex_new("^(.*)\\s\\(\\d+\\)$", 0, 0, NULL);
	g_regex_match(regex, oname, 0, &match_info);
	if (g_match_info_matches(match_info)) {
		oname = g_match_info_fetch(match_info, 1);
	}
	g_match_info_free(match_info);
	g_regex_unref(regex);
	
	gchar *names = NULL;
	GSList *nums = NULL;
	gint num;
	for (num=100; num>0; num--)
		nums = g_slist_prepend(nums, (gpointer)num);
	
	gchar *pname;
	pname = g_regex_escape_string(oname, -1);
	pname = g_strdup_printf("^%s\\s\\((\\d+)\\)$", pname);
	regex = g_regex_new(pname, 0, 0, NULL);
	
	GtkTreeIter iter;
	gboolean valid = gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter, "2");
	
	while (valid) {
		gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 0, &names, -1);
		names = g_strstrip(names);
		g_regex_match(regex, names, 0, &match_info);
		if (g_match_info_matches(match_info)) {
			num = g_ascii_strtoll(g_match_info_fetch(match_info, 1), NULL, 0);
			nums = g_slist_remove_all(nums, (gpointer)num);
		}
		g_free(names);
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(pref.store), &iter);
	}
	
	for (num=1; num<g_slist_length(nums); num++) {
		oname = g_strdup_printf("%s (%d)", oname, (gint)g_slist_nth_data(nums, num));
		break;
	}
	g_match_info_free(match_info);
	g_regex_unref(regex);
	g_slist_free(nums);
	g_free(pname);
	return oname;
}

static void on_profile_organize_clicked(GtkToolButton *toolbutton, gpointer p)
{
	GtkTreePath *path;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(pref.p_view), &path, NULL);
	g_return_if_fail(path!=NULL);
	gtk_tree_path_next(path);
	gtk_tree_path_next(path);
	GtkTreeIter iter;
	GtkTreeIter iter2;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter, gtk_tree_path_to_string(path));
	switch ((int)p) {
		case 1:
			gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter2, "2");
			gtk_list_store_move_before(GTK_LIST_STORE(pref.store), &iter, &iter2);
			break;
		case 2:
			if (g_strcmp0(gtk_tree_path_to_string(path),"2")!=0 && gtk_tree_path_prev(path)) {
				gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter2, gtk_tree_path_to_string(path));
				gtk_list_store_move_before(GTK_LIST_STORE(pref.store), &iter, &iter2);
			}
			break;
		case 3:
			gtk_tree_path_next(path);
			gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter2, gtk_tree_path_to_string(path));
			gtk_list_store_move_after(GTK_LIST_STORE(pref.store), &iter, &iter2);
			break;
		case 4:
			gtk_list_store_move_before(GTK_LIST_STORE(pref.store), &iter, NULL);
			break;
		case 5: {
				GtkTreeIter iter_new;
				gtk_list_store_insert_after(pref.store, &iter_new, &iter);
				gchar *name;
				gtk_tree_model_get(GTK_TREE_MODEL(pref.store), &iter, 0, &name, -1);
				gtk_list_store_set(pref.store, &iter_new, 0, new_names(name), -1);
				g_free(name);
				gint i;
				GValue val = {0};
				for (i=1; i<gtk_tree_model_get_n_columns(GTK_TREE_MODEL(pref.store)); i++) {
					g_value_init(&val, gtk_tree_model_get_column_type(GTK_TREE_MODEL(pref.store), i+1));
					gtk_tree_model_get_value(GTK_TREE_MODEL(pref.store), &iter, i, &val);
					gtk_list_store_set_value(pref.store, &iter_new, i, &val);
					g_value_unset(&val);
				}
				gtk_tree_path_prev(path); //because first 2 rows are hidden in p_view
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(pref.p_view), path, NULL, FALSE);
			}
			break;
		case 6:
			gtk_list_store_remove(GTK_LIST_STORE(pref.store), &iter);
			int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pref.store), NULL) - 2;
			if (n<=0) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(pref.combo), 0);
			} else {
				gtk_tree_path_prev(path);
				gtk_tree_path_prev(path);
				gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(pref.store), &iter, gtk_tree_path_to_string(path));
				gtk_tree_model_iter_next(GTK_TREE_MODEL(pref.store), &iter);
				if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(pref.store), &iter))
					path = gtk_tree_path_new_from_string(g_strdup_printf("%d", n-1));
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(pref.p_view), path, NULL, FALSE);
				
			}
			break;
		case 99:
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
			on_edit_profiles_changed();
			break;
	}
}

static void on_organize_profile_clicked(GtkButton *button, gpointer p)
{
	GtkWidget *dialog, *vbox, *widget, *tool_bar;
	dialog = gtk_dialog_new_with_buttons("Organize Profiles", GTK_WINDOW(p),
		GTK_DIALOG_DESTROY_WITH_PARENT, 
		GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, 
		NULL);
	vbox = ui_dialog_vbox_new(GTK_DIALOG(dialog));
	GdkPixbuf *pb = gtk_window_get_icon(GTK_WINDOW(geany->main_widgets->window));
	gtk_window_set_icon(GTK_WINDOW(dialog), pb);
	
	gtk_box_set_spacing(GTK_BOX(vbox), 6);
	
	tool_bar = gtk_toolbar_new();
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(tool_bar), GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_toolbar_set_style(GTK_TOOLBAR(tool_bar), GTK_TOOLBAR_ICONS);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GOTO_TOP));
	gtk_widget_set_tooltip_text(widget, "Move selected profile to the top.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)1);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP));
	gtk_widget_set_tooltip_text(widget, "Move selected profile up.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)2);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GO_DOWN));
	gtk_widget_set_tooltip_text(widget, "Move selected profile down.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)3);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GOTO_BOTTOM));
	gtk_widget_set_tooltip_text(widget, "Move selected profile to the bottom.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)4);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_COPY));
	gtk_widget_set_tooltip_text(widget, "Make a copy of selected profile.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)5);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_DELETE));
	gtk_widget_set_tooltip_text(widget, "Delete selected profile.");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_profile_organize_clicked), (gpointer)6);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	gtk_box_pack_start(GTK_BOX(vbox), tool_bar, FALSE, FALSE, 0);
	
	GtkTreeModel *filter;
	filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(pref.store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter), (GtkTreeModelFilterVisibleFunc)show_only_profiles, NULL, NULL);
	widget = gtk_tree_view_new_with_model(GTK_TREE_MODEL(filter));
	
	GtkCellRenderer *text_renderer;
	GtkTreeViewColumn *column;
	column = gtk_tree_view_column_new();
	text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(widget), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(widget), FALSE);
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(widget), 0);
	
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
	GtkTreePath *path;
	path = gtk_tree_path_new_from_string(gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(pref.store), &iter));
	if (gtk_tree_path_prev(path))
		if (gtk_tree_path_prev(path))
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
	g_signal_connect(widget, "cursor-changed", G_CALLBACK(on_profile_organize_clicked), (gpointer)99);
	pref.p_view = widget;
	
	widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(widget), pref.p_view);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(widget), pref.p_view);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(widget), GTK_SHADOW_IN);
	gtk_widget_set_size_request(widget, 250, 300);
	
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	
	gtk_widget_show_all(dialog);
	
	gint w, x, y;
	gtk_window_get_position(GTK_WINDOW(p), &x, &y);
	gtk_window_get_size(GTK_WINDOW(dialog), &w, NULL);
	gtk_window_move(GTK_WINDOW(dialog), x-w, y);
	
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void on_delete_profile_clicked()
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
	if (!is_edit_profiles_selected_nth_item(&iter, "0")) {
		gtk_list_store_remove(GTK_LIST_STORE(pref.store), &iter);
		if (gtk_list_store_iter_is_valid(GTK_LIST_STORE(pref.store), &iter)) {
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(pref.combo), &iter);
		} else {
			int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pref.store), NULL);
			if (n==2) n=1;
			gtk_combo_box_set_active(GTK_COMBO_BOX(pref.combo), n-1);
		}
	}
}

static void on_delete_proxy_profile_clicked()
{
	GtkTreeIter iter;
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(pref.proxy_combo), &iter);
	if (!is_edit_profiles_selected_nth_item_proxy(&iter, "0")) {
		gtk_list_store_remove(GTK_LIST_STORE(pref.proxy_store), &iter);
		int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(pref.proxy_store), NULL);
		if (n==2) n=1;
		gtk_combo_box_set_active(GTK_COMBO_BOX(pref.proxy_combo), n-1);
	}
}

static void on_document_save()
{
	if (curl) {
		gchar *filepath;
		gchar *putdir;
		gchar *dst;
		filepath = document_get_current()->real_path;
		putdir = local_or_tmp_directory();
		putdir = g_strconcat(putdir, "/", NULL);
		dst = g_file_get_relative_path(g_file_new_for_path(putdir), g_file_new_for_path(filepath));
		if (dst) {
			add_pending_item(0, dst, filepath);
			if (redefine_parent_iter(dst, FALSE)) add_pending_item(22, dst, NULL);
		}
		g_free(putdir);
	}
}

static void on_configure_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	config_page_number = gtk_notebook_get_current_page(GTK_NOTEBOOK(pref.notebook));
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
		current_settings.autonav = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref.autonav));
		
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
		add_pending_item(22, dst, NULL);
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

static gboolean profiles_treeview_row_is_separator_proxy(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	return is_edit_profiles_selected_nth_item_proxy(iter, "1");
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
	
	pending_store = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
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

static gboolean focus_widget(gpointer p)
{
	GtkWidget *widget = (GtkWidget *)p;
	gtk_widget_grab_focus(widget);
	return !gtk_widget_has_focus(widget);
}

static GtkWidget *make_toolbar(void)
{
	GtkWidget *widget, *tool_bar;
	
	tool_bar = gtk_toolbar_new();
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(tool_bar), GTK_ICON_SIZE_MENU);
	gtk_toolbar_set_style(GTK_TOOLBAR(tool_bar), GTK_TOOLBAR_ICONS);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_CONNECT));
	gtk_widget_set_tooltip_text(widget, "Connect / Disconnect");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_connect_clicked), NULL);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	toolbar.connect = widget;
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_STOP));
	gtk_widget_set_sensitive(widget, FALSE);
	gtk_widget_set_tooltip_text(widget, "Abort");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_abort_clicked), NULL);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	toolbar.abort = widget;
	
	widget = GTK_WIDGET(gtk_separator_tool_item_new());
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_NETWORK));
	gtk_widget_set_tooltip_text(widget, "Proxy profiles");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_proxy_profiles_clicked), NULL);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	toolbar.proxy = widget;
	
	widget = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_PREFERENCES));
	gtk_widget_set_tooltip_text(widget, "Preferences");
	g_signal_connect(widget, "clicked", G_CALLBACK(on_edit_preferences), (gpointer)0);
	gtk_container_add(GTK_CONTAINER(tool_bar), widget);
	toolbar.preference = widget;
	
	return tool_bar;
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
	
	mutex = g_mutex_new();
	
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
	
	current_settings.current_proxy = -1;
	load_settings(2);
	
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
	store = gtk_list_store_new(11, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	GtkTreeIter iter;
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "New profile...", -1);
	widget = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
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
	
	table = gtk_table_new(12, 4, FALSE);
	
	gtk_widget_set_size_request(widget, 250, -1);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 2, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_button_new_with_mnemonic("_Delete");
	gtk_widget_set_tooltip_text(widget, "Delete current profile.");
	gtk_widget_set_size_request(widget, 90, -1);
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_delete_profile_clicked), NULL);
	pref.delete = widget;
	check_delete_button_sensitive(NULL);
	
	widget = gtk_label_new("Name");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	g_object_set_data(G_OBJECT(widget), "name-edited", (gpointer)FALSE);
	gtk_widget_set_tooltip_text(widget, "Profile name for display.");
	gtk_entry_set_max_length(GTK_ENTRY(widget), 255);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_name_edited), dialog);
	pref.name = widget;
	widget = gtk_button_new_with_mnemonic("_Organize...");
	gtk_widget_set_tooltip_text(widget, "Organize profiles.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_organize_profile_clicked), dialog);
	
	widget = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), widget, 0, 4, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 4);
	
	widget = gtk_label_new("Host *");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "FTP hostname. (required)");
	gtk_entry_set_max_length(GTK_ENTRY(widget), 255);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.host = widget;
	
	widget = gtk_label_new("Port");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Default FTP port number is 21.\nDefault SFTP port number is 22.");
	gtk_widget_set_size_request(widget, 40, -1);
	gtk_entry_set_max_length(GTK_ENTRY(widget), 5);
	gtk_entry_set_text(GTK_ENTRY(widget), "21");
	gtk_table_attach(GTK_TABLE(table), widget, 3, 4, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.port = widget;
	
	widget = gtk_label_new("Login");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Username for FTP login. Can be left blank.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.login = widget;
	widget = gtk_check_button_new_with_label("Anonymous");
	gtk_widget_set_tooltip_text(widget, "Use anonymous username and password.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 4, 5, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	pref.anon_handler_id = g_signal_connect(widget, "toggled", G_CALLBACK(on_use_anonymous_toggled), NULL);
	pref.anon = widget;
	
	widget = gtk_label_new("Password");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 5, 6, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Password for FTP login. Can be left blank.");
	gtk_entry_set_visibility(GTK_ENTRY(widget), FALSE);
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 5, 6, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.passwd = widget;
	widget = gtk_check_button_new_with_label("Show");
	gtk_widget_set_tooltip_text(widget, "Show password.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 5, 6, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "toggled", G_CALLBACK(on_show_password_toggled), NULL);
	pref.showpass = widget;
	
	widget = gtk_combo_box_new_text();
	gtk_widget_set_tooltip_text(widget, "Security authentication mode. Don't forget to change the port number.");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "FTP");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "SFTP");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "TLSv1 (FTPS)");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "SSLv3 (FTPS)");
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
	gtk_widget_set_size_request(widget, 50, -1);
	pref.auth_handler_id = g_signal_connect(widget, "changed", G_CALLBACK(on_auth_changed), dialog);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 6, 7, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	pref.auth = widget;
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Private key file location. You may also put the public key file (ending in .pub) in the same folder. Leave this blank if you want to use the default private key (~/.ssh/id_rsa). Cannot generate or understand Putty's .ppk file right now.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 6, 7, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.privatekey = widget;
	widget = gtk_button_new_with_mnemonic("_Browse...");
	gtk_widget_set_tooltip_text(widget, "Browse the private key.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 6, 7, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_browse_private_key_clicked), NULL);
	pref.browsekey = widget;
	check_private_key_browse_sensitive();
	
	widget = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), widget, 0, 4, 7, 8, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 4);
	
	widget = gtk_label_new("Remote");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 8, 9, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Initial remote directory.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 8, 9, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.remote = widget;
	widget = gtk_button_new_with_mnemonic("_Use Current");
	if (!curl) gtk_widget_set_sensitive(widget, FALSE);
	gtk_widget_set_tooltip_text(widget, "Use location of currently selected item as initial remote directory.");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 8, 9, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_use_current_clicked), NULL);
	pref.usecurrent = widget;
	
	widget = gtk_label_new("Local");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 9, 10, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Initial local directory. Leave this blank if you want to use temporarily directory.");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 9, 10, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.local = widget;
	widget = gtk_button_new_with_mnemonic("_Browse");
	gtk_table_attach(GTK_TABLE(table), widget, 2, 4, 9, 10, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_browse_local_clicked), NULL);
	
	widget = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), widget, 0, 4, 10, 11, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 4);
	
	widget = gtk_label_new("Web Host");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 11, 12, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Corresponding web host for your FTP account. (e.g. http://www.example.com/)");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 4, 11, 12, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.webhost = widget;
	
	widget = gtk_label_new("Prefix");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 12, 13, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_tooltip_text(widget, "Prefix to remove from FTP path. (e.g. /public_html)");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 4, 12, 13, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_host_login_password_changed), dialog);
	pref.prefix = widget;
	
	hbox = gtk_hbox_new(FALSE, 6);
	widget = gtk_image_new_from_stock(GTK_STOCK_DND_MULTIPLE, GTK_ICON_SIZE_MENU);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	widget = gtk_label_new("Profiles");
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	gtk_widget_show_all(hbox);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table, hbox);
	
	table = gtk_table_new(4, 5, FALSE);
	
	store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "New proxy profile...", -1);
	widget = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	gtk_list_store_append(store, &iter); //for separator
	pref.proxy_store = store;
	pref.proxy_combo = widget;
	g_object_unref(G_OBJECT(store));
	
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widget), renderer, TRUE);
	gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(widget), renderer, "text", 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
	gtk_widget_set_tooltip_text(widget, "Choose a proxy profile to edit or choose New proxy profile to create one.");
	gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(widget), (GtkTreeViewRowSeparatorFunc)profiles_treeview_row_is_separator_proxy, NULL, NULL);
	g_signal_connect(widget, "changed", G_CALLBACK(on_edit_proxy_profiles_changed), NULL);
	gtk_widget_set_size_request(widget, 250, -1);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 3, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	
	widget = gtk_button_new_with_mnemonic("_Delete");
	gtk_widget_set_tooltip_text(widget, "Delete current proxy profile.");
	gtk_widget_set_size_request(widget, 90, -1);
	gtk_table_attach(GTK_TABLE(table), widget, 3, 5, 0, 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "clicked", G_CALLBACK(on_delete_proxy_profile_clicked), NULL);
	pref.proxy_delete = widget;
	check_delete_button_sensitive_proxy(NULL);
	
	widget = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "HTTP");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "SOCKS4");
	gtk_combo_box_append_text(GTK_COMBO_BOX(widget), "SOCKS5");
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 1, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	pref.type_handler_id = g_signal_connect(widget, "changed", G_CALLBACK(on_proxy_profile_entry_changed), dialog);
	pref.proxy_type = widget;
	
	widget = gtk_label_new("Host");
	gtk_table_attach(GTK_TABLE(table), widget, 1, 2, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_size_request(widget, 100, -1);
	gtk_entry_set_max_length(GTK_ENTRY(widget), 255);
	gtk_table_attach(GTK_TABLE(table), widget, 2, 3, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_proxy_profile_entry_changed), dialog);
	pref.proxy_host = widget;
	
	widget = gtk_label_new("Port");
	gtk_misc_set_alignment(GTK_MISC(widget), 1, 0.5);
	gtk_table_attach(GTK_TABLE(table), widget, 3, 4, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	widget = gtk_entry_new();
	gtk_widget_set_size_request(widget, 40, -1);
	gtk_entry_set_max_length(GTK_ENTRY(widget), 5);
	gtk_table_attach(GTK_TABLE(table), widget, 4, 5, 1, 2, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	g_signal_connect(widget, "key-release-event", G_CALLBACK(on_proxy_profile_entry_changed), dialog);
	pref.proxy_port = widget;
	
	widget = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), widget, 0, 5, 2, 3, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 4);
	
	widget = gtk_check_button_new_with_label("Auto-Navigate to Initial Remote Directory");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), current_settings.autonav);
	gtk_table_attach(GTK_TABLE(table), widget, 0, 5, 3, 4, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 2, 2);
	pref.autonav = widget;
	
	load_settings(1);
	
	hbox = gtk_hbox_new(FALSE, 6);
	widget = gtk_label_new("Proxy");
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	gtk_widget_show_all(hbox);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table, hbox);
	
	gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);
	pref.notebook = notebook;
	
	gtk_widget_show_all(vbox);
	
	g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);
	
	if (curl) 
		select_profile(current_profile.index);
	
	switch (config_page_number) {
		case 0:
			if (!curl) g_timeout_add(100, (GSourceFunc)focus_widget, pref.host);
			break;
		case 1:
			if (!curl) g_timeout_add(100, (GSourceFunc)focus_widget, pref.proxy_host);
			break;
	}
	gtk_notebook_set_current_page(GTK_NOTEBOOK(pref.notebook), config_page_number);
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
