#include <stdio.h>
#include <math.h>

#if !defined(WIN32)
  #include <gkrellm2/gkrellm.h>
#endif

#include "volume.h"
#include "mixer.h"

#define VOLUME_STYLE style_id
static gint style_id;
static GkrellmMonitor *monitor;
static GtkWidget *pluginbox;
static Mixer *Mixerz;
static int global_flags = 0;
static int config_global_flags = 0;
static GtkWidget *right_click_entry;
static char right_click_cmd[1024];

/* functions for the bookkeeping of open mixers and sliders */
/* retuns the added mixer or and existing one with the same id */
static Mixer *add_mixer_by_id(char *id) {
  Mixer *result,**m;
  mixer_t *mixer;

  if (Mixerz == NULL) m = &Mixerz;
  else {
    result = Mixerz;
    do {
        if (!strcmp(id,result->id)) return result;
    } while (result->next != NULL && (result = result->next));
    m = &(result->next);
  }

  if ((mixer = mixer_open(id)) == NULL) return NULL;

  result = malloc(sizeof(Mixer));
  result->id = strdup(id);
  result->mixer = mixer;
  result->next = NULL;
  result->Sliderz = NULL;
  /* add The Mixer to the end */
  *m = result;
  return result;
}

static void remove_mixer(Mixer *m) {
  Slider *s,*s1;
  Mixer *tmp;

  for (s = m->Sliderz ; s != NULL ;s = s1) {
    gkrellm_panel_destroy(s->panel);
    if (s->bal) gkrellm_panel_destroy(s->bal->panel);
    s1 = s->next;
    free(s->bal);
    free(s);
  }

  mixer_close(m->mixer);
  free(m->id);
  if (Mixerz == m) Mixerz = m->next;
  else {
    for (tmp = Mixerz; tmp->next != m; tmp = tmp->next);
    /* tmp->next == m */
    tmp->next = m->next;
  }
}

static void remove_all_mixers() {
  while(Mixerz != NULL) remove_mixer(Mixerz);
}

static Slider *add_slider(Mixer *m, int dev) {
  Slider *result,*s;
  if (dev < 0 || dev >= mixer_get_nr_devices(m->mixer)) return NULL;
  result = malloc(sizeof(Slider));
  result->mixer = m->mixer;
  result->parent = m;
  result->dev = dev;
  result->flags = 0;
  result->next = NULL;
  result->krell = NULL;
  result->panel = NULL;
  result->balance = 0;
  result->pleft = result->pright = -1;
  result->bal = NULL;
  if (m->Sliderz == NULL) m->Sliderz = result;
  else {
    for (s = m->Sliderz ; s->next != NULL; s = s->next);
    s->next = result; 
  }
  return result;
}

/*---*/

static gint
bvolume_expose_event(GtkWidget *widget, GdkEventExpose *event,gpointer slide) {
  gdk_draw_pixmap(widget->window,
                  widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
                  ((Bslider *)slide)->panel->pixmap,
                  event->area.x, event->area.y,
                  event->area.x, event->area.y,
                  event->area.width, event->area.height);
  return TRUE;
}

static gint
volume_expose_event(GtkWidget *widget, GdkEventExpose *event,gpointer slide) {
  gdk_draw_pixmap(widget->window,
                  widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
                  ((Slider *)slide)->panel->pixmap,
                  event->area.x, event->area.y,
                  event->area.x, event->area.y,
                  event->area.width, event->area.height);
  return TRUE;
}

static gint
volume_get_volume(Slider *s) {
  gint left,right;
  mixer_get_device_volume(s->mixer,s->dev,&left,&right);
  return left > right ?  left : right;
}

static void 
volume_show_volume(Slider *s) {
  if (s->krell != NULL) 
    gkrellm_update_krell(s->panel,s->krell,volume_get_volume(s));
  gkrellm_draw_panel_layers(s->panel);
  gkrellm_config_modified();
}


static void
volume_set_volume(Slider *s,gint volume) {
  gint left,right;

  if (GET_FLAG(s->flags,MUTED)) return;
  volume = volume < 0 ? 0 : volume;
  if (volume < 0) volume = 0;
  else if (volume > mixer_get_device_fullscale(s->mixer,s->dev)) 
      volume = mixer_get_device_fullscale(s->mixer,s->dev);

  if (s->balance == 0 && !GET_FLAG(s->flags,BALANCE)) left = right = volume;
  else if (s->balance > 0) {
    right = volume;
    left = ((100 - s->balance)* volume) / 100;
  } else { /* balance < 0 */
    left = volume;
    right = ((100 + s->balance)* volume) / 100;
  }
  mixer_set_device_volume(s->mixer,s->dev,left,right);
  s->pleft = left; s->pright = right;
  volume_show_volume(s);
}

static void volume_show_balance(Slider *s) {
  gchar *buf;
  gchar *buf_utf8 = NULL, *buf_locale = NULL;
  if (s->bal == NULL) return;
  if (s->balance == 0) buf = g_strdup(_("Centered"));
  else buf = g_strdup_printf("%3d%% %s",abs(s->balance),
                   s->balance > 0 ? _("Right") : _("Left"));

  gkrellm_locale_dup_string(&buf_utf8, buf, &buf_locale);
  gkrellm_draw_decal_text(s->bal->panel,s->bal->decal,buf_locale,-1);
  gkrellm_update_krell(s->bal->panel,s->bal->krell,s->balance + 100 );
  gkrellm_draw_panel_layers(s->bal->panel);
  g_free(buf);
  g_free(buf_locale);
  g_free(buf_utf8);
}

