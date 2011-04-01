/* $Id$
 *
 * Buzztard
 * Copyright (C) 2006 Buzztard team <buzztard-devel@lists.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:btmachinecanvasitem
 * @short_description: class for the editor machine views machine canvas item
 *
 * The canvas object emits #BtMachineCanvasItem::position-changed signal after
 * it has been moved.
 */

/* @todo more graphics:
 * - use svg gfx (design/gui/svgcanvas.c )
 *   - need to have prerenderend images for current zoom level
 *     - idealy have them in ui-resources, in order to have them shared
 *     - currently there is a ::zoom property to update the font-size
 *       its set in update_machines_zoom(), there we would need to regenerate
 *       the pixmaps too
 * - state graphics
 *   - have some gfx in the middle
 *     mute: x over o
 *     bypass: -/\- around o
 *     solo:
 *       ! over x
 *       one filled o on top of 4 hollow o's
 *   - use transparency for mute/bypass, solo would switch all other sources to
 *     muted, can't differenciate mute from bypass on an fx
 *
 * @todo: add insert before/after to context menu (see wire-canvas item)
 *
 * @todo: play machines live (midi keyboard for playing notes and triggers)
 *   - for source-machine context menu add
 *     - one item "play with > [list of keyboard]"
 *     - one more item to disconnect
 *   - playing multiple machines with a split keyboard would be nice too
 *
 * @todo: "remove and relink" is difficult if there are non empty wire patterns
 *   - those would need to be copies to new target machine(s) and we would need
 *     to add more tracks for playing them.
 *   - we could ask the user if that is what he wants:
 *     - "don't remove"
 *     - "drop wire patterns"
 *     - "copy wire patterns"
 *
 * @todo: dialog manager
 *   - store preferences and properties dialog pointers not here, but in a
 *     app-wide dialog manager
 *   - this allows to:
 *     - show/hide the dialog also from e.g. the pattern/sequence page
 *     - makes it easier to show/hide all
 *     - makes it easier to store the state in the song
 *
 * @todo: gray out "properties"/"preferences" items in context menu if resulting
 * dialog would be empty
 * - can't do that yet as it is code in the dialog that implements the logic
 *
 * @todo; make the properties dialog a readable gobject property
 * - then we can go over all of them from machine page and show/hide them
 */

#define BT_EDIT
#define BT_MACHINE_CANVAS_ITEM_C

#include "bt-edit.h"

#define LOW_VUMETER_VAL -60.0

//-- signal ids

enum {
  POSITION_CHANGED,
  START_CONNECT,
  LAST_SIGNAL
};

//-- property ids

enum {
  MACHINE_CANVAS_ITEM_MACHINES_PAGE=1,
  MACHINE_CANVAS_ITEM_MACHINE,
  MACHINE_CANVAS_ITEM_ZOOM
};


struct _BtMachineCanvasItemPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;

  /* the application */
  BtEditApplication *app;

  /* the machine page we are on */
  G_POINTER_ALIAS(BtMainPageMachines *,main_page_machines);

  /* the underlying machine */
  BtMachine *machine;
  GHashTable *properties;
  const gchar *help_uri;

  /* machine context_menu */
  GtkMenu *context_menu;
  GtkWidget *menu_item_mute,*menu_item_solo,*menu_item_bypass;
  gulong id_mute,id_solo,id_bypass;

  /* the properties and preferences dialogs */
  GtkWidget *properties_dialog;
  GtkWidget *preferences_dialog;
  /* the analysis dialog */
  GtkWidget *analysis_dialog;

  /* the graphical components */
  GnomeCanvasItem *label;
  GnomeCanvasItem *box;
  GnomeCanvasItem *output_meter, *input_meter;
  G_POINTER_ALIAS(GstElement *,output_level);
  G_POINTER_ALIAS(GstElement *,input_level);
  gdouble last_input_level_value;
  gdouble last_output_level_value;

  GstClock *clock;

  /* cursor for moving */
  GdkCursor *drag_cursor;

  /* the zoomration in pixels/per unit */
  gdouble zoom;

  /* interaction state */
  gboolean dragging,moved/*,switching*/;
  gdouble offx,offy,dragx,dragy;

  /* playback state */
  gboolean is_playing;

  /* lock for multithreaded access */
  GMutex        *lock;
};

static guint signals[LAST_SIGNAL]={0,};

static GQuark bus_msg_level_quark=0;
static GQuark machine_canvas_item_quark=0;

//-- the class

G_DEFINE_TYPE (BtMachineCanvasItem, bt_machine_canvas_item, GNOME_TYPE_CANVAS_GROUP);


//-- prototypes

static void on_machine_properties_dialog_destroy(GtkWidget *widget, gpointer user_data);
static void on_machine_preferences_dialog_destroy(GtkWidget *widget, gpointer user_data);

//-- helper methods

static void desaturate_pixbuf(GdkPixbuf *pixbuf) {
  guint x,y,w,h,rowstride,gray;
  guchar *p;

  g_assert(gdk_pixbuf_get_colorspace(pixbuf)==GDK_COLORSPACE_RGB);
  g_assert(gdk_pixbuf_get_bits_per_sample(pixbuf)==8);
  g_assert(gdk_pixbuf_get_has_alpha(pixbuf));
  g_assert(gdk_pixbuf_get_n_channels(pixbuf)==4);

  w=gdk_pixbuf_get_width(pixbuf);
  h=gdk_pixbuf_get_height(pixbuf);
  rowstride=gdk_pixbuf_get_rowstride(pixbuf)-(w*4);
  p=gdk_pixbuf_get_pixels(pixbuf);

  for(y=0;y<h;y++) {
    for(x=0;x<w;x++) {
      gray=((guint)p[0]+(guint)p[1]+(guint)p[2]);
      p[0]=(guchar)(((guint)p[0]+gray)>>2);
      p[1]=(guchar)(((guint)p[1]+gray)>>2);
      p[2]=(guchar)(((guint)p[2]+gray)>>2);
      p+=4;
    }
    p+=rowstride;
  }
}

