/* $Id: machine.c,v 1.31 2004-09-26 01:50:08 ensonic Exp $
 * base class for a machine
 */
 
#define BT_CORE
#define BT_MACHINE_C

#include <libbtcore/core.h>

enum {
  MACHINE_SONG=1,
	MACHINE_ID,
	MACHINE_PLUGIN_NAME,
  MACHINE_VOICES,
  MACHINE_GLOBAL_PARAMS,
  MACHINE_VOICE_PARAMS
};

struct _BtMachinePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
	
	/* the song the machine belongs to */
	BtSong *song;
	/* the main gstreamer container element */
	GstBin *bin;

	/* the id, we can use to lookup the machine */
	gchar *id;
	/* the name of the gst-plugin the machine is using */
	gchar *plugin_name;

  /* the number of voices the machine provides */
  glong voices;
  /* the number of dynamic params the machine provides per instance */
  glong global_params;
  /* the number of dynamic params the machine provides per instance and voice */
  glong voice_params;

  /* gstreamer dparams */
  GstDParamManager *dparam_manager;
  GType *global_types;
  GType *voice_types; 
	GstDParam **global_dparams;
	GstDParam **voice_dparams;  // @todo we need these for every voice

  GList *patterns;	// each entry points to BtPattern
  
  /* the element to analyse the current input
   * could be placed right behind the internal adder
   * (see wire.c)
   */
  GstElement *input_level;
  /* the element to analyse the current output
   * could be placed right before the internal spreader
   * (see wire.c)
   */
  GstElement *output_level;
  
  /* public fields are
	GstElement *machine,*adder,*spreader;
	GstElement *dst_elem,*src_elem;
  */
};

//-- helper methods

// @todo ideally this would be a protected method, but how to do this in 'C' ?
gboolean bt_machine_init_gst_element(BtMachine *self) {

  g_assert(self->machine==NULL);
  g_assert(self->private->id);
  g_assert(self->private->plugin_name);

  self->machine=gst_element_factory_make(self->private->plugin_name,self->private->id);
  if(!self->machine) {
    GST_ERROR("  failed to instantiate machine \"%s\"",self->private->plugin_name);
    return(FALSE);
  }
  // there is no adder or spreader in use by default
  self->dst_elem=self->src_elem=self->machine;
  GST_DEBUG("  instantiated machine \"%s\"",self->private->plugin_name);
  if((self->private->dparam_manager=gst_dpman_get_manager(self->machine))) {
    GParamSpec **specs;
    GstDParam **dparam;
    guint i;

    // setting param mode. Only synchronized is currently supported
    gst_dpman_set_mode(self->private->dparam_manager, "synchronous");
    GST_DEBUG("  machine \"%s\" supports dparams",self->private->plugin_name);
    
    // manage dparams
    specs=gst_dpman_list_dparam_specs(self->private->dparam_manager);
    // count the specs
    for(i=0;specs[i];i++);
    // right now, we have no voice params
    self->private->global_params=i;
    self->private->global_dparams=(GstDParam **)g_new0(gpointer,self->private->global_params);
    self->private->global_types  =(GType *     )g_new0(GType   ,self->private->global_params);
    // iterate over all dparam
    for(i=0,dparam=self->private->global_dparams;specs[i];i++,dparam++) {
      self->private->global_types[i]=G_PARAM_SPEC_VALUE_TYPE(specs[i]);
      self->private->global_dparams[i]=gst_dparam_new(self->private->global_types[i]);
      gst_dpman_attach_dparam(self->private->dparam_manager,g_param_spec_get_name(specs[i]),self->private->global_dparams[i]);
      GST_DEBUG("    added global_param \"%s\"",g_param_spec_get_name(specs[i]));
    }
  }
  g_object_get(G_OBJECT(self->private->song),"bin",&self->private->bin,NULL);
  gst_bin_add(self->private->bin,self->machine);
  g_assert(self->machine!=NULL);
  g_assert(self->src_elem!=NULL);
  g_assert(self->dst_elem!=NULL);

  if(BT_IS_SINK_MACHINE(self)) {
    GST_DEBUG("this will be the master for the song");
    g_object_set(G_OBJECT(self->private->song),"master",G_OBJECT(self->machine),NULL);
  }
  return(TRUE);
}

