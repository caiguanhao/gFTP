#ifndef gFTP_PLUGIN_H
#define gFTP_PLUGIN_H

enum
{
	KB_CREATE_BLANK_FILE,
	KB_COUNT
};

enum
{
	FILEVIEW_COLUMN_ICON = 0,
	FILEVIEW_COLUMN_NAME,
	FILEVIEW_COLUMN_DIR,
	FILEVIEW_COLUMN_INFO,
	FILEVIEW_N_COLUMNS
};

static GtkWidget *box, *fileview_scroll;
static GtkWidget *file_view, *pending_view;
static GtkWidget *btn_connect;
static GtkTreeStore *file_store;
static GtkListStore *pending_store;
static GtkTreeIter parent, current_pending;
static CURL *curl;
static gchar **all_profiles, **all_hosts;
static gsize all_profiles_length;
static gchar *profiles_file, *hosts_file, *tmp_dir;
static gboolean running = FALSE;
static gboolean to_abort = FALSE;
static gint page_number = 0;

#ifndef BASE64ENCODE_TIMES
#define BASE64ENCODE_TIMES 8
#endif

GList *filelist;
GList *dirlist;

struct string
{
	char *ptr;
	int len;
};

struct transfer
{
	char *from;
	char *to;
};

struct uploadf {
	FILE *fp;
	int transfered;
	int filesize;
};

struct commands {
	char *name;
	struct curl_slist *list;
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
	GtkWidget *remote;
	GtkWidget *usecurrent;
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
	char *remote;
} current_profile;

static void add_pending_item(gint type, gchar *n1, gchar *n2);
static void save_profiles(gint type);
static void save_hosts();
static void execute();
static void execute_end(gint type);
static void *disconnect(gpointer p);
static void *to_get_dir_listing(gpointer p);
static void load_profiles(gint type);
static gchar *load_profile_property(gint index, gchar *field);
static void on_edit_profiles_changed(void);
static void on_open_clicked(GtkMenuItem *menuitem, gpointer p);
static void on_menu_item_clicked(GtkMenuItem *menuitem, gpointer user_data);
static gboolean on_retry_entry_keypress(GtkWidget *widget, GdkEventKey *event, gpointer dialog);
static void on_retry_use_anonymous_toggled(GtkToggleButton *togglebutton, gpointer user_data);
static void on_retry_show_password_toggled(GtkToggleButton *togglebutton, gpointer user_data);
static void on_retry_dialog_response(GtkDialog *dialog, gint response, gpointer user_data);
static size_t write_data (void *ptr, size_t size, size_t nmemb, FILE *stream);
static size_t write_function(void *ptr, size_t size, size_t nmemb, struct string *str);

#endif