static void alpha_pixbuf(GdkPixbuf *pixbuf) {
  guint x,y,w,h,rowstride;
  guchar *p;

  g_assert(gdk_pixbuf_get_colorspace(pixbuf)==GDK_COLORSPACE_RGB);
  g_assert(gdk_pixbuf_get_bits_per_sample(pixbuf)==8);
  g_assert(gdk_pixbuf_get_has_alpha(pixbuf));
  g_assert(gdk_pixbuf_get_n_channels(pixbuf)==4);

  w=gdk_pixbuf_get_width(pixbuf);
  h=gdk_pixbuf_get_height(pixbuf);
  rowstride=gdk_pixbuf_get_rowstride(pixbuf)-(w*4);
  p=gdk_pixbuf_get_pixels(pixbuf);

  for(y=0;y<h;y++) {
    for(x=0;x<w;x++) {
      p[3]>>=1;
      p+=4;
    }
    p+=rowstride;
  }
}

static void update_machine_graphics(BtMachineCanvasItem *self) {
  GdkPixbuf *pixbuf;
  gboolean has_parent;

  pixbuf=bt_ui_resources_get_machine_graphics_pixbuf_by_machine(self->priv->machine,self->priv->zoom);
  has_parent=(GST_OBJECT_PARENT(self->priv->machine)!=NULL);
  if(!has_parent || self->priv->dragging) {
    GdkPixbuf *tmp=gdk_pixbuf_copy(pixbuf);
    g_object_unref(pixbuf);
    pixbuf=tmp;
  }
  if(!has_parent) {
    desaturate_pixbuf(pixbuf);
  }
  if(self->priv->dragging) {
    alpha_pixbuf(pixbuf);
  }
  gnome_canvas_item_set(self->priv->box,
    "width",(gdouble)gdk_pixbuf_get_width(pixbuf),
    "height",(gdouble)gdk_pixbuf_get_height(pixbuf),
    /* this would make the icons blurred
    "width-set",TRUE,
    "height-set",TRUE,
    */
    "pixbuf",pixbuf,
    NULL);
  g_object_unref(pixbuf);
}

static void show_machine_properties_dialog(BtMachineCanvasItem *self) {
  if(!self->priv->properties_dialog) {

    self->priv->properties_dialog=GTK_WIDGET(bt_machine_properties_dialog_new(self->priv->machine));
    bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(self->priv->properties_dialog));
    GST_INFO("machine properties dialog opened");
    // remember open/closed state
    g_hash_table_insert(self->priv->properties,g_strdup("properties-shown"),g_strdup("1"));
    g_signal_connect(self->priv->properties_dialog,"destroy",G_CALLBACK(on_machine_properties_dialog_destroy),(gpointer)self);
  }
  gtk_window_present(GTK_WINDOW(self->priv->properties_dialog));
}

static void show_machine_preferences_dialog(BtMachineCanvasItem *self) {
  if(!self->priv->preferences_dialog) {
    self->priv->preferences_dialog=GTK_WIDGET(bt_machine_preferences_dialog_new(self->priv->machine));
    bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(self->priv->preferences_dialog));
    g_signal_connect(self->priv->preferences_dialog,"destroy",G_CALLBACK(on_machine_preferences_dialog_destroy),(gpointer)self);
  }
  gtk_window_present(GTK_WINDOW(self->priv->preferences_dialog));
}


//-- event handler

static void on_signal_analysis_dialog_destroy(GtkWidget *widget, gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("signal analysis dialog destroy occurred");
  self->priv->analysis_dialog=NULL;
  // remember open/closed state
  g_hash_table_remove(self->priv->properties,"analyzer-shown");
}


static void on_song_is_playing_notify(const BtSong *song,GParamSpec *arg,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  g_object_get((gpointer)song,"is-playing",&self->priv->is_playing,NULL);
  if(!self->priv->is_playing) {
    const gdouble h=MACHINE_VIEW_MACHINE_SIZE_Y;

    self->priv->last_output_level_value=0.0;
    gnome_canvas_item_set(self->priv->output_meter,
      "y1", h*0.6,
      NULL);
    self->priv->last_input_level_value=0.0;
    gnome_canvas_item_set(self->priv->input_meter,
      "y1", h*0.6,
      NULL);

  }
}

static gboolean on_delayed_idle_machine_level_change(gpointer user_data) {
  gconstpointer * const params=(gconstpointer *)user_data;
  BtMachineCanvasItem *self=(BtMachineCanvasItem *)params[0];
  GstMessage *message=(GstMessage *)params[1];

  if(self) {
    const GstStructure *structure=gst_message_get_structure(message);
    const GValue *l_cur;
    const gdouble h=MACHINE_VIEW_MACHINE_SIZE_Y;
    gdouble cur=0.0, val;
    guint i,size;

    g_mutex_lock(self->priv->lock);
    g_object_remove_weak_pointer((gpointer)self,(gpointer *)&params[0]);
    g_mutex_unlock(self->priv->lock);

    if(!self->priv->is_playing)
      goto done;

    l_cur=(GValue *)gst_structure_get_value(structure, "peak");
    size=gst_value_list_get_size(l_cur);
    for(i=0;i<size;i++) {
      cur+=g_value_get_double(gst_value_list_get_value(l_cur,i));
    }
    if(isinf(cur) || isnan(cur)) cur=LOW_VUMETER_VAL;
    else cur/=size;
    val=cur;
    if(val>0.0) val=0.0;
    val=val/LOW_VUMETER_VAL;
    if(val>1.0) val=1.0;
    if(GST_MESSAGE_SRC(message)==(GstObject *)(self->priv->output_level)) {
      gnome_canvas_item_set(self->priv->output_meter,
        "y1", h*0.05+(0.55*h*val),
        NULL);
    }
    if(GST_MESSAGE_SRC(message)==(GstObject *)(self->priv->input_level)) {
      gnome_canvas_item_set(self->priv->input_meter,
        "y1", h*0.05+(0.55*h*val),
        NULL);
    }
  }
done:
  gst_message_unref(message);
  g_slice_free1(2*sizeof(gconstpointer),params);
  return(FALSE);
}

static gboolean on_delayed_machine_level_change(GstClock *clock,GstClockTime time,GstClockID id,gpointer user_data) {
  // the callback is called from a clock thread
  if(GST_CLOCK_TIME_IS_VALID(time))
    g_idle_add(on_delayed_idle_machine_level_change,user_data);
  else {
    gconstpointer * const params=(gconstpointer *)user_data;
    GstMessage *message=(GstMessage *)params[1];
    gst_message_unref(message);
    g_slice_free1(2*sizeof(gconstpointer),user_data);
  }
  return(TRUE);
}