static void
volume_set_balance(Slider *s,gint amount) {
  if (amount < -100) amount = -100;
  else if (amount > 100) amount = 100;
  if (abs(amount) <= 3) amount = 0;
  s->balance = amount;
  volume_set_volume(s,volume_get_volume(s));
  volume_show_balance(s);
}

static void
volume_mute_mixer(Mixer *m) {
  Slider *s;
  for (s = m->Sliderz ; s != NULL ; s = s->next) {
      mixer_set_device_volume(s->mixer,s->dev,0,0);
      volume_show_volume(s);
      SET_FLAG(s->flags,MUTED);
  }
}

static void 
volume_unmute_mixer(Mixer *m) {
  Slider *s;
  for (s = m->Sliderz ; s != NULL ; s = s->next) {
      DEL_FLAG(s->flags,MUTED);
      mixer_set_device_volume(s->mixer,s->dev,s->pleft,s->pright);
      volume_show_volume(s);
  }
}

static void
volume_toggle_mute(Slider *s) {
  Mixer *m;
  if (GET_FLAG(s->flags,MUTED)) {
    if (GET_FLAG(global_flags,MUTEALL)) {
      for (m = Mixerz ; m != NULL; m = m->next) volume_unmute_mixer(m);
    } else volume_unmute_mixer(s->parent);
  } else {
    if (GET_FLAG(global_flags,MUTEALL)) {
      for (m = Mixerz ; m != NULL; m = m->next) volume_mute_mixer(m);
    } else volume_mute_mixer(s->parent);
  }
}

static gint 
bvolume_cb_scroll(GtkWidget *widget, GdkEventScroll *event,Bslider *s) {
  int amount = 0;
  switch (event->direction) {
    case GDK_SCROLL_UP:
    case GDK_SCROLL_RIGHT:
      amount = 5;
      break;
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_LEFT:
      amount = -5;
      break;
  }
  volume_set_balance(s->slider,s->slider->balance + amount);
  return TRUE;
}

static gint 
volume_cb_scroll(GtkWidget *widget, GdkEventScroll *event,Slider *s) {
  gint  amount;
  amount = volume_get_volume(s);
  switch (event->direction) {
    case GDK_SCROLL_UP:
    case GDK_SCROLL_RIGHT:
      amount += 5;
      break;
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_LEFT:
      amount -= 5;
      break;
  }
  volume_set_volume(s,amount);
  return TRUE;
}

static void
run_right_click_cmd() {
    if (right_click_cmd) {
      g_spawn_command_line_async(right_click_cmd,NULL);
    }
}

static void
bvolume_button_press(GtkWidget *widget, GdkEventButton *ev,Bslider *s) {
  long location;
  if (ev->button == 1) {
    SET_FLAG(s->flags,IS_PRESSED);
    location = ev->x  - s->krell->x0 ;
    location = location >= 0 ? location : 0;
    location = (location * 200)
              / s->krell->w_scale;
    volume_set_balance(s->slider,location - 100);
  }
  else if (ev->button == 3) {
    run_right_click_cmd();
  }
}

static void
volume_button_press(GtkWidget *widget,GdkEventButton *ev,Slider *s) {
  long location;
  if (ev->button == 1) {
    SET_FLAG(s->flags,IS_PRESSED);
    location = ev->x  - s->krell->x0 ;
    location = location >= 0 ? location : 0;
    location = (location * mixer_get_device_fullscale(s->mixer,s->dev))
              / s->krell->w_scale;
    volume_set_volume(s,location); 
  }
  else if (ev->button == 3) {
    run_right_click_cmd();
  }
}

static void
bvolume_button_release(GtkWidget *widget,GdkEventButton *ev,Bslider *s) {
  if (ev->button == 1) DEL_FLAG(s->flags,IS_PRESSED);
  if (ev->button == 2) volume_toggle_mute(s->slider);
}

static void
volume_button_release(GtkWidget *widget,GdkEventButton *ev,Slider *s) {
  if (ev->button == 1) DEL_FLAG(s->flags,IS_PRESSED);
  if (ev->button == 2) volume_toggle_mute(s);
}


static void
bvolume_motion(GtkWidget *widget,GdkEventMotion *ev,Bslider *s) {
  gdouble location;
  if (!(GET_FLAG(s->flags,IS_PRESSED))) return;
  if (!(ev->state & GDK_BUTTON1_MASK)) {
    /* just to be sure */
    DEL_FLAG(s->flags,IS_PRESSED); return ;
  }
  location = ev->x  - s->krell->x0 ;
  location = location >= 0 ? location : 0;
  location = (location * 200)
              / s->krell->w_scale;
  volume_set_balance(s->slider,location - 100);
}

static void
volume_motion(GtkWidget *widget,GdkEventMotion *ev,Slider *s) {
  gdouble location;
  if (!GET_FLAG(s->flags,IS_PRESSED)) return;
  if (!(ev->state & GDK_BUTTON1_MASK)) {
    /* just to be sure */
    DEL_FLAG(s->flags,IS_PRESSED); return ;
  }
  location = ev->x  - s->krell->x0 ;
  location = location >= 0 ? location : 0;
  location = (location * mixer_get_device_fullscale(s->mixer,s->dev))
              / s->krell->w_scale;
  volume_set_volume(s,location);
}

