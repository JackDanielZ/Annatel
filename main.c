#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#include <Elementary.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#include <getopt.h>

#define _EET_ENTRY "config"

static const char *baseUrl = "http://www.annatel.tv";

static char _channels_list_xml[100000] = {0};

typedef struct
{
   const char *buffer;
   const char *current;
} Lexer;

typedef struct
{
   const char *username;
   const char *password;
} Config;

typedef struct
{
  char *name;
  char *url;
  char *desc;
  char *desc_title;
  char *logo_url;
} Channel_Desc;

static Eo *_main_box = NULL, *_main_grid = NULL, *_channel_desc_label = NULL;
static Elm_Gengrid_Item_Class *_main_grid_item_class = NULL;

static Eet_Data_Descriptor *_config_edd = NULL;

static Config *_config = NULL;

static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, 0.0, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(label);
        if (wref) efl_wref_add(label, wref);
     }
   elm_object_text_set(label, text);
   return label;
}

static Eo *
_button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *bt = wref ? *wref : NULL;
   if (!bt)
     {
        bt = elm_button_add(parent);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(bt, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(bt);
        if (wref) efl_wref_add(bt, wref);
        if (cb_func) evas_object_smart_callback_add(bt, "clicked", cb_func, cb_data);
     }
   elm_object_text_set(bt, text);
   elm_object_part_content_set(bt, "icon", icon);
   return bt;
}

static Eo *
_icon_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *ic = wref ? *wref : NULL;
   if (!ic)
     {
        ic = elm_icon_add(parent);
        elm_icon_standard_set(ic, path);
        evas_object_show(ic);
        if (wref) efl_wref_add(ic, wref);
     }
   return ic;
}

static void
_ws_skip(Lexer *l)
{
   /*
    * Skip spaces and \n
    */
   do
     {
        char c = *(l->current);
        switch (c)
          {
           case ' ': case '\n':
              break;
           default:
              return;
          }
        l->current++;
     }
   while (1);
}