static void on_machine_level_change(GstBus * bus, GstMessage * message, gpointer user_data) {
  const GstStructure *structure=gst_message_get_structure(message);
  const GQuark name_id=gst_structure_get_name_id(structure);

  if(name_id==bus_msg_level_quark) {
    BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);
    GstElement *level=GST_ELEMENT(GST_MESSAGE_SRC(message));

    // check if its our level-meter
    if((level==self->priv->output_level) ||
      (level==self->priv->input_level)) {
      GstClockTime timestamp, duration;
      GstClockTime waittime=GST_CLOCK_TIME_NONE;

      if(gst_structure_get_clock_time (structure, "running-time", &timestamp) &&
        gst_structure_get_clock_time (structure, "duration", &duration)) {
        /* wait for middle of buffer */
        waittime=timestamp+duration/2;
      }
      else if(gst_structure_get_clock_time (structure, "endtime", &timestamp)) {
        /* level send endtime as stream_time and not as running_time */
        waittime=gst_segment_to_running_time(&GST_BASE_TRANSFORM(level)->segment, GST_FORMAT_TIME, timestamp);
      }
      if(GST_CLOCK_TIME_IS_VALID(waittime)) {
        // @todo: should we use param=g_slice_allow(2*sizeof(gconstpointer));
        // followed by g_slice_free(2*sizeof(gconstpointer),params)
        // we already require glib-2.10
        const GstStructure *structure=gst_message_get_structure(message);
        const GValue *l_cur;
        gdouble cur=0.0, val;
        guint i,size;
        gboolean changed=FALSE;

        //GST_WARNING("target %"GST_TIME_FORMAT" %"GST_TIME_FORMAT,
        //  GST_TIME_ARGS(endtime),GST_TIME_ARGS(waittime));

        // check the value here and skip updates if it hasn't changed
        l_cur=(GValue *)gst_structure_get_value(structure, "peak");
        size=gst_value_list_get_size(l_cur);
        for(i=0;i<size;i++) {
          cur+=g_value_get_double(gst_value_list_get_value(l_cur,i));
        }
        if(G_UNLIKELY(isinf(cur) || isnan(cur))) cur=LOW_VUMETER_VAL;
        else cur/=size;
        val=cur;
        if(val>0.0) val=0.0;
        val=val/LOW_VUMETER_VAL;
        if(val>1.0) val=1.0;
        if((level==self->priv->output_level) && (fabs(val-self->priv->last_output_level_value)>0.01)) {
          self->priv->last_output_level_value=val;
          changed=TRUE;
        }
        if((level==self->priv->input_level) && (fabs(val-self->priv->last_input_level_value)>0.01)) {
          self->priv->last_input_level_value=val;
          changed=TRUE;
        }
        if(changed) {
          gconstpointer *params=(gconstpointer *)g_slice_alloc(2*sizeof(gconstpointer));
          GstClockTime basetime=gst_element_get_base_time(level);
          GstClockID clock_id;

          // FIXME: we would rather send 'val' to avoid re-calculation (adding it to the structure would be a hack)
          params[0]=(gpointer)self;
          params[1]=(gpointer)gst_message_ref(message);
          g_mutex_lock(self->priv->lock);
          g_object_add_weak_pointer((gpointer)self,(gpointer *)&params[0]);
          g_mutex_unlock(self->priv->lock);
          clock_id=gst_clock_new_single_shot_id(self->priv->clock,waittime+basetime);
          if(gst_clock_id_wait_async(clock_id,on_delayed_machine_level_change,(gpointer)params)!=GST_CLOCK_OK) {
            gst_message_unref(message);
            g_slice_free1(2*sizeof(gconstpointer),params);
          }
          gst_clock_id_unref(clock_id);
        }
      }
    }
  }
}

static void on_machine_id_changed(BtMachine *machine, GParamSpec *arg, gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  if(self->priv->label) {
    gchar *id;

    g_object_get(self->priv->machine,"id",&id,NULL);
    gnome_canvas_item_set(self->priv->label,"text",id,NULL);
    g_free(id);
  }
}

static void on_machine_parent_changed(GstObject *object, GstObject *parent, gpointer user_data) {
  update_machine_graphics(BT_MACHINE_CANVAS_ITEM(user_data));
}

static void on_machine_state_changed(BtMachine *machine, GParamSpec *arg, gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);
  BtMachineState state;

  g_object_get(machine,"state",&state,NULL);
  GST_INFO(" new state is %d",state);

  update_machine_graphics(self);

  switch(state) {
    case BT_MACHINE_STATE_NORMAL:
      if(self->priv->menu_item_mute && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute))) {
        g_signal_handler_block(self->priv->menu_item_mute,self->priv->id_mute);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_mute,self->priv->id_mute);
      }
      if(self->priv->menu_item_solo && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo))) {
        g_signal_handler_block(self->priv->menu_item_solo,self->priv->id_solo);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_solo,self->priv->id_solo);
      }
      if(self->priv->menu_item_bypass && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass))) {
        g_signal_handler_block(self->priv->menu_item_bypass,self->priv->id_bypass);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_bypass,self->priv->id_bypass);
      }
      break;
    case BT_MACHINE_STATE_MUTE:
      if(self->priv->menu_item_mute && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute))) {
        g_signal_handler_block(self->priv->menu_item_mute,self->priv->id_mute);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute),TRUE);
        g_signal_handler_unblock(self->priv->menu_item_mute,self->priv->id_mute);
      }
      if(self->priv->menu_item_solo && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo))) {
        g_signal_handler_block(self->priv->menu_item_solo,self->priv->id_solo);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_solo,self->priv->id_solo);
      }
      if(self->priv->menu_item_bypass && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass))) {
        g_signal_handler_block(self->priv->menu_item_bypass,self->priv->id_bypass);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_bypass,self->priv->id_bypass);
      }
      break;
    case BT_MACHINE_STATE_SOLO:
      if(self->priv->menu_item_mute && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute))) {
        g_signal_handler_block(self->priv->menu_item_mute,self->priv->id_mute);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_mute,self->priv->id_mute);
      }
      if(self->priv->menu_item_solo && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo))) {
        g_signal_handler_block(self->priv->menu_item_solo,self->priv->id_solo);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo),TRUE);
        g_signal_handler_unblock(self->priv->menu_item_solo,self->priv->id_solo);
      }
      if(self->priv->menu_item_bypass && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass))) {
        g_signal_handler_block(self->priv->menu_item_bypass,self->priv->id_bypass);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_bypass,self->priv->id_bypass);
      }
      break;
    case BT_MACHINE_STATE_BYPASS:
      if(self->priv->menu_item_mute && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute))) {
        g_signal_handler_block(self->priv->menu_item_mute,self->priv->id_mute);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_mute),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_mute,self->priv->id_mute);
      }
      if(self->priv->menu_item_solo && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo))) {
        g_signal_handler_block(self->priv->menu_item_solo,self->priv->id_solo);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_solo),FALSE);
        g_signal_handler_unblock(self->priv->menu_item_solo,self->priv->id_solo);
      }
      if(self->priv->menu_item_bypass && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass))) {
        g_signal_handler_block(self->priv->menu_item_bypass,self->priv->id_bypass);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->priv->menu_item_bypass),TRUE);
        g_signal_handler_unblock(self->priv->menu_item_bypass,self->priv->id_bypass);
      }
      break;
    default:
      GST_WARNING("invalid machine state: %d",state);
      break;
  }
}