static void create_bslider(Slider *slide,int first_create) {
  GkrellmStyle *panel_style = gkrellm_meter_style(VOLUME_STYLE);
  GkrellmStyle *slider_style = //gkrellm_krell_slider_style();
    gkrellm_copy_style(gkrellm_meter_style_by_name("volume.balance_slider"));

  GkrellmTextstyle *ts = gkrellm_meter_textstyle(VOLUME_STYLE);
  GkrellmPiximage *krell_image;
  Bslider *result;

  gkrellm_set_style_slider_values_default(slider_style,0,0,0);

  if (first_create) {
    result = malloc(sizeof(Bslider));
    result->panel = gkrellm_panel_new0();
    slide->bal = result;
    result->slider = slide;
  } else result = slide->bal;

  krell_image = gkrellm_krell_slider_piximage();
  result->krell = gkrellm_create_krell(result->panel,krell_image,slider_style);

  /* [-100..+100] */
  gkrellm_set_krell_full_scale(result->krell,200, 1);
  gkrellm_monotonic_krell_values(result->krell, FALSE);

  result->decal = gkrellm_create_decal_text(result->panel,_("Centered"),
      ts,panel_style,-1,-1,-1);
  gkrellm_draw_decal_text(result->panel,result->decal,_("Centered"),-1);

  gkrellm_panel_configure(result->panel,NULL, panel_style);
  gkrellm_panel_create(pluginbox, monitor, result->panel);

   if (!gkrellm_style_is_themed(slider_style,GKRELLMSTYLE_KRELL_YOFF))
         gkrellm_move_krell_yoff(result->panel,
              result->krell,(result->panel->h - result->krell->h_frame) / 2);


  if (first_create) {
    g_signal_connect(GTK_OBJECT(result->panel->drawing_area), "expose_event",
                         G_CALLBACK(bvolume_expose_event),result);
    g_signal_connect(G_OBJECT(result->panel->drawing_area), "scroll_event", 
                         G_CALLBACK(bvolume_cb_scroll), result);
    g_signal_connect(G_OBJECT(result->panel->drawing_area),"button_press_event",
                        G_CALLBACK(bvolume_button_press),result);
    g_signal_connect(GTK_OBJECT(result->panel->drawing_area),
        "button_release_event", G_CALLBACK(bvolume_button_release),result);
    g_signal_connect(GTK_OBJECT(result->panel->drawing_area),
        "motion_notify_event", G_CALLBACK(bvolume_motion),result);
  }

  volume_show_balance(slide);
}

void 
toggle_button_press(GkrellmDecalbutton *button, Slider *s) {
  int l,r ;
  mixer_get_device_volume(s->mixer, s->dev, &l, &r);
  mixer_set_device_volume(s->mixer, s->dev, ++l % 2, ++r % 2);
}

static void create_slider(Slider *s,int first_create) {
  GkrellmStyle *panel_style = gkrellm_meter_style(VOLUME_STYLE);
  GkrellmStyle *slider_style = 
    gkrellm_copy_style(gkrellm_meter_style_by_name("volume.level_slider"));
  GkrellmPiximage *krell_image;

  /* Switches not supported yet ! */
  if (mixer_get_device_fullscale(s->mixer, s->dev) == 1) return;

  gkrellm_set_style_slider_values_default(slider_style,0,0,0);

  if (first_create) s->panel = gkrellm_panel_new0();


  gkrellm_panel_configure(s->panel,
                          mixer_get_device_name(s->mixer,s->dev),
                          panel_style);
  gkrellm_panel_create(pluginbox, monitor, s->panel);
  /* center the krell if the style is not themed */
  if (mixer_get_device_fullscale(s->mixer, s->dev) != 1) {
    krell_image = gkrellm_krell_slider_piximage();
    s->krell = gkrellm_create_krell(s->panel,krell_image,slider_style);
    gkrellm_set_krell_full_scale(s->krell, 
                               mixer_get_device_fullscale(s->mixer,s->dev), 1);
    gkrellm_monotonic_krell_values(s->krell, FALSE);

    if (!gkrellm_style_is_themed(slider_style,GKRELLMSTYLE_KRELL_YOFF))
      gkrellm_move_krell_yoff(s->panel, 
          s->krell,(s->panel->h - s->krell->h_frame) / 2);

    if (first_create) {
       g_signal_connect(G_OBJECT(s->panel->drawing_area),
                           "scroll_event", G_CALLBACK(volume_cb_scroll), s);
       g_signal_connect(G_OBJECT(s->panel->drawing_area), "button_press_event", 
                        G_CALLBACK(volume_button_press),s);
       g_signal_connect(GTK_OBJECT(s->panel->drawing_area),
                        "button_release_event",
                        G_CALLBACK(volume_button_release),s);
       g_signal_connect(GTK_OBJECT(s->panel->drawing_area),
                       "motion_notify_event",
                      G_CALLBACK(volume_motion),s);
    }
  } else {
    g_assert_not_reached();
    s->button = gkrellm_make_scaled_button(s->panel, NULL,
                                           toggle_button_press, s, 
                                           FALSE, FALSE, 0, 0, 0,
                                           gkrellm_chart_width() - 15, 0,
                                           10, 0 );
  }

  if (first_create) {
    g_signal_connect(GTK_OBJECT(s->panel->drawing_area), "expose_event",
      G_CALLBACK(volume_expose_event),s);
  }
  volume_show_volume(s);
  if (GET_FLAG(s->flags,BALANCE)) create_bslider(s,first_create);
}

static void create_volume_plugin(GtkWidget *vbox,gint first_create) {
  Mixer *m;
  Slider *s;

  pluginbox = vbox;
  for (m = Mixerz ; m != NULL; m = m->next)
    for (s = m->Sliderz; s != NULL ; s = s->next) {
    create_slider(s,first_create);
  }
}