//-- methods

/**
 * bt_machine_add_pattern:
 * @self: the machine to add the pattern to
 * @pattern: the new pattern instance
 *
 * let the machine know that the suplied pattern has been added for the machine.
 */
void bt_machine_add_pattern(const BtMachine *self, const BtPattern *pattern) {
	self->private->patterns=g_list_append(self->private->patterns,g_object_ref(G_OBJECT(pattern)));
}

/**
 * bt_machine_get_pattern_by_id:
 * @self: the machine to search for the pattern
 * @id: the identifier of the pattern
 *
 * search the machine for a pattern by the supplied id.
 * The pattern must have been added previously to this setup with #bt_machine_add_pattern().
 *
 * Returns: BtPattern instance or NULL if not found
 */
BtPattern *bt_machine_get_pattern_by_id(const BtMachine *self,const gchar *id) {
  gboolean found=FALSE;
	BtPattern *pattern;
  gchar *pattern_id;
	GList* node=g_list_first(self->private->patterns);
	
	while(node) {
		pattern=BT_PATTERN(node->data);
    g_object_get(G_OBJECT(pattern),"id",&pattern_id,NULL);
		if(!strcmp(pattern_id,"id")) found=TRUE;
    g_free(pattern_id);
    if(found) return(pattern);
		node=g_list_next(node);
	}
  return(NULL);
}

/**
 * bt_machine_get_global_dparam_index:
 * @self: the machine to search for the global dparam
 * @name: the name of the global dparam
 *
 * Searches the list of registered dparam of a machine for a global dparam of
 * the given name and returns the index if found.
 *
 * Returns: the index or -1 when it has not be found
 */
glong bt_machine_get_global_dparam_index(const BtMachine *self, const gchar *name) {
  GstDParam *dparam=gst_dpman_get_dparam(self->private->dparam_manager,name);
  glong i;

  if((dparam==NULL)) { GST_ERROR("no dparam named \"%s\" found",name);return(-1); }
  
  for(i=0;i<self->private->global_params;i++) {
    if(self->private->global_dparams[i]==dparam) return(i);
  }
  return(-1);
}

/**
 * bt_machine_get_voice_dparam_index:
 * @self: the machine to search for the voice dparam
 * @name: the name of the voice dparam
 *
 * Searches the list of registered dparam of a machine for a voice dparam of
 * the given name and returns the index if found.
 *
 * Returns: the index or -1 when it has not be found
 */
glong bt_machine_get_voice_dparam_index(const BtMachine *self, const gchar *name) {
  GstDParam *dparam=gst_dpman_get_dparam(self->private->dparam_manager,name);
  glong i;

  if((dparam==NULL)) { GST_ERROR("no dparam named \"%s\" found",name);return(-1); }
  
  // @todo we need to support multiple voices
  for(i=0;i<self->private->voice_params;i++) {
    if(self->private->voice_dparams[i]==dparam) return(i);
  }
  return(-1);
}

/**
 * bt_machine_get_global_dparam_type:
 * @self: the machine to search for the global dparam type
 * @index: the offset in the list of global dparams
 *
 * Retrieves the GType of a global dparam 
 *
 * Returns: the requested GType
 */
GType bt_machine_get_global_dparam_type(const BtMachine *self, glong index) {
  g_assert(index<self->private->global_params);
  return(self->private->global_types[index]);
}

/**
 * bt_machine_get_voice_dparam_type:
 * @self: the machine to search for the voice dparam type
 * @index: the offset in the list of voice dparams
 *
 * Retrieves the GType of a voice dparam 
 *
 * Returns: the requested GType
 */
GType bt_machine_get_voice_dparam_type(const BtMachine *self, glong index) {
  g_assert(index<self->private->voice_params);
  return(self->private->voice_types[index]);
}

/**
 * bt_machine_set_global_dparam_value:
 * @self: the machine to set the global dparam value
 * @index: the offset in the list of global dparams
 * @event: the new value
 *
 * Sets a the specified global dparam to the give data value.
 *
 */