static void on_machine_properties_dialog_destroy(GtkWidget *widget, gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("machine properties dialog destroy occurred");
  self->priv->properties_dialog=NULL;
  // remember open/closed state
  g_hash_table_remove(self->priv->properties,"properties-shown");
}

static void on_machine_preferences_dialog_destroy(GtkWidget *widget, gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("machine preferences dialog destroy occurred");
  self->priv->preferences_dialog=NULL;
}

static void on_context_menu_mute_toggled(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("context_menu mute toggled");

  if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_MUTE,NULL);
  }
  else {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_context_menu_solo_toggled(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("context_menu solo toggled");

  if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_SOLO,NULL);
  }
  else {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_context_menu_bypass_toggled(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("context_menu bypass toggled");

  if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_BYPASS,NULL);
  }
  else {
    g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_context_menu_properties_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  show_machine_properties_dialog(self);
}

static void on_context_menu_preferences_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  show_machine_preferences_dialog(self);
}

static void on_context_menu_rename_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);
  GtkWidget *dialog;

  GST_INFO("context_menu rename event occurred");

  dialog=GTK_WIDGET(bt_machine_rename_dialog_new(self->priv->machine));
  bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(dialog));
  gtk_widget_show_all(dialog);
  if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
    bt_machine_rename_dialog_apply(BT_MACHINE_RENAME_DIALOG(dialog));
  }
  gtk_widget_destroy(dialog);
}

static void on_context_menu_delete_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);
  BtMainWindow *main_window;
  BtSong *song;
  BtSequence *sequence;
  gchar *msg=NULL,*id;
  gboolean has_patterns,is_connected,remove=FALSE;
  //BtWire *wire1,*wire2;

  GST_INFO("context_menu delete event occurred for machine : %p",self->priv->machine);

  g_object_get(self->priv->app,"main-window",&main_window,"song",&song,NULL);
  g_object_get(song,"sequence",&sequence,NULL);

  // don't ask if machine has no patterns and is not connected
  has_patterns=bt_machine_has_patterns(self->priv->machine);
  is_connected=self->priv->machine->src_wires || self->priv->machine->dst_wires;
  if(has_patterns) {
    BtPattern *pattern;
    gulong ix=0;
    gboolean is_unused=TRUE;

    // bah, freshly created generators always have one empty pattern called "00"
    // enough if the pattern is not used?
    do {
      pattern=bt_machine_get_pattern_by_index(self->priv->machine,ix++);
      if(pattern) {
        is_unused&=(!bt_sequence_is_pattern_used(sequence,pattern));
        g_object_unref(pattern);
      }
    } while(pattern && is_unused);
    if(is_unused) {
      // no patterns worth keeping
      has_patterns=FALSE;
    }
  }
  //GST_DEBUG("is-connected %d, has-patterns %d",is_connected,has_patterns);

  if((has_patterns || is_connected)) {
    g_object_get(self->priv->machine,"id",&id,NULL);
    msg=g_strdup_printf(_("Delete machine '%s'"),id);
    g_free(id);
  }
  else {
    // do not ask
    remove=TRUE;
  }

  if(remove || bt_dialog_question(main_window,_("Delete machine..."),msg,_("There is no complete undo for this yet."))) {
    GST_INFO("now removing machine : %p,ref_count=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
    bt_main_page_machines_delete_machine(self->priv->main_page_machines, self->priv->machine);
  }
  g_object_unref(sequence);
  g_object_unref(song);
  g_object_unref(main_window);
  g_free(msg);
}

static void on_context_menu_connect_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  g_signal_emit(self,signals[START_CONNECT],0);
}

static void on_context_menu_analysis_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  GST_INFO("context_menu analysis item selected");
  if(!self->priv->analysis_dialog) {
    self->priv->analysis_dialog=GTK_WIDGET(bt_signal_analysis_dialog_new(GST_BIN(self->priv->machine)));
    bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(self->priv->analysis_dialog));
    GST_INFO("analyzer dialog opened");
    // remember open/closed state
    g_hash_table_insert(self->priv->properties,g_strdup("analyzer-shown"),g_strdup("1"));
    g_signal_connect(self->priv->analysis_dialog,"destroy",G_CALLBACK(on_signal_analysis_dialog_destroy),(gpointer)self);
  }
  else {
    gtk_window_present(GTK_WINDOW(self->priv->analysis_dialog));
  }
}


static void on_context_menu_help_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);

  // show help for machine
  gtk_show_uri_simple(GTK_WIDGET(self->priv->main_page_machines),self->priv->help_uri);
}

static void on_context_menu_about_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(user_data);
  BtMainWindow *main_window;

  // show info about machine
  g_object_get(self->priv->app,"main-window",&main_window,NULL);
  bt_machine_action_about(self->priv->machine,main_window);
  g_object_unref(main_window);
}

//-- helper methods

#if 0
static gboolean bt_machine_canvas_item_is_over_state_switch(const BtMachineCanvasItem *self,GdkEvent *event) {
  GnomeCanvas *canvas;
  GnomeCanvasItem *ci,*pci;
  gboolean res=FALSE;

  g_object_get(self->priv->main_page_machines,"canvas",&canvas,NULL);
  if((ci=gnome_canvas_get_item_at(canvas,event->button.x,event->button.y))) {
    g_object_get(ci,"parent",&pci,NULL);
    //GST_DEBUG("ci=%p : self=%p, self->box=%p, self->state_switch=%p",ci,self,self->priv->box,self->priv->state_switch);
    if((ci==self->priv->state_switch)
      || (ci==self->priv->state_mute) || (pci==self->priv->state_mute)
      || (ci==self->priv->state_solo)
      || (ci==self->priv->state_bypass) || (pci==self->priv->state_bypass)) {
      res=TRUE;
    }
    g_object_unref(pci);
  }
  g_object_unref(canvas);
  return(res);
}
#endif