static void update_volume_plugin(void) {
  Slider *s;
  Mixer *m;
  for (m = Mixerz; m != NULL; m = m->next) 
    for (s = m->Sliderz ; s != NULL; s = s->next) {
      int left,right;
      mixer_get_device_volume(s->mixer,s->dev,&left,&right);
      /* calculate the balance and show volume if needed */
      if (s->pleft!=left || s->pright!=right) { 
        if (GET_FLAG(s->flags,BALANCE)) {
          if (left < right) {
            s->balance = 100 - (gint) rint(((gdouble)left/right) * 100);
          } else if (left > right) {
            s->balance = (gint) rint(((gdouble)right/left) * 100) - 100;
          } else if (left == right && left != 0) s->balance = 0;
          volume_show_balance(s);
        }
       if (!GET_FLAG(s->flags,MUTED)) { s->pleft = left; s->pright = right; }
       volume_show_volume(s);
     }
   }
}

static void
save_volume_plugin_config(FILE *f) {
  Mixer *m;
  Slider *s;
  if (GET_FLAG(global_flags,MUTEALL)) fprintf(f,"%s MUTEALL\n",CONFIG_KEYWORD);
  
  if (right_click_cmd) {
      fprintf(f, "%s RIGHT_CLICK_CMD %s\n", CONFIG_KEYWORD,
              right_click_cmd);
  }
                              
  for (m = Mixerz ; m != NULL ; m = m->next) { 
    fprintf(f,"%s ADDMIXER %s\n",CONFIG_KEYWORD,m->id);

    for(s = m->Sliderz; s != NULL; s = s->next) {
      fprintf(f,"%s ADDDEV %d\n",CONFIG_KEYWORD,s->dev);

      if (strcmp(mixer_get_device_name(s->mixer,s->dev),
                 mixer_get_device_real_name(s->mixer,s->dev))) {
        fprintf(f,"%s SETDEVNAME %s\n",CONFIG_KEYWORD,
            mixer_get_device_name(s->mixer,s->dev));
      }
      if (GET_FLAG(s->flags,BALANCE))
        fprintf(f,"%s SHOWBALANCE\n",CONFIG_KEYWORD);

      if (GET_FLAG(s->flags,SAVE_VOLUME)) {
        int left,right;
        mixer_get_device_volume(s->mixer,s->dev,&left,&right);
        fprintf(f,"%s SETVOLUME %d %d\n",CONFIG_KEYWORD,left,right);
      }
    }
  }
}

static void 
load_volume_plugin_config(gchar *command) {
  static Mixer *m = NULL;
  static Slider *s = NULL;
  gchar *arg;
  /* gkrellm doesn't care if we fsck the string it gives us */
  for (arg = command; !isspace(*arg); arg++);
  *arg = '\0'; arg++;

  if (!strcmp("MUTEALL",command)) SET_FLAG(global_flags,MUTEALL);
  else if (!strcmp("ADDMIXER",command)) {
    m = add_mixer_by_id(arg);
  } else if (!strcmp("RIGHT_CLICK_CMD",command)) {
    g_strlcpy(right_click_cmd, arg, sizeof(right_click_cmd));
  } else if (!strcmp("ADDDEV",command)) {
    if (m != NULL) s = add_slider(m,atoi(arg));
  } else if (!strcmp("SETDEVNAME",command)) {
    if (s != NULL) mixer_set_device_name(s->mixer,s->dev,arg);
  } else if (!strcmp("SHOWBALANCE",command)) {
    if (s != NULL) SET_FLAG(s->flags,BALANCE);
  } else if (!strcmp("SETVOLUME",command)) {
    if (s != NULL) {
      char *next;
      int left,right;
      left = strtol(arg,&next,10);
      right = strtol(next,NULL,10);
      mixer_set_device_volume(s->mixer,s->dev,left,right);
      SET_FLAG(s->flags,SAVE_VOLUME);
    }
  } 
}

/* configuration code */
enum {
  ID_COLUMN = 0, 
  NAME_COLUMN,
  C_MODEL_COLUMN,
  C_NB_COLUMN,
  N_COLUMNS
};

enum {
  C_ENABLED_COLUMN = 0,
  C_VOLUME_COLUMN,
  C_BALANCE_COLUMN,
  C_NAME_COLUMN,
  C_SNAME_COLUMN,
  C_DEVNR_COLUMN,
  C_N_COLUMNS
};

GtkListStore *model;
GtkWidget *config_notebook;
/* Used to only update the sliderz if needed. So there isn't any unnecessary
 * flickering */
gboolean mixer_config_changed = FALSE;

static void 
toggle_item(gchar *path_str,gpointer data,gint column) {
  GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
  GtkTreeIter iter;
  gboolean item;

  gtk_tree_model_get_iter(GTK_TREE_MODEL(data),&iter,path);
  gtk_tree_model_get(GTK_TREE_MODEL(data),&iter,column,&item,-1);

  gtk_list_store_set(GTK_LIST_STORE(data),&iter,column,!item,-1);
  mixer_config_changed = TRUE;

  gtk_tree_path_free(path);
}

static void 
toggle_enabled(GtkCellRendererToggle *cell,gchar *path_str,gpointer data) {
  toggle_item(path_str,data,C_ENABLED_COLUMN);
}

static void 
toggle_volume(GtkCellRendererToggle *cell,gchar *path_str,gpointer data) {
  toggle_item(path_str,data,C_VOLUME_COLUMN);
}

static void
toggle_balance(GtkCellRendererToggle *cell,gchar *path_str,gpointer data) {
  toggle_item(path_str,data,C_BALANCE_COLUMN);
}

