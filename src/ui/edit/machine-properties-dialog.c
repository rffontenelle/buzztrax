/* $Id: machine-properties-dialog.c,v 1.4 2005-01-07 15:02:09 ensonic Exp $
 * class for the machine properties dialog
 */

#define BT_EDIT
#define BT_MACHINE_PROPERTIES_DIALOG_C

#include "bt-edit.h"

//-- property ids

enum {
  MACHINE_PROPERTIES_DIALOG_APP=1,
  MACHINE_PROPERTIES_DIALOG_MACHINE
};

struct _BtMachinePropertiesDialogPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* the application */
  BtEditApplication *app;

  /* the underlying machine */
  BtMachine *machine;
};

static GtkDialogClass *parent_class=NULL;

//-- event handler

//-- helper methods

static gboolean bt_machine_properties_dialog_init_ui(const BtMachinePropertiesDialog *self) {
	BtMainWindow *main_window;
  GtkWidget *box,*label,*widget,*table,*scrolled_window;
	gchar *id;
	GdkPixbuf *window_icon=NULL;
	gulong i,global_params;
	GstDParam *dparam;
	GParamSpec *property;
	GType param_type;

	g_object_get(self->priv->app,"main-window",&main_window,NULL);
	gtk_window_set_transient_for(GTK_WINDOW(self),GTK_WINDOW(main_window));

  // create and set window icon
	if(BT_IS_SOURCE_MACHINE(self->priv->machine)) {
		window_icon=gdk_pixbuf_new_from_filename("menu_source_machine.png");
	}
	else if(BT_IS_PROCESSOR_MACHINE(self->priv->machine)) {
		window_icon=gdk_pixbuf_new_from_filename("menu_processor_machine.png");
  }
  else if(BT_IS_SINK_MACHINE(self->priv->machine)) {
		window_icon=gdk_pixbuf_new_from_filename("menu_sink_machine.png");
  }
  if(window_icon) {
    gtk_window_set_icon(GTK_WINDOW(self),window_icon);
  }
  
	// leave the choice of with to gtk
	gtk_window_set_default_size(GTK_WINDOW(self),-1,200);
	// set a title
	g_object_get(self->priv->machine,"id",&id,"global-params",&global_params,NULL);
  gtk_window_set_title(GTK_WINDOW(self),g_strdup_printf(_("%s properties"),id));
	g_free(id);
    
  // add widgets to the dialog content area
  box=gtk_vbox_new(FALSE,12);
  //gtk_container_set_border_width(GTK_CONTAINER(box),6);
	
	// @todo add preset controls (combobox, edit button, copy, random, help)
	gtk_container_add(GTK_CONTAINER(box),gtk_label_new("no preset selection here yet"));

	// add separator
  gtk_container_add(GTK_CONTAINER(box),gtk_hseparator_new());
	
	if(global_params/*+voices*voice_params*/) {
		GST_INFO("machine has %d properties",global_params);
		// machine controls inside a scrolled window
  	scrolled_window=gtk_scrolled_window_new(NULL,NULL);
  	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
  	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),GTK_SHADOW_NONE);
		// add machine controls into the table
		table=gtk_table_new(/*rows=*/global_params+1,/*columns=*/2,/*homogenous=*/FALSE);
		for(i=0;i<global_params;i++) {
			dparam=bt_machine_get_global_dparam(self->priv->machine,i);
			property=GST_DPARAM_PARAM_SPEC(dparam);
			// get name
			label=gtk_label_new(GST_DPARAM_NAME(dparam));
			gtk_misc_set_alignment(GTK_MISC(label),1.0,0.5);
			gtk_table_attach(GTK_TABLE(table),label, 0, 1, i, i+1, GTK_FILL|GTK_EXPAND,GTK_SHRINK, 2,1);
			// @todo choose proper widgets
			param_type=bt_machine_get_global_dparam_type(self->priv->machine,i);
			if(param_type==G_TYPE_PARAM_STRING) {
				widget=gtk_label_new("string");
			}
			else {
				gchar *str=g_strdup_printf("unhandled type \"%s\"",G_PARAM_SPEC_TYPE_NAME(property));
				widget=gtk_label_new(str);g_free(str);
			}
			//gtk_tooltips_set_tip(GTK_TOOLTIPS(tips),widget,g_param_spec_get_blurb(property),NULL);
			gtk_table_attach(GTK_TABLE(table),widget, 1, 2, i, i+1, GTK_FILL|GTK_EXPAND,GTK_SHRINK, 2,1);
		}
		gtk_table_attach(GTK_TABLE(table),gtk_label_new(" "), 0, 2, i, i+1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND, 2,1);
		gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),table);
		gtk_container_add(GTK_CONTAINER(box),scrolled_window);
	}
	else {
		gtk_container_add(GTK_CONTAINER(box),gtk_label_new(_("machine has no params")));
	}
  gtk_container_add(GTK_CONTAINER(self),box);
	
	g_object_try_unref(main_window);
  return(TRUE);
}