static gboolean bt_machine_canvas_item_init_context_menu(const BtMachineCanvasItem *self) {
  GtkWidget *menu_item,*label;

  self->priv->menu_item_mute=menu_item=gtk_check_menu_item_new_with_label(_("Mute"));
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  self->priv->id_mute=g_signal_connect(menu_item,"toggled",G_CALLBACK(on_context_menu_mute_toggled),(gpointer)self);
  if(BT_IS_SOURCE_MACHINE(self->priv->machine)) {
    self->priv->menu_item_solo=menu_item=gtk_check_menu_item_new_with_label(_("Solo"));
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);
    self->priv->id_solo=g_signal_connect(menu_item,"toggled",G_CALLBACK(on_context_menu_solo_toggled),(gpointer)self);
  }
  if(BT_IS_PROCESSOR_MACHINE(self->priv->machine)) {
    self->priv->menu_item_bypass=menu_item=gtk_check_menu_item_new_with_label(_("Bypass"));
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);
    self->priv->id_bypass=g_signal_connect(menu_item,"toggled",G_CALLBACK(on_context_menu_bypass_toggled),(gpointer)self);
  }

  menu_item=gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);

  menu_item=gtk_image_menu_item_new_from_stock(GTK_STOCK_PROPERTIES,NULL);  // dynamic part
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  // make this menu item bold (default)
  label=gtk_bin_get_child(GTK_BIN(menu_item));
  if(GTK_IS_LABEL(label)) {
    gchar *str=g_strconcat("<b>",gtk_label_get_label(GTK_LABEL(label)),"</b>",NULL);
    if(gtk_label_get_use_underline(GTK_LABEL(label))) {
      gtk_label_set_markup_with_mnemonic(GTK_LABEL(label),str);
    }
    else {
      gtk_label_set_markup(GTK_LABEL(label),str);
    }
    g_free(str);
  }
  else {
    GST_WARNING("expecting a GtkLabel as a first child");
  }
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_properties_activate),(gpointer)self);

  menu_item=gtk_image_menu_item_new_from_stock(GTK_STOCK_PREFERENCES,NULL); // static part
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_preferences_activate),(gpointer)self);

  menu_item=gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);

  menu_item=gtk_menu_item_new_with_label(_("Rename..."));
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_rename_activate),(gpointer)self);
  if(!BT_IS_SINK_MACHINE(self->priv->machine)) {
    menu_item=gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE,NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_delete_activate),(gpointer)self);

    menu_item=gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);

    menu_item=gtk_menu_item_new_with_label(_("Connect machines"));
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_connect_activate),(gpointer)self);
  } else {
    menu_item=gtk_menu_item_new_with_label(_("Signal Analysis..."));
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_analysis_activate),(gpointer)self);
  }

  menu_item=gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);

  menu_item=gtk_image_menu_item_new_from_stock(GTK_STOCK_HELP,NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  if(!self->priv->help_uri) {
    gtk_widget_set_sensitive(menu_item,FALSE);
  } else {
    g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_help_activate),(gpointer)self);
  }
  gtk_widget_show(menu_item);

  menu_item=gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT,NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_about_activate),(gpointer)self);

  return(TRUE);
}

//-- constructor methods

/**
 * bt_machine_canvas_item_new:
 * @main_page_machines: the machine page the new item belongs to
 * @machine: the machine for which a canvas item should be created
 * @xpos: the horizontal location
 * @ypos: the vertical location
 * @zoom: the zoom ratio
 *
 * Create a new instance
 *
 * Returns: the new instance or %NULL in case of an error
 */
BtMachineCanvasItem *bt_machine_canvas_item_new(const BtMainPageMachines *main_page_machines,BtMachine *machine,gdouble xpos,gdouble ypos,gdouble zoom) {
  BtMachineCanvasItem *self;
  GnomeCanvas *canvas;

  g_object_get((gpointer)main_page_machines,"canvas",&canvas,NULL);

  self=BT_MACHINE_CANVAS_ITEM(gnome_canvas_item_new(gnome_canvas_root(canvas),
                            BT_TYPE_MACHINE_CANVAS_ITEM,
                            "machines-page",main_page_machines,
                            "machine", machine,
                            "x", xpos,
                            "y", ypos,
                            "zoom", zoom,
                            NULL));

  //GST_INFO("machine canvas item added, ref-ct=%d",G_OBJECT_REF_COUNT(self));

  g_object_unref(canvas);
  return(self);
}

//-- methods

/**
 * bt_machine_show_properties_dialog:
 * @machine: machine to show the dialog for
 *
 * Shows the machine properties dialog.
 */
void bt_machine_show_properties_dialog(BtMachine *machine) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(g_object_get_qdata((GObject *)machine,machine_canvas_item_quark));

  show_machine_properties_dialog(self);
}

/**
 * bt_machine_show_preferences_dialog:
 * @machine: machine to show the dialog for
 *
 * Shows the machine preferences dialog.
 */
void bt_machine_show_preferences_dialog(BtMachine *machine) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(g_object_get_qdata((GObject *)machine,machine_canvas_item_quark));

  show_machine_preferences_dialog(self);
}

//-- wrapper

//-- class internals