static Eina_Bool
_is_next_token(Lexer *l, const char *token)
{
   _ws_skip(l);
   if (!strncmp(l->current, token, strlen(token)))
     {
        l->current += strlen(token);
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

static char *
_next_word(Lexer *l, const char *special, Eina_Bool special_allowed)
{
   if (!special) special = "";
   _ws_skip(l);
   const char *str = l->current;
   while (*str &&
         ((*str >= 'a' && *str <= 'z') ||
          (*str >= 'A' && *str <= 'Z') ||
          (*str >= '0' && *str <= '9') ||
          !(!!special_allowed ^ !!strchr(special, *str)) ||
          *str == '_')) str++;
   if (str == l->current) return NULL;
   int size = str - l->current;
   char *word = malloc(size + 1);
   memcpy(word, l->current, size);
   word[size] = '\0';
   l->current = str;
   return word;
}

static long
_next_integer(Lexer *l)
{
   _ws_skip(l);
   const char *str = l->current;
   while (*str && (*str >= '0' && *str <= '9')) str++;
   if (str == l->current) return -1;
   int size = str - l->current;
   char *n_str = alloca(size + 1);
   memcpy(n_str, l->current, size);
   n_str[size] = '\0';
   l->current = str;
   return atol(n_str);
}

#define JUMP_AT(l, ...) _jump_at(l, __VA_ARGS__, NULL)

static Eina_Bool
_jump_at(Lexer *l, ...)
{
   const char *token;
   Eina_Bool over;
   char *min = NULL;
   va_list args;
   va_start(args, l);
   do
     {
        token = va_arg(args, const char *);
        over = va_arg(args, int);
        if (token)
          {
             char *found = strstr(l->current, token);
             if (found) found += (over ? strlen(token) : 0);
             if (found) min = (!min || found < min ? found : min);
          }
     } while (token);
   if (!min) return EINA_FALSE;
   l->current = min;
   return EINA_TRUE;
}

static void
_config_eet_load()
{
   if (_config_edd) return;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Config);
   _config_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_BASIC(_config_edd, Config, "username", username, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(_config_edd, Config, "password", password, EET_T_STRING);
}

static void
_config_save()
{
   char path[1024];
   sprintf(path, "%s/e_annatel/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_WRITE);
   eet_data_write(file, _config_edd, _EET_ENTRY, _config, EINA_TRUE);
   eet_close(file);
}

static Eina_Bool
_mkdir(const char *dir)
{
   if (!ecore_file_exists(dir))
     {
        Eina_Bool success = ecore_file_mkdir(dir);
        if (!success)
          {
             printf("Cannot create a config folder \"%s\"\n", dir);
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static void
_config_load()
{
   char path[1024];

   sprintf(path, "%s/e_annatel", efreet_config_home_get());
   if (!_mkdir(path)) return;

   sprintf(path, "%s/e_annatel/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_READ);
   if (!file)
     {
        _config = calloc(1, sizeof(Config));
        _config->username = "USER";
        _config->password = "PASS";
     }
   else
     {
        _config = eet_data_read(file, _config_edd, _EET_ENTRY);
        eet_close(file);
     }

   _config_save();
}

static char *
_main_grid_item_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
  Channel_Desc *ch_desc = data;
  return strdup(ch_desc->name);
}

static Evas_Object *
_main_grid_item_content_get(void *data, Evas_Object *obj, const char *part)
{
  Channel_Desc *ch_desc = data;
  if (!strcmp(part, "elm.swallow.icon"))
  {
    Evas_Object *image = elm_image_add(obj);
    elm_image_file_set(image, ch_desc->logo_url, NULL);
    elm_image_aspect_fixed_set(image, EINA_FALSE);
    evas_object_show(image);
    return image;
  }
  return NULL;
}

static Eina_Bool
_main_grid_item_state_get(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
  return EINA_FALSE;
}

static void
_main_grid_item_del(void *data, Evas_Object *obj EINA_UNUSED)
{
  free(data);
}

static Eina_Bool
_url_data_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
  Ecore_Con_Event_Url_Data *url_data = event_info;
  Channel_Desc *ch_desc = ecore_con_url_data_get(url_data->url_con);

  if (ch_desc == NULL)
  {
    /* The received data is the channels list */
    strcat(_channels_list_xml, (char *)url_data->data);
  }

  return EINA_TRUE;
}

static Eina_Bool
_url_complete_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
  Ecore_Con_Event_Url_Complete *url_complete = event_info;

  Channel_Desc *ch_desc = ecore_con_url_data_get(url_complete->url_con);

  if (ch_desc == NULL)
  {
    int i;
    int end_channels = 0;

    Lexer l;
    l.buffer = l.current = _channels_list_xml;
    JUMP_AT(&l, "<channels>", 1);
    _ws_skip(&l);
    while(end_channels == 0)
    {
      _ws_skip(&l);
      if (_is_next_token(&l, "<channel>")) /* New channel */
      {
        ch_desc = malloc(sizeof(Channel_Desc));
        _ws_skip(&l);
        while (!_is_next_token(&l, "</channel>"))
        {
          l.current++; /* Skip < */
          char *w = _next_word(&l, NULL, EINA_TRUE), *str, *end;
          l.current++; /* Skip > */
          _ws_skip(&l);
          end = strstr(l.current, "</"); /* Search for end of XML element */
          str = strndup(l.current, end - l.current); /* Copy string */
          l.current = end; /* Jump over the string */
          JUMP_AT(&l, ">", 1); /* and over the XML element name */
          _ws_skip(&l);

          if (!strcmp(w, "name")) ch_desc->name = str;
          else if (!strcmp(w, "logo")) ch_desc->logo_url = str;
          else if (!strcmp(w, "url")) ch_desc->url = str;
          else if (!strcmp(w, "program_description")) ch_desc->desc = str;
          else if (!strcmp(w, "program_title")) ch_desc->desc_title = str;
          else free(str);
          free(w);
        }
        _ws_skip(&l);
        elm_gengrid_item_append(_main_grid, _main_grid_item_class, ch_desc, NULL, NULL);
      }
      if (_is_next_token(&l, "</channels>"))
      {
        end_channels = 1;
      }
    }
  }

  return EINA_TRUE;
}

static void
_grid_item_selected(void *data EINA_UNUSED, Evas_Object *obj, void *event_info)
{
  char desc[1024];
  Elm_Object_Item *it = elm_gengrid_selected_item_get(_main_grid);
  Channel_Desc *ch_desc = elm_object_item_data_get(it);
  sprintf(desc, "<font_size=20><b>Channel: </b>%s<br><b>Title: </b>%s<br><b>Description: </b>%s<br></font_size>", ch_desc->name, ch_desc->desc_title, ch_desc->desc);
  printf("Selected: %s\n", ch_desc->name);
  elm_object_text_set(_channel_desc_label, desc);
}

static void
_grid_item_unselected(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
}

static void
_grid_item_double_clicked(void *data EINA_UNUSED, Evas_Object *obj, void *event_info)
{
  Elm_Object_Item *it = elm_gengrid_selected_item_get(_main_grid);
  Channel_Desc *ch_desc = elm_object_item_data_get(it);
  printf("Clicked: %s\n", ch_desc->name);
}

int main(int argc, char **argv)
{
  Eo *win, *bg, *o, *panes1, *panes2;

  Ecore_Con_Url *channels_ecore_url;
  char channels_url[256];

  static struct option long_options[] =
  {
    /* These options set a flag. */
    {0, 0, 0, 0}
  };

  getopt_long (argc, argv, "", long_options, NULL);

  eina_init();
  ecore_init();
  ecore_con_init();
  ecore_con_url_init();
  efreet_init();
  elm_init(argc, argv);

  elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

  _config_load();

  win = elm_win_add(NULL, "Annatel", ELM_WIN_BASIC);

  bg = elm_bg_add(win);
  evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_show(bg);
  elm_win_resize_object_add(win, bg);

  elm_win_autodel_set(win, EINA_TRUE);

  panes1 = elm_panes_add(win);
  elm_panes_horizontal_set(panes1, EINA_TRUE);
  evas_object_size_hint_weight_set(panes1, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  elm_win_resize_object_add(win, panes1);
  evas_object_show(panes1);

  _main_grid = elm_gengrid_add(panes1);
  elm_gengrid_item_size_set(_main_grid, ELM_SCALE_SIZE(150), ELM_SCALE_SIZE(150));
  evas_object_size_hint_weight_set(_main_grid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(_main_grid, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_smart_callback_add(_main_grid, "selected", _grid_item_selected, NULL);
  evas_object_smart_callback_add(_main_grid, "unselected", _grid_item_unselected, NULL);
  evas_object_smart_callback_add(_main_grid, "clicked,double", _grid_item_double_clicked, NULL);
  elm_object_part_content_set(panes1, "left", _main_grid);
  elm_panes_content_left_size_set(panes1, 0.8);
  evas_object_show(_main_grid);

  _main_grid_item_class = elm_gengrid_item_class_new();
  _main_grid_item_class->item_style = "default";
  _main_grid_item_class->func.text_get = _main_grid_item_text_get;
  _main_grid_item_class->func.content_get = _main_grid_item_content_get;
  _main_grid_item_class->func.state_get = _main_grid_item_state_get;
  _main_grid_item_class->func.del = _main_grid_item_del;

  panes2 = elm_panes_add(win);
  evas_object_size_hint_weight_set(panes2, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  elm_panes_content_left_size_set(panes2, 0.8);
  evas_object_show(panes2);
  elm_object_part_content_set(panes1, "right", panes2);

  _channel_desc_label = elm_entry_add(win);
  elm_object_text_set(_channel_desc_label, "<b>This is a small label</b>");
  elm_label_line_wrap_set(_channel_desc_label, ELM_WRAP_CHAR);
  evas_object_show(_channel_desc_label);
  elm_object_part_content_set(panes2, "left", _channel_desc_label);

  elm_win_maximized_set(win, EINA_TRUE);
//  elm_win_fullscreen_set(win, EINA_TRUE);
  evas_object_show(win);

  sprintf(channels_url, "%s/api/getchannels?login=%s&password=%s", baseUrl, _config->username, _config->password);
  channels_ecore_url = ecore_con_url_new(channels_url);
  ecore_con_url_additional_header_add(channels_ecore_url, "User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:72.0) Gecko/20100101 Firefox/72.0");
  ecore_con_url_timeout_set(channels_ecore_url, 5.0);

  ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _url_data_cb, NULL);
  ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _url_complete_cb, NULL);

  if (!ecore_con_url_get(channels_ecore_url))
  {
    printf("could not realize request.\n");
    goto exit;
  }

  elm_run();

exit:
  elm_shutdown();
  efreet_shutdown();
  ecore_con_url_shutdown();
  ecore_con_shutdown();
  ecore_shutdown();
  eina_shutdown();
  return 0;
}