void bt_machine_set_global_dparam_value(const BtMachine *self, glong index, GValue *event) {
  GstDParam *dparam;
  
  g_assert(index<self->private->global_params);
  
  dparam=self->private->global_dparams[index];
  // depending on the type, set the GValue
  // @todo use a function pointer here, like e.g. self->private->global_param_set(dparam,event)
  switch(G_VALUE_TYPE(event)) {
    case G_TYPE_DOUBLE: {
      //gchar *str=g_strdup_value_contents(event);
      //GST_INFO("events for %s at track %d : \"%s\"",self->private->id,index,str);
      g_object_set_property(G_OBJECT(dparam),"value_double",event);
      //g_free(str);
    }  break;
    default:
      GST_ERROR("unsupported GType=%d",G_VALUE_TYPE(event));
  }
}

/**
 * bt_machine_pattern_iterator_new:
 * @self: the machine to generate the pattern iterator from
 *
 * Builds an iterator handle, one can use to traverse the #BtPattern list of the
 * machine.
 * The new iterator already points to the first element in the list.
 * Advance the iterator with bt_machine_pattern_iterator_next() and
 * read from the iterator with bt_machine_pattern_iterator_get_pattern().
 *
 * Returns: the iterator or NULL
 */
gpointer bt_machine_pattern_iterator_new(const BtMachine *self) {
  gpointer res=NULL;

  if(self->private->patterns) {
    res=g_list_first(self->private->patterns);
  }
  return(res);
}

/**
 * bt_machine_pattern_iterator_next:
 * @iter: the iterator handle as returned by bt_machine_pattern_iterator_new()
 *
 * Advances the iterator for one element. Read data with bt_machine_pattern_iterator_get_pattern().
 * 
 * Returns: the new iterator or NULL for end-of-list
 */
gpointer bt_machine_pattern_iterator_next(gpointer iter) {
  g_assert(iter);
	return(g_list_next((GList *)iter));
}

/**
 * bt_machine_pattern_iterator_get_pattern:
 * @iter: the iterator handle as returned by bt_machine_pattern_iterator_new()
 *
 * Retrieves the #BtPattern from the current list position determined by the iterator.
 * Advance the iterator with bt_machine_pattern_iterator_next().
 *
 * Returns: the #BtPattern instance
 */