static void
device_name_edited(GtkCellRendererText *text,
                    gchar *pathstr,gchar *value,gpointer user_data) {
  GtkTreePath *path = gtk_tree_path_new_from_string(pathstr);
  GtkTreeIter iter;

  gtk_tree_model_get_iter(GTK_TREE_MODEL(user_data),&iter,path);
  gtk_list_store_set(GTK_LIST_STORE(user_data),&iter,C_SNAME_COLUMN,value,-1);
  mixer_config_changed = TRUE;
}

static void up_clicked(GtkWidget *widget,gpointer user_data) {
  GtkTreeIter selected,up,new;
  GtkTreeView *view = GTK_TREE_VIEW(user_data);
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreePath *path;
  gchar *name,*id;
  gpointer *child_model,*nb;

  selection = gtk_tree_view_get_selection(view);

  if (!gtk_tree_selection_get_selected(selection,&model,&selected)) return;
  gtk_tree_model_get(model,&selected,
       ID_COLUMN,&id,
       NAME_COLUMN,&name,
       C_MODEL_COLUMN,&child_model,
       C_NB_COLUMN,&nb,
        -1);

  path = gtk_tree_model_get_path(model,&selected);
  if (!gtk_tree_path_prev(path)) return;
  if (!gtk_tree_model_get_iter(model,&up,path)) return;
  gtk_list_store_remove(GTK_LIST_STORE(model),&selected);
  gtk_list_store_insert_before(GTK_LIST_STORE(model),&new,&up);

  gtk_list_store_set(GTK_LIST_STORE(model),&new,
       ID_COLUMN,id,
       NAME_COLUMN,name,
       C_MODEL_COLUMN,child_model,
       C_NB_COLUMN,nb,
       -1);
  mixer_config_changed = TRUE;

}

static void down_clicked(GtkWidget *widget,gpointer user_data) {
  GtkTreeIter selected,down,new;
  GtkTreeView *view = GTK_TREE_VIEW(user_data);
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreePath *path;
  gchar *name,*id;
  gpointer *child_model,*nb;

  selection = gtk_tree_view_get_selection(view);

  if (!gtk_tree_selection_get_selected(selection,&model,&selected)) return;
  gtk_tree_model_get(model,&selected,
       ID_COLUMN,&id,
       NAME_COLUMN,&name,
       C_MODEL_COLUMN,&child_model,
       C_NB_COLUMN,&nb,
        -1);

  path = gtk_tree_model_get_path(model,&selected);
  gtk_tree_path_next(path);
  if (!gtk_tree_model_get_iter(model,&down,path)) return;
  gtk_list_store_insert_after(GTK_LIST_STORE(model),&new,&down);

  gtk_list_store_set(GTK_LIST_STORE(model),&new,
       ID_COLUMN,id,
       NAME_COLUMN,name,
       C_MODEL_COLUMN,child_model,
       C_NB_COLUMN,nb,
       -1);
  mixer_config_changed = TRUE;
  gtk_list_store_remove(GTK_LIST_STORE(model),&selected);
}

static GtkWidget *
create_device_notebook(GtkListStore *store, char *name) {
  GtkWidget *treeview;
  GtkCellRenderer *renderer;
  GtkWidget *scrolledwindow;
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *widget;
  GtkWidget *topvbox;
  
  topvbox = gtk_vbox_new(FALSE,0);
  gtk_container_set_border_width(GTK_CONTAINER(topvbox), 0);
  label = gtk_label_new(name);
  /* put the notebook at the end */
  gtk_notebook_insert_page(GTK_NOTEBOOK(config_notebook), topvbox, label,
      gtk_notebook_get_n_pages(GTK_NOTEBOOK(config_notebook)) - 3);

  vbox = gkrellm_gtk_framed_vbox(topvbox,NULL,2,TRUE,0,2);

  treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(treeview),TRUE);
  gtk_tree_selection_set_mode(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
      GTK_SELECTION_SINGLE);

  /* causes it to be destroyed when the treeview is destroyed */
  g_object_unref(G_OBJECT(store));

  renderer = gtk_cell_renderer_toggle_new();
  g_signal_connect(G_OBJECT(renderer),"toggled",
      G_CALLBACK(toggle_enabled),store);

  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, 
      _("Enabled"),renderer,
      "active",C_ENABLED_COLUMN,
      NULL);

  renderer = gtk_cell_renderer_toggle_new();
  g_signal_connect(G_OBJECT(renderer),"toggled",
      G_CALLBACK(toggle_volume),store);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, 
      _("Save volume"),renderer,
      "active",C_VOLUME_COLUMN,
      "activatable",C_ENABLED_COLUMN,
      NULL);

#if !defined(WIN32)
  renderer = gtk_cell_renderer_toggle_new();
  g_signal_connect(G_OBJECT(renderer),"toggled",
      G_CALLBACK(toggle_balance),store);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, 
      _("Balance"),renderer,
      "active",C_BALANCE_COLUMN,
      "activatable",C_ENABLED_COLUMN,
      NULL);