static void bt_machine_canvas_item_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  BtMachineCanvasItem *self = BT_MACHINE_CANVAS_ITEM(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_CANVAS_ITEM_MACHINES_PAGE: {
      g_value_set_object(value, self->priv->main_page_machines);
    } break;
    case MACHINE_CANVAS_ITEM_MACHINE: {
      GST_INFO("getting machine : %p,ref_count=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
      g_value_set_object(value, self->priv->machine);
      //GST_INFO("... : %p,ref_count=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
    } break;
    case MACHINE_CANVAS_ITEM_ZOOM: {
      g_value_set_double(value, self->priv->zoom);
    } break;
    default: {
       G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_machine_canvas_item_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  BtMachineCanvasItem *self = BT_MACHINE_CANVAS_ITEM(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_CANVAS_ITEM_MACHINES_PAGE: {
      g_object_try_weak_unref(self->priv->main_page_machines);
      self->priv->main_page_machines = BT_MAIN_PAGE_MACHINES(g_value_get_object(value));
      g_object_try_weak_ref(self->priv->main_page_machines);
      //GST_DEBUG("set the main_page_machines for machine_canvas_item: %p",self->priv->main_page_machines);
    } break;
    case MACHINE_CANVAS_ITEM_MACHINE: {
      g_object_try_unref(self->priv->machine);
      self->priv->machine = BT_MACHINE(g_value_dup_object(value));
      if(self->priv->machine) {
        BtSong *song;
        GstElement *element;
        GstBin *bin;
        GstBus *bus;

        GST_INFO("set the  machine %p,machine->ref_ct=%d for new canvas item",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
        g_object_set_qdata((GObject *)self->priv->machine,machine_canvas_item_quark,(gpointer)self);
        g_object_get(self->priv->machine,"properties",&self->priv->properties,"machine",&element,NULL);

#if GST_CHECK_VERSION(0,10,31)
        self->priv->help_uri=gst_element_factory_get_documentation_uri(gst_element_get_factory(element));
#else
        if(GSTBT_IS_HELP(element))
          g_object_get(element,"documentation-uri",&self->priv->help_uri,NULL);
        else
          self->priv->help_uri=NULL;
#endif
        gst_object_unref(element);

        //GST_DEBUG("set the machine for machine_canvas_item: %p, properties: %p",self->priv->machine,self->priv->properties);
        bt_machine_canvas_item_init_context_menu(self);
        g_signal_connect(self->priv->machine, "notify::id", G_CALLBACK(on_machine_id_changed), (gpointer)self);
        g_signal_connect(self->priv->machine, "notify::state", G_CALLBACK(on_machine_state_changed), (gpointer)self);
        g_signal_connect(self->priv->machine, "parent-set", G_CALLBACK(on_machine_parent_changed), (gpointer)self);
        g_signal_connect(self->priv->machine, "parent-unset", G_CALLBACK(on_machine_parent_changed), (gpointer)self);

        g_object_get(self->priv->app,"song",&song,NULL);
        g_signal_connect(song,"notify::is-playing",G_CALLBACK(on_song_is_playing_notify),(gpointer)self);
        g_object_get(song,"bin", &bin,NULL);
        bus=gst_element_get_bus(GST_ELEMENT(bin));
        g_signal_connect(bus, "message::element", G_CALLBACK(on_machine_level_change), (gpointer)self);
        gst_object_unref(bus);
        self->priv->clock=gst_pipeline_get_clock (GST_PIPELINE(bin));
        gst_object_unref(bin);
        g_object_unref(song);

        if(!BT_IS_SINK_MACHINE(self->priv->machine)) {
          if(bt_machine_enable_output_post_level(self->priv->machine)) {
            g_object_get(self->priv->machine,"output-post-level",&self->priv->output_level,NULL);
            g_object_try_weak_ref(self->priv->output_level);
            gst_object_unref(self->priv->output_level);
          }
          else {
            GST_INFO("enabling output level for machine failed");
          }
        }
        if(!BT_IS_SOURCE_MACHINE(self->priv->machine)) {
          if(bt_machine_enable_input_pre_level(self->priv->machine)) {
            g_object_get(self->priv->machine,"input-pre-level",&self->priv->input_level,NULL);
            g_object_try_weak_ref(self->priv->input_level);
            gst_object_unref(self->priv->input_level);
          }
          else {
            GST_INFO("enabling input level for machine failed");
          }
        }
      }
    } break;
    case MACHINE_CANVAS_ITEM_ZOOM: {
      self->priv->zoom=g_value_get_double(value);
      GST_DEBUG("set the zoom for machine_canvas_item: %f",self->priv->zoom);
      if(self->priv->label) {
        gnome_canvas_item_set(self->priv->label,"size-points",MACHINE_VIEW_FONT_SIZE*self->priv->zoom,NULL);
      }
      /* reload the svg icons, we do this to keep them sharp */
      if(self->priv->box) {
        update_machine_graphics(self);
      }
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_machine_canvas_item_dispose(GObject *object) {
  BtMachineCanvasItem *self = BT_MACHINE_CANVAS_ITEM(object);
  BtSong *song;
  GstBin *bin;
  GstBus *bus;

  return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG("!!!! self=%p",self);

  GST_DEBUG("machine: %p,ref_count %d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));

  g_signal_handlers_disconnect_matched(self->priv->machine,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,(gpointer)self);

  g_object_get(self->priv->app,"song",&song,"bin",&bin,NULL);
  if(song) {
    g_signal_handlers_disconnect_matched(song,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_song_is_playing_notify,NULL);
    g_object_unref(song);
  }
  bus=gst_element_get_bus(GST_ELEMENT(bin));
  g_signal_handlers_disconnect_matched(bus, G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_level_change,NULL);
  gst_object_unref(bus);
  gst_object_unref(bin);

  GST_DEBUG("  signal disconected");

  g_object_try_weak_unref(self->priv->output_level);
  g_object_try_weak_unref(self->priv->input_level);
  g_object_try_unref(self->priv->machine);
  g_object_try_weak_unref(self->priv->main_page_machines);
  if(self->priv->clock) gst_object_unref(self->priv->clock);
  g_object_unref(self->priv->app);

  GST_DEBUG("  unrefing done");

  if(self->priv->properties_dialog) {
    gtk_widget_destroy(self->priv->properties_dialog);
  }
  if(self->priv->preferences_dialog) {
    gtk_widget_destroy(self->priv->preferences_dialog);
  }
  GST_DEBUG("  destroying dialogs done");

  gdk_cursor_unref(self->priv->drag_cursor);

  gtk_widget_destroy(GTK_WIDGET(self->priv->context_menu));
  g_object_try_unref(self->priv->context_menu);
  GST_DEBUG("  destroying done, machine: %p,ref_count %d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));

  GST_DEBUG("  chaining up");
  G_OBJECT_CLASS(bt_machine_canvas_item_parent_class)->dispose(object);
  GST_DEBUG("  done");
}

static void bt_machine_canvas_item_finalize(GObject *object) {
  BtMachineCanvasItem *self = BT_MACHINE_CANVAS_ITEM(object);

  GST_DEBUG("!!!! self=%p",self);
  g_mutex_free(self->priv->lock);

  G_OBJECT_CLASS(bt_machine_canvas_item_parent_class)->finalize(object);
  GST_DEBUG("  done");
}

/*
 * bt_machine_canvas_item_realize:
 *
 * draw something that looks a bit like a buzz-machine
 */
static void bt_machine_canvas_item_realize(GnomeCanvasItem *citem) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(citem);
  gdouble w=MACHINE_VIEW_MACHINE_SIZE_X,h=MACHINE_VIEW_MACHINE_SIZE_Y;
  gdouble fh=MACHINE_VIEW_FONT_SIZE;
  gchar *id,*prop;

  GNOME_CANVAS_ITEM_CLASS(bt_machine_canvas_item_parent_class)->realize(citem);

  //GST_DEBUG("realize for machine occurred, machine=%p",self->priv->machine);

  g_object_get(self->priv->machine,"id",&id,NULL);

  // add machine components
  // the body
  self->priv->box=gnome_canvas_item_new (GNOME_CANVAS_GROUP(citem),
                           GNOME_TYPE_CANVAS_PIXBUF,
                           "anchor", GTK_ANCHOR_CENTER,
                           "x",0.0,
                           "y",-(w-h),
                           "width-in-pixels",TRUE,
                           "height-in-pixels",TRUE,
                           NULL);
  update_machine_graphics(self);

  // the name label
  self->priv->label=gnome_canvas_item_new(GNOME_CANVAS_GROUP(citem),
                           GNOME_TYPE_CANVAS_TEXT,
                           /* can we use the x-anchor to position left ? */
                           /*"x-offset",-(w*0.1),*/
                           "x", 0.0,
                           "y", -(h*0.65)+fh,
                           "justification", GTK_JUSTIFY_LEFT,
                           /* test if this ensures equal sizes among systems,
                            * maybe we should leave it blank */
                           "font", "sans", /* "helvetica" */
                           "size-points", fh*self->priv->zoom,
                           "size-set", TRUE,
                           "text", id,
                           "fill-color", "black",
                           "clip", TRUE,
                           "clip-width",(w+w)*0.70,
                           "clip-height",h+h,
                           NULL);

  // the input volume level meter
  guint32 border_color=0x6666667F;
  //if(!BT_IS_SOURCE_MACHINE(self->priv->machine)) {
    self->priv->input_meter=gnome_canvas_item_new(GNOME_CANVAS_GROUP(citem),
                           GNOME_TYPE_CANVAS_RECT,
                           "x1", -w*0.65,
                           //"y1", +h*0.05,
                           "y1", +h*0.6,
                           "x2", -w*0.55,
                           "y2", +h*0.6,
                           "fill-color", "gray40",
                           "outline_color-rgba", border_color,
                           "width-pixels", 2,
                           NULL);
  //}
  // the output volume level meter
  //if(!BT_IS_SINK_MACHINE(self->priv->machine)) {
    self->priv->output_meter=gnome_canvas_item_new(GNOME_CANVAS_GROUP(citem),
                           GNOME_TYPE_CANVAS_RECT,
                           "x1",  w*0.6,
                           //"y1", +h*0.05,
                           "y1", +h*0.6,
                           "x2",  w*0.7,
                           "y2", +h*0.6,
                           "fill-color", "gray40",
                           "outline_color-rgba", border_color,
                           "width-pixels", 2,
                           NULL);
  //}
  g_free(id);

  prop=(gchar *)g_hash_table_lookup(self->priv->properties,"properties-shown");
  if(prop && prop[0]=='1' && prop[1]=='\0') {
    self->priv->properties_dialog=GTK_WIDGET(bt_machine_properties_dialog_new(self->priv->machine));
    bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(self->priv->properties_dialog));
    g_signal_connect(self->priv->properties_dialog,"destroy",G_CALLBACK(on_machine_properties_dialog_destroy),(gpointer)self);
  }
  prop=(gchar *)g_hash_table_lookup(self->priv->properties,"analyzer-shown");
  if(prop && prop[0]=='1' && prop[1]=='\0') {
    if((self->priv->analysis_dialog=GTK_WIDGET(bt_signal_analysis_dialog_new(GST_BIN(self->priv->machine))))) {
      bt_edit_application_attach_child_window(self->priv->app,GTK_WINDOW(self->priv->analysis_dialog));
      g_signal_connect(self->priv->analysis_dialog,"destroy",G_CALLBACK(on_signal_analysis_dialog_destroy),(gpointer)self);
    }
  }

  //item->realized = TRUE;
}

static gboolean bt_machine_canvas_item_event(GnomeCanvasItem *citem, GdkEvent *event) {
  BtMachineCanvasItem *self=BT_MACHINE_CANVAS_ITEM(citem);
  gboolean res=FALSE;
  gdouble dx, dy, px, py;
  gchar str[G_ASCII_DTOSTR_BUF_SIZE];

  //GST_INFO("event for machine occurred");

  switch (event->type) {
    case GDK_2BUTTON_PRESS:
      GST_DEBUG("GDK_2BUTTON_RELEASE: %d, 0x%x",event->button.button,event->button.state);
      show_machine_properties_dialog(self);
      res=TRUE;
      break;
    case GDK_BUTTON_PRESS:
      GST_DEBUG("GDK_BUTTON_PRESS: %d, 0x%x",event->button.button,event->button.state);
      if((event->button.button==1) && !(event->button.state&GDK_SHIFT_MASK)) {
#if 0
        if(!bt_machine_canvas_item_is_over_state_switch(self,event)) {
#endif
          // dragx/y coords are world coords of button press
          self->priv->dragx=event->button.x;
          self->priv->dragy=event->button.y;
          // set some flags
          self->priv->dragging=TRUE;
          self->priv->moved=FALSE;
#if 0
        }
        else {
          self->priv->switching=TRUE;
        }
#endif
        res=TRUE;
      }
      else if(event->button.button==3) {
        // show context menu
        gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
        res=TRUE;
      }
      break;
    case GDK_MOTION_NOTIFY:
      //GST_DEBUG("GDK_MOTION_NOTIFY: %f,%f",event->button.x,event->button.y);
      if(self->priv->dragging) {
        if(!self->priv->moved) {
          update_machine_graphics(self);
          gnome_canvas_item_raise_to_top(citem);
          gnome_canvas_item_grab(citem, GDK_POINTER_MOTION_MASK |
                                /* GDK_ENTER_NOTIFY_MASK | */
                                /* GDK_LEAVE_NOTIFY_MASK | */
          GDK_BUTTON_RELEASE_MASK, self->priv->drag_cursor, event->button.time);
        }
        dx=event->button.x-self->priv->dragx;
        dy=event->button.y-self->priv->dragy;
        gnome_canvas_item_move(citem, dx, dy);
        g_signal_emit(citem,signals[POSITION_CHANGED],0);
        self->priv->dragx=event->button.x;
        self->priv->dragy=event->button.y;
        self->priv->moved=TRUE;
        res=TRUE;
      }
      break;
    case GDK_BUTTON_RELEASE:
      GST_DEBUG("GDK_BUTTON_RELEASE: %d",event->button.button);
      if(self->priv->dragging) {
        self->priv->dragging=FALSE;
        if(self->priv->moved) {
          // change position properties of the machines
          g_object_get(citem,"x",&px,"y",&py,NULL);
          px/=MACHINE_VIEW_ZOOM_X;
          py/=MACHINE_VIEW_ZOOM_Y;
          g_hash_table_insert(self->priv->properties,g_strdup("xpos"),g_strdup(g_ascii_dtostr(str,G_ASCII_DTOSTR_BUF_SIZE,px)));
          g_hash_table_insert(self->priv->properties,g_strdup("ypos"),g_strdup(g_ascii_dtostr(str,G_ASCII_DTOSTR_BUF_SIZE,py)));
          g_signal_emit(citem,signals[POSITION_CHANGED],0);
          gnome_canvas_item_ungrab(citem,event->button.time);
          update_machine_graphics(self);
        }
        res=TRUE;
      }
      else
#if 0
      if(self->priv->switching) {
        self->priv->switching=FALSE;
        // still over mode switch
        if(bt_machine_canvas_item_is_over_state_switch(self,event)) {
          guint modifier=(gulong)event->button.state&gtk_accelerator_get_default_mod_mask();
          //gulong modifier=(gulong)event->button.state&(GDK_CONTROL_MASK|GDK_MOD4_MASK);
          GST_DEBUG("  mode quad state switch, key_modifier is: 0x%x + mask: 0x%x -> 0x%x",event->button.state,(GDK_CONTROL_MASK|GDK_MOD4_MASK),modifier);
          switch(modifier) {
            case 0:
              g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
              break;
            case GDK_CONTROL_MASK:
              g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_MUTE,NULL);
              break;
            case GDK_MOD4_MASK:
              g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_SOLO,NULL);
              break;
            case GDK_CONTROL_MASK|GDK_MOD4_MASK:
              g_object_set(self->priv->machine,"state",BT_MACHINE_STATE_BYPASS,NULL);
              break;
          }
        }
      }
#endif
      break;
    case GDK_KEY_RELEASE:
      GST_DEBUG("GDK_KEY_RELEASE: %d",event->key.keyval);
      switch(event->key.keyval) {
        case GDK_Menu:
          // show context menu
          gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
          res=TRUE;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  /* we don't want the click falling through to the parent canvas item, if we have handled it */
  //if(res) g_signal_stop_emission_by_name(citem->canvas,"event-after");
  if(!res) {
    if(GNOME_CANVAS_ITEM_CLASS(bt_machine_canvas_item_parent_class)->event) {
      res=GNOME_CANVAS_ITEM_CLASS(bt_machine_canvas_item_parent_class)->event(citem,event);
    }
  }
  //GST_INFO("event for machine occurred : %d",res);
  return res;
}

static void bt_machine_canvas_item_init(BtMachineCanvasItem *self) {
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, BT_TYPE_MACHINE_CANVAS_ITEM, BtMachineCanvasItemPrivate);
  GST_DEBUG("!!!! self=%p",self);
  self->priv->app = bt_edit_application_new();

  // generate the context menu
  self->priv->context_menu=GTK_MENU(g_object_ref_sink(gtk_menu_new()));
  // the menu-items are generated in bt_machine_canvas_item_init_context_menu()

  // the cursor for dragging
  self->priv->drag_cursor=gdk_cursor_new(GDK_FLEUR);

  self->priv->zoom=1.0;

  self->priv->lock=g_mutex_new ();
}

static void bt_machine_canvas_item_class_init(BtMachineCanvasItemClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GnomeCanvasItemClass *citem_class=GNOME_CANVAS_ITEM_CLASS(klass);

  bus_msg_level_quark=g_quark_from_static_string("level");
  machine_canvas_item_quark=g_quark_from_static_string("machine-canvas-item");

  g_type_class_add_private(klass,sizeof(BtMachineCanvasItemPrivate));

  gobject_class->set_property = bt_machine_canvas_item_set_property;
  gobject_class->get_property = bt_machine_canvas_item_get_property;
  gobject_class->dispose      = bt_machine_canvas_item_dispose;
  gobject_class->finalize     = bt_machine_canvas_item_finalize;

  citem_class->realize        = bt_machine_canvas_item_realize;
  citem_class->event          = bt_machine_canvas_item_event;

  /**
   * BtMachineCanvasItem::position-changed
   * @self: the machine-canvas-item object that emitted the signal
   *
   * Signals that item has been moved around. The new position can be read from
   * the canvas item.
   */
  signals[POSITION_CHANGED] = g_signal_new("position-changed",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        0,
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, // return type
                                        0 // n_params
                                        );
  /**
   * BtMachineCanvasItem::start-connect
   * @self: the machine-canvas-item object that emitted the signal
   *
   * Signals that a connect should be made starting from this machine.
   */
  signals[START_CONNECT] = g_signal_new("start-connect",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        0,
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, // return type
                                        0 // n_params
                                        );

  g_object_class_install_property(gobject_class,MACHINE_CANVAS_ITEM_MACHINES_PAGE,
                                  g_param_spec_object("machines-page",
                                     "machines-page contruct prop",
                                     "Set application object, the window belongs to",
                                     BT_TYPE_MAIN_PAGE_MACHINES, /* object type */
#ifndef GNOME_CANVAS_BROKEN_PROPERTIES
                                     G_PARAM_CONSTRUCT_ONLY |
#endif
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_CANVAS_ITEM_MACHINE,
                                  g_param_spec_object("machine",
                                     "machine contruct prop",
                                     "Set machine object, the item belongs to",
                                     BT_TYPE_MACHINE, /* object type */
#ifndef GNOME_CANVAS_BROKEN_PROPERTIES
                                     G_PARAM_CONSTRUCT_ONLY |
#endif
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_CANVAS_ITEM_ZOOM,
                                  g_param_spec_double("zoom",
                                     "zoom prop",
                                     "Set zoom ratio for the machine item",
                                     0.0,
                                     100.0,
                                     1.0,
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));
}