//-- constructor methods

/**
 * bt_machine_properties_dialog_new:
 * @app: the application the dialog belongs to
 *
 * Create a new instance
 *
 * Returns: the new instance or NULL in case of an error
 */
BtMachinePropertiesDialog *bt_machine_properties_dialog_new(const BtEditApplication *app,const BtMachine *machine) {
  BtMachinePropertiesDialog *self;

  if(!(self=BT_MACHINE_PROPERTIES_DIALOG(g_object_new(BT_TYPE_MACHINE_PROPERTIES_DIALOG,"app",app,"machine",machine,NULL)))) {
    goto Error;
  }
  // generate UI
  if(!bt_machine_properties_dialog_init_ui(self)) {
    goto Error;
  }
  gtk_widget_show_all(GTK_WIDGET(self));
  return(self);
Error:
  g_object_try_unref(self);
  return(NULL);
}

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_machine_properties_dialog_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtMachinePropertiesDialog *self = BT_MACHINE_PROPERTIES_DIALOG(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_PROPERTIES_DIALOG_APP: {
      g_value_set_object(value, self->priv->app);
    } break;
    case MACHINE_PROPERTIES_DIALOG_MACHINE: {
      g_value_set_object(value, self->priv->machine);
    } break;
    default: {
 			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_machine_properties_dialog_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtMachinePropertiesDialog *self = BT_MACHINE_PROPERTIES_DIALOG(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_PROPERTIES_DIALOG_APP: {
      g_object_try_unref(self->priv->app);
      self->priv->app = g_object_try_ref(g_value_get_object(value));
      //GST_DEBUG("set the app for settings_dialog: %p",self->priv->app);
    } break;
    case MACHINE_PROPERTIES_DIALOG_MACHINE: {
      g_object_try_unref(self->priv->machine);
      self->priv->machine = g_object_try_ref(g_value_get_object(value));
    } break;
    default: {
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_machine_properties_dialog_dispose(GObject *object) {
  BtMachinePropertiesDialog *self = BT_MACHINE_PROPERTIES_DIALOG(object);
	return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG("!!!! self=%p",self);
  g_object_try_unref(self->priv->app);
  g_object_try_unref(self->priv->machine);

  if(G_OBJECT_CLASS(parent_class)->dispose) {
    (G_OBJECT_CLASS(parent_class)->dispose)(object);
  }
}

static void bt_machine_properties_dialog_finalize(GObject *object) {
  BtMachinePropertiesDialog *self = BT_MACHINE_PROPERTIES_DIALOG(object);

  GST_DEBUG("!!!! self=%p",self);
  g_free(self->priv);

  if(G_OBJECT_CLASS(parent_class)->finalize) {
    (G_OBJECT_CLASS(parent_class)->finalize)(object);
  }
}

static void bt_machine_properties_dialog_init(GTypeInstance *instance, gpointer g_class) {
  BtMachinePropertiesDialog *self = BT_MACHINE_PROPERTIES_DIALOG(instance);
  self->priv = g_new0(BtMachinePropertiesDialogPrivate,1);
  self->priv->dispose_has_run = FALSE;
}

static void bt_machine_properties_dialog_class_init(BtMachinePropertiesDialogClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GtkObjectClass *gtkobject_class = GTK_OBJECT_CLASS(klass);

  parent_class=g_type_class_ref(GTK_TYPE_DIALOG);
  
  gobject_class->set_property = bt_machine_properties_dialog_set_property;
  gobject_class->get_property = bt_machine_properties_dialog_get_property;
  gobject_class->dispose      = bt_machine_properties_dialog_dispose;
  gobject_class->finalize     = bt_machine_properties_dialog_finalize;

  g_object_class_install_property(gobject_class,MACHINE_PROPERTIES_DIALOG_APP,
                                  g_param_spec_object("app",
                                     "app construct prop",
                                     "Set application object, the dialog belongs to",
                                     BT_TYPE_EDIT_APPLICATION, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_PROPERTIES_DIALOG_MACHINE,
                                  g_param_spec_object("machine",
                                     "machine construct prop",
                                     "Set machine object, the dialog handles",
                                     BT_TYPE_MACHINE, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

}

GType bt_machine_properties_dialog_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtMachinePropertiesDialogClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_machine_properties_dialog_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtMachinePropertiesDialog),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_machine_properties_dialog_init, // instance_init
    };
		type = g_type_register_static(GTK_TYPE_WINDOW,"BtMachinePropertiesDialog",&info,0);
  }
  return type;
}