#endif

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, 
      _("Name"),renderer,
      "text", C_NAME_COLUMN,
      NULL);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview),
      -1, _("Shown Name"),renderer,
      "text",C_SNAME_COLUMN,
      "editable",C_ENABLED_COLUMN,
      NULL);
  g_signal_connect(G_OBJECT(renderer),"edited",
      G_CALLBACK(device_name_edited),store);

  scrolledwindow = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
                     GTK_POLICY_AUTOMATIC,
                     GTK_POLICY_ALWAYS);


  hbox = gtk_hbox_new(FALSE, 3);
  widget = gtk_button_new_from_stock(GTK_STOCK_GO_UP);
  g_signal_connect(G_OBJECT(widget),"clicked",
                   G_CALLBACK(up_clicked),treeview);
  gtk_box_pack_start(GTK_BOX(hbox),widget,FALSE,FALSE,3);

  widget = gtk_button_new_from_stock(GTK_STOCK_GO_DOWN);
  g_signal_connect(G_OBJECT(widget),"clicked",
                   G_CALLBACK(down_clicked),treeview);
  gtk_box_pack_start(GTK_BOX(hbox),widget,FALSE,FALSE,3);


  gtk_box_pack_start(GTK_BOX(vbox),scrolledwindow,TRUE,TRUE,3);
  gtk_container_add(GTK_CONTAINER(scrolledwindow),treeview);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 3);

  gtk_widget_show_all(topvbox);
  return topvbox;
}

static void add_mixer_to_model(char *id, mixer_t *mixer, Slider *s) {
  GtkTreeIter iter;
  GtkListStore *child_model;
  gboolean enabled,save_volume,balance;
  GtkWidget *notebook;
  int i;

  child_model = gtk_list_store_new(C_N_COLUMNS,
                                  G_TYPE_BOOLEAN, /* enabled or not */
                                  G_TYPE_BOOLEAN, /* save volume or not */
                                  G_TYPE_BOOLEAN, /* show balance or not */
                                  G_TYPE_STRING,  /* real name */
                                  G_TYPE_STRING,  /* set name */
                                  G_TYPE_INT      /* device number */
      );

   for(i = 0; i < mixer_get_nr_devices(mixer); i++) {
     if (mixer_get_device_fullscale(mixer, i) == 1) {
       /*Switch not supported yet */
       continue;
     }
     if (s != NULL && s->dev == i) { 
       enabled = TRUE;
       save_volume = GET_FLAG(s->flags,SAVE_VOLUME);
       balance = GET_FLAG(s->flags,BALANCE);
       s = s->next;
      } else {
        enabled = save_volume = balance = FALSE;
      }

      gtk_list_store_append(child_model,&iter);
      gtk_list_store_set(child_model,&iter,
        C_ENABLED_COLUMN,enabled,
        C_VOLUME_COLUMN,save_volume,
        C_BALANCE_COLUMN,balance,
        C_NAME_COLUMN,mixer_get_device_real_name(mixer,i),
        C_SNAME_COLUMN,mixer_get_device_name(mixer,i),
        C_DEVNR_COLUMN,i,
        -1);
  }

  notebook = create_device_notebook(child_model,mixer_get_name(mixer));

  gtk_list_store_append(model,&iter);
  gtk_list_store_set(model,&iter,
                       ID_COLUMN,id,
                       NAME_COLUMN,mixer_get_name(mixer),
                       C_MODEL_COLUMN,child_model,
                       C_NB_COLUMN,notebook,
                       -1);
  return;
}

static gboolean findid(GtkTreeModel *m,GtkTreePath *path,
                                            GtkTreeIter *iter,gpointer data) {
  char *item;
  char **arg = (char **) data;
  gtk_tree_model_get(m,iter,ID_COLUMN,&item,-1);
  if (!strcmp(item,*arg)) {
    *arg = NULL;
    return TRUE;
  }
  return FALSE;
}

static void add_mixerid_to_model(char *id,gboolean gui) {
  char **arg = &id;
  char *name;
  mixer_t *mixer;

  gtk_tree_model_foreach(GTK_TREE_MODEL(model),findid,arg);
  if (id == NULL) { 
    if (gui) gkrellm_message_window(_("Error"),_("Id already in list"),NULL); 
    return;
  }
  if ((mixer = mixer_open(id)) == NULL) {
    if (gui) {
      name = 
        g_strdup_printf(_("Couldn't open %s or %s isn't a mixer device"),id,id);
      gkrellm_message_window(_("Error"),name,NULL);
      g_free(name);
    }
    return;
  }
  add_mixer_to_model(id,mixer, NULL);
  mixer_close(mixer);
  return;
}
#ifndef WIN32
static void file_choosen(GtkWidget *w,gpointer selector) {
  char *id;
  id = (char *) gtk_file_selection_get_filename(GTK_FILE_SELECTION(selector));
  add_mixerid_to_model(id,TRUE);
}

static void select_file(GtkWidget *widget,gpointer user_data) {
  GtkWidget *selector;
  selector = gtk_file_selection_new(_("Please select a mixer device"));
  gtk_file_selection_set_filename(GTK_FILE_SELECTION(selector),"/dev/");

  g_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(selector)->ok_button),
      "clicked",
      G_CALLBACK(file_choosen),
      selector);

  g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(selector)->ok_button),
      "clicked",
      G_CALLBACK (gtk_widget_destroy), (gpointer) selector); 

  g_signal_connect_swapped(
      GTK_OBJECT(GTK_FILE_SELECTION(selector)->cancel_button),
      "clicked", G_CALLBACK (gtk_widget_destroy), (gpointer) selector); 
  gtk_widget_show(selector);
}
#endif

static void create_volume_model(void) {
  Mixer *m;
  mixer_idz_t *idz,*t;

  model = gtk_list_store_new(N_COLUMNS,
                             G_TYPE_STRING,  /* id */
                             G_TYPE_STRING,  /* name */
                             G_TYPE_POINTER, /* pointer to the child store */
                             G_TYPE_POINTER  /* pointer to the child NB */
      );
  for (m = Mixerz; m != NULL; m = m->next) {
    add_mixer_to_model(m->id,m->mixer,m->Sliderz);
  }
  idz = mixer_get_id_list();
  for (t = idz; t != NULL; t = t->next) {
    add_mixerid_to_model(t->id,FALSE);
  }
  mixer_free_idz(idz);
}