BtPattern *bt_machine_pattern_iterator_get_pattern(gpointer iter) {
  g_assert(iter);
	return(BT_PATTERN(((GList *)iter)->data));
}

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_machine_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtMachine *self = BT_MACHINE(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_SONG: {
      g_value_set_object(value, self->private->song);
    } break;
    case MACHINE_ID: {
      g_value_set_string(value, self->private->id);
    } break;
    case MACHINE_PLUGIN_NAME: {
      g_value_set_string(value, self->private->plugin_name);
    } break;
    case MACHINE_VOICES: {
      g_value_set_long(value, self->private->voices);
      // @todo reallocate self->private->patterns->private->data
    } break;
    case MACHINE_GLOBAL_PARAMS: {
      g_value_set_long(value, self->private->global_params);
    } break;
    case MACHINE_VOICE_PARAMS: {
      g_value_set_long(value, self->private->voice_params);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_machine_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtMachine *self = BT_MACHINE(object);
  return_if_disposed();
  switch (property_id) {
    case MACHINE_SONG: {
      g_object_try_weak_unref(self->private->song);
      self->private->song = BT_SONG(g_value_get_object(value));
      g_object_try_weak_ref(self->private->song);
      GST_DEBUG("set the song for machine: %p",self->private->song);
    } break;
    case MACHINE_ID: {
      g_free(self->private->id);
      self->private->id = g_value_dup_string(value);
      GST_DEBUG("set the id for machine: %s",self->private->id);
      //if(self->machine)	bt_machine_rename(self);
    } break;
    case MACHINE_PLUGIN_NAME: {
      g_free(self->private->plugin_name);
      self->private->plugin_name = g_value_dup_string(value);
      GST_DEBUG("set the plugin_name for machine: %s",self->private->plugin_name);
    } break;
    case MACHINE_VOICES: {
      self->private->voices = g_value_get_long(value);
    } break;
    case MACHINE_GLOBAL_PARAMS: {
      self->private->global_params = g_value_get_long(value);
    } break;
    case MACHINE_VOICE_PARAMS: {
      self->private->voice_params = g_value_get_long(value);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_machine_dispose(GObject *object) {
  BtMachine *self = BT_MACHINE(object);

	return_if_disposed();
  self->private->dispose_has_run = TRUE;

  GST_DEBUG("!!!! self=%p",self);
  
  // remove the GstElements from the bin
  if(self->private->bin) {
    if(self->machine) {
      GST_DEBUG("  removing machine from bin, obj->ref_count=%d",G_OBJECT(self->machine)->ref_count);
      gst_bin_remove(self->private->bin,self->machine);
    }
    if(self->adder) {
      GST_DEBUG("  removing adder from bin, obj->ref_count=%d",G_OBJECT(self->adder)->ref_count);
       gst_bin_remove(self->private->bin,self->adder);
    }
    if(self->spreader) {
      GST_DEBUG("  removing spreader from bin, obj->ref_count=%d",G_OBJECT(self->spreader)->ref_count);
      gst_bin_remove(self->private->bin,self->spreader);
    }
    // @todo add the rest
    GST_DEBUG("  elements removed from bin");
    g_object_try_unref(self->private->bin);
  }

  g_object_try_weak_unref(self->private->song);
  g_object_try_unref(self->private->dparam_manager);
  g_object_try_unref(self->private->input_level);
  g_object_try_unref(self->private->output_level);
  //g_object_try_unref(self->machine);
  //g_object_try_unref(self->adder);
  //g_object_try_unref(self->spreader);

  // unref list of patterns
	if(self->private->patterns) {
    GList* node=g_list_first(self->private->patterns);
		while(node) {
      {
        GObject *obj=node->data;
        GST_DEBUG("  free pattern : %p (%d)",obj,obj->ref_count);
      }
			g_object_try_unref(node->data);
      node->data=NULL;
			node=g_list_next(node);
		}
	}
}

static void bt_machine_finalize(GObject *object) {
  BtMachine *self = BT_MACHINE(object);

  GST_DEBUG("!!!! self=%p",self);

	g_free(self->private->id);
	g_free(self->private->plugin_name);
  g_free(self->private->voice_types);
  g_free(self->private->voice_dparams);
  g_free(self->private->global_types);
  g_free(self->private->global_dparams);
  g_free(self->private);
  // free list of patterns
	if(self->private->patterns) {
		g_list_free(self->private->patterns);
		self->private->patterns=NULL;
	}
}

static void bt_machine_init(GTypeInstance *instance, gpointer g_class) {
  BtMachine *self = BT_MACHINE(instance);
  self->private = g_new0(BtMachinePrivate,1);
  self->private->dispose_has_run = FALSE;
  self->private->voices=-1;
}

static void bt_machine_class_init(BtMachineClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GParamSpec *g_param_spec;
  
  gobject_class->set_property = bt_machine_set_property;
  gobject_class->get_property = bt_machine_get_property;
  gobject_class->dispose      = bt_machine_dispose;
  gobject_class->finalize     = bt_machine_finalize;

  g_object_class_install_property(gobject_class,MACHINE_SONG,
                                  g_param_spec_object("song",
                                     "song contruct prop",
                                     "song object, the machine belongs to",
                                     BT_TYPE_SONG, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_ID,
                                  g_param_spec_string("id",
                                     "id contruct prop",
                                     "machine identifier",
                                     "unamed machine", /* default value */
                                     G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_PLUGIN_NAME,
                                  g_param_spec_string("plugin-name",
                                     "plugin_name contruct prop",
                                     "the name of the gst plugin for the machine",
                                     "unamed machine", /* default value */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_VOICES,
																	g_param_spec_long("voices",
                                     "voices prop",
                                     "number of voices in the machine",
                                     0,
                                     G_MAXLONG,
                                     0,
                                     G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_GLOBAL_PARAMS,
																	g_param_spec_long("global-params",
                                     "global_params prop",
                                     "number of params for the machine",
                                     0,
                                     G_MAXLONG,
                                     0,
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,MACHINE_VOICE_PARAMS,
																	g_param_spec_long("voice-params",
                                     "voice_params prop",
                                     "number of params for each machine voice",
                                     0,
                                     G_MAXLONG,
                                     0,
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));
}

GType bt_machine_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtMachineClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_machine_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtMachine),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_machine_init, // instance_init
    };
		type = g_type_register_static(G_TYPE_OBJECT,"BtMachine",&info,0);
  }
  return type;
}