static void create_volume_plugin_mixer_tabs(void) {
  GtkWidget *treeview;
  GtkWidget *widget,*hbox,*vbox;
  GtkWidget *scrolledwindow;
  GtkWidget *notebook;
  GtkCellRenderer *renderer;
  
  notebook = 
    gkrellm_gtk_framed_notebook_page(config_notebook,_("Available mixers"));

  /* Ugly hack to put the gkrellm created notebook at the start */
  gtk_notebook_reorder_child(GTK_NOTEBOOK(config_notebook), 
    gtk_notebook_get_nth_page(GTK_NOTEBOOK(config_notebook), -1),
    0);

  vbox = gtk_vbox_new(FALSE,3);
  gtk_container_add(GTK_CONTAINER(notebook),vbox);
  create_volume_model();

  /* creating the treeview */

  treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  gtk_tree_view_set_reorderable(GTK_TREE_VIEW(treeview),TRUE);
  gtk_tree_selection_set_mode(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
      GTK_SELECTION_SINGLE);

  /* causes it to be destroyed when the treeview is destroyed */
  g_object_unref(G_OBJECT(model));

  renderer = gtk_cell_renderer_toggle_new();
  g_signal_connect(G_OBJECT(renderer),"toggled",
      G_CALLBACK(toggle_enabled),model);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, 
      _("Id"),renderer,
      "text", ID_COLUMN,
      NULL);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview),
      -1, _("Mixer Name"),renderer,
      "text",NAME_COLUMN,
      NULL);

  scrolledwindow = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
                     GTK_POLICY_AUTOMATIC,
                     GTK_POLICY_ALWAYS);


  gtk_box_pack_start(GTK_BOX(vbox),scrolledwindow,TRUE,TRUE,3);
  gtk_container_add(GTK_CONTAINER(scrolledwindow),treeview);

  hbox = gtk_hbox_new(FALSE,3);

  widget = gtk_button_new_from_stock(GTK_STOCK_GO_UP);
  g_signal_connect(G_OBJECT(widget),"clicked",
                   G_CALLBACK(up_clicked),treeview);
  gtk_box_pack_start(GTK_BOX(hbox),widget,FALSE,FALSE,3);

  widget = gtk_button_new_from_stock(GTK_STOCK_GO_DOWN);
  g_signal_connect(G_OBJECT(widget),"clicked",
                   G_CALLBACK(down_clicked),treeview);
  gtk_box_pack_start(GTK_BOX(hbox),widget,FALSE,FALSE,3);


#ifndef WIN32
  widget = gtk_button_new_from_stock(GTK_STOCK_ADD);
  gtk_box_pack_end(GTK_BOX(hbox),widget,FALSE,FALSE,3);
  g_signal_connect(G_OBJECT(widget),"clicked",
                   G_CALLBACK(select_file),model);
#endif
  gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,FALSE,3);
  gtk_widget_show_all(notebook);
}

static void option_toggle(GtkWidget *widget,gpointer data) {
  if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    SET_FLAG(config_global_flags,GPOINTER_TO_INT(data));
  else
    DEL_FLAG(config_global_flags,GPOINTER_TO_INT(data));
}

static void
create_volume_plugin_config(GtkWidget *tab) {
  GtkWidget *label,*text,*page,*toggle,*right_click_hbox,*right_click_label;
  gchar *info_text[] = { 
   N_("<b>Gkrellm Volume Plugin\n\n"),
   N_("This plugin allows you to control your mixers with gkrellm\n\n"),
   N_("<b>User Interface:\n"),
   N_("\tDragging the krells around or turning your mousewheel above a panel\n" \
   "\twill change the volume of the device.\n" \
   "\tMiddle mouse button will (un)mute the mixer\n\n"),
   N_("<b> Configuration:\n"),
   N_(
   "\tThe available mixers tab shows the detected mixers. The order of the\n" \
   "\tmixers is the same as they will be in the main window\n" \
   "\n" \
   "\tEach mixer gets its own tab. You can adjust each device separately:\n" \
   "\t * Enabled: will cause the device to show up in the main window.\n" \
   "\t * Save volume: will cause the volume and balance to be saved on exit\n" \
   "\t   and reset on startup.\n"),
#if !defined(WIN32)
   N_(
   "\t * Balance: Gives you a panel below the device to control" \
   " its balance\n"),
#endif
   N_("\t * Name: The 'official' name of the device.\n" \
   "\t * Shown name: can be edited and is the name shown in the main window.\n"\
   "\n"),
   N_("<b>Options:\n"),
   N_("\t* Mute all mixers at the same time: Mutes all devices on a middle\n"\
      "\t  mouse button click instead of only the one the slider belongs to.\n"\
      "\t* Right-click command: The command to run when the right mouse\n"\
      "\t  button is clicked on the plugin\n") 
  };

  gint i;

  gchar *plugin_about_text = g_strdup_printf(
     _("Volumeplugin %d.%d.%d\n" \
     "GKrellM volume Plugin\n\n" \
     "Copyright (C) 2000 Sjoerd Simons\n" \
     "sjoerd@luon.net\n" \
     "http://gkrellm.luon.net \n\n" \
     "Released under the GNU Public Licence"),
     VOLUME_MAJOR_VERSION,VOLUME_MINOR_VERSION,VOLUME_EXTRA_VERSION);

  config_global_flags= global_flags;

  config_notebook = gtk_notebook_new();
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(config_notebook),TRUE);
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(config_notebook), GTK_POS_TOP);

  gtk_box_pack_start(GTK_BOX(tab), config_notebook, TRUE, TRUE, 0);

  /* global options tab */
  page = gkrellm_gtk_framed_notebook_page(config_notebook,_("Options"));

  /* option - mute all mixers at the same time */
  toggle = gtk_check_button_new_with_label(_("Mute all mixers at the same time"));
  g_signal_connect(GTK_OBJECT(toggle),"toggled",
                           G_CALLBACK(option_toggle),GINT_TO_POINTER(MUTEALL));
  gtk_box_pack_start(GTK_BOX(page),toggle,FALSE,FALSE,3);
  
  /* option - right-click command */
  right_click_hbox = gtk_hbox_new(FALSE, 0);
  right_click_label = gtk_label_new(_("Right-click command: "));
  gtk_box_pack_start(GTK_BOX(right_click_hbox),right_click_label,FALSE,FALSE,0);
  right_click_entry = gtk_entry_new();
  if (right_click_cmd) 
    gtk_entry_set_text((GtkEntry*)right_click_entry, right_click_cmd);

  gtk_box_pack_start(GTK_BOX(right_click_hbox),right_click_entry,TRUE,TRUE,8);
  gtk_box_pack_start(GTK_BOX(page),right_click_hbox,FALSE,FALSE,3);
  
  /* info tab */
  page = gkrellm_gtk_notebook_page(config_notebook,_("Info"));
  text = gkrellm_gtk_scrolled_text_view(page,NULL,
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  for (i=0; i < sizeof(info_text)/sizeof(gchar *); ++i)
      gkrellm_gtk_text_view_append(text,_(info_text[i]));


  /* about tab */
  text = gtk_label_new(plugin_about_text); 
  label = gtk_label_new(_("About"));
  gtk_notebook_append_page(GTK_NOTEBOOK(config_notebook),text,label);

  g_free(plugin_about_text);

  /* mixer tabs */
  create_volume_plugin_mixer_tabs();
  
  gtk_widget_show_all(config_notebook);
}

static gboolean add_configed_mixer_device(GtkTreeModel *m, GtkTreePath *path,
                                              GtkTreeIter *iter,gpointer data) {
  gboolean enabled;
  gboolean save_volume;
  gboolean balance;
  gint nr;
  Slider *s;
  gchar *rname,*name;
  gchar *id = (gchar *) data;
  Mixer *mixer;

  gtk_tree_model_get(m,iter,C_ENABLED_COLUMN,&enabled,-1);
  if (enabled) {
    mixer = add_mixer_by_id(id);

    gtk_tree_model_get(m,iter,
          C_DEVNR_COLUMN,&nr,
          C_VOLUME_COLUMN,&save_volume,
          C_BALANCE_COLUMN,&balance,
          C_NAME_COLUMN,&rname,
          C_SNAME_COLUMN,&name,
          -1);
    if (strcmp(name,rname)) mixer_set_device_name(mixer->mixer,nr,name);
    
    s = add_slider(mixer,nr);
    if (save_volume) SET_FLAG(s->flags,SAVE_VOLUME);
    else DEL_FLAG(s->flags,SAVE_VOLUME);
    if (balance) SET_FLAG(s->flags,BALANCE);
    else DEL_FLAG(s->flags,BALANCE);
    create_slider(s,1);
  }
  return FALSE;
}

static gboolean add_configed_mixer(GtkTreeModel *m,GtkTreePath *path,
                             GtkTreeIter *iter,gpointer data) {
  GtkListStore *store;
  gchar *id;

  gtk_tree_model_get(m,iter,ID_COLUMN,&id,C_MODEL_COLUMN,&store,-1);
  gtk_tree_model_foreach(GTK_TREE_MODEL(store),add_configed_mixer_device,id);

  return FALSE;
}

void apply_volume_plugin_config(void) {
  if (mixer_config_changed) {
    remove_all_mixers();
    gtk_tree_model_foreach(GTK_TREE_MODEL(model),add_configed_mixer,NULL);
    mixer_config_changed = FALSE;
  }
  global_flags = config_global_flags;
  if (right_click_entry) {
    g_strlcpy(right_click_cmd, gtk_entry_get_text((GtkEntry *)right_click_entry), 
            sizeof(right_click_cmd));
  }
}

/* end of configuration code */

static GkrellmMonitor plugin_mon  = {
  "Volume Plugin",      /* Name, for config tab.    */
  0,          /* Id,  0 if a plugin       */
  create_volume_plugin,    /* The create function      */
  update_volume_plugin,      /* The update function      */
  create_volume_plugin_config, /* The config tab create function   */
  apply_volume_plugin_config,       /* Apply the config function        */
  save_volume_plugin_config, /* Save user config     */
  load_volume_plugin_config, /* Load user config     */
  CONFIG_KEYWORD,   /* config keyword     */

  NULL,     /* Undefined 2  */
  NULL,     /* Undefined 1  */
  NULL,     /* Undefined 0  */
  LOCATION,

  NULL,       /* Handle if a plugin, filled in by GKrellM     */
  NULL        /* path if a plugin, filled in by GKrellM       */
};

#if defined(WIN32)
    __declspec(dllexport) GkrellmMonitor *
        gkrellm_init_plugin(win32_plugin_callbacks* calls)
#else
    GkrellmMonitor * gkrellm_init_plugin(void)
#endif
{
  #if defined(WIN32)
    callbacks = calls;
  #endif

#ifdef ENABLE_NLS
   bind_textdomain_codeset(PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  style_id = gkrellm_add_meter_style(&plugin_mon,"volume");
  init_mixer();
  Mixerz = NULL;
  monitor = &plugin_mon;
  return monitor;
}
