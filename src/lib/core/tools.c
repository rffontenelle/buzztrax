/* $Id: tools.c,v 1.30 2006-09-03 13:18:37 ensonic Exp $
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
 
#define BT_CORE
#define BT_TOOLS_C
#include <libbtcore/core.h>

static gboolean bt_gst_registry_class_filter(GstPluginFeature * const feature, gpointer user_data) {
  const gchar * const class_filter=(gchar *)user_data;
  const gsize sl=(gsize)(strlen(class_filter));
  
  if(GST_IS_ELEMENT_FACTORY(feature)) {
    if(!g_ascii_strncasecmp(gst_element_factory_get_klass(GST_ELEMENT_FACTORY(feature)),class_filter,sl)) {
      return(TRUE);
    }
  }
  return(FALSE);
}

/**
 * bt_gst_registry_get_element_names_by_class:
 * @class_filter: path for filtering (e.g. "Sink/Audio")
 *
 * Iterates over all available plugins and filters by class-name.
 * The matching uses right-truncation, e.g. "Sink/" matches all sorts of sinks.
 *
 * Returns: list of element names, g_list_free after use.
 */
GList *bt_gst_registry_get_element_names_by_class(const gchar *class_filter) {
  const GList *node;
  GList *res=NULL;
  g_return_val_if_fail(BT_IS_STRING(class_filter),NULL);
 
  GList * const list=gst_default_registry_feature_filter(bt_gst_registry_class_filter,FALSE,(gpointer)class_filter);
  for(node=list;node;node=g_list_next(node)) {
    GstPluginFeature * const feature=GST_PLUGIN_FEATURE(node->data);
    res=g_list_append(res,(gchar *)gst_plugin_feature_get_name(feature));
  }
  g_list_free(list);
  return(res);
}

/**
 * gst_element_dbg_caps:
 * @elem: a #GstElement
 *
 * Write out a list of pads for the given element
 *
 */
void gst_element_dbg_pads(GstElement * const elem) {
  GstIterator *it;
  GstPad * const pad;
  GstPadDirection dir;
  const gchar *dirs[]={"unknown","src","sink",NULL};
  gboolean done;
  guint i;

  GST_DEBUG("machine: %s",GST_ELEMENT_NAME(elem));
  it=gst_element_iterate_pads(elem);
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (it, (gpointer)&pad)) {
      case GST_ITERATOR_OK:
        dir=gst_pad_get_direction(pad);
        GstCaps * const caps=gst_pad_get_caps(pad);
        const guint size=gst_caps_get_size(caps);
        GST_DEBUG("  pad: %s:%s, dir: %s, nr-caps: %d",GST_DEBUG_PAD_NAME(pad),dirs[dir],size);
        // iterate over structures and print
        for(i=0;i<size;i++) {
          const GstStructure * const structure=gst_caps_get_structure(caps,i);
          gchar * const str=gst_structure_to_string(structure);
          GST_DEBUG("    caps[%2d]: %s : %s",i,GST_STR_NULL(gst_structure_get_name(structure)),str);
          g_free(str);
          //gst_object_unref(structure);
        }
        //gst_object_unref(caps);
        gst_object_unref(pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free(it);
}

#ifndef HAVE_GLIB_2_8
gpointer g_try_malloc0( const gulong n_bytes ) {
  gpointer const mem=g_try_malloc(n_bytes);

  if(mem) {
    memset(mem,0,n_bytes);
  }
  return(mem);
}
#endif

/**
 * bt_g_type_get_base_type:
 * @type: a GType
 *
 * Call g_type_parent() as long as it returns a parent.
 *
 * Returns: the super parent type, aka base type. 
 */
GType bt_g_type_get_base_type(GType type) {
  GType base;

  while((base=g_type_parent(type))) type=base;
  return(type);
}

/* cpu load monitoring */

static GstClockTime treal_last=G_GINT64_CONSTANT(0),tuser_last=G_GINT64_CONSTANT(0),tsys_last=G_GINT64_CONSTANT(0);
//long clk=1;

/*
 * bt_cpu_load_init:
 *
 * Initializes cpu usage monitoring.
 */
static void GST_GNUC_CONSTRUCTOR bt_cpu_load_init(void) {
  struct timeval now;
    
  gettimeofday(&now,NULL);
  treal_last=GST_TIMEVAL_TO_TIME(now);
  //clk=sysconf(_SC_CLK_TCK);
}

/**
 * bt_cpu_load_get_current:
 *
 * Determines the current CPU load.
 *
 * Returns: CPU usage as integer ranging from 0% to 100%
 */
guint bt_cpu_load_get_current(void) {
  struct rusage rus,ruc;
  //struct tms tms;
  GstClockTime tnow,treal,tuser,tsys;
  struct timeval now;
  guint cpuload;
  
  gettimeofday(&now,NULL);
  tnow=GST_TIMEVAL_TO_TIME(now);
  treal=tnow-treal_last;
  treal_last=tnow;
  // version 1
  getrusage(RUSAGE_SELF,&rus);
  getrusage(RUSAGE_CHILDREN,&ruc);
  tnow=GST_TIMEVAL_TO_TIME(rus.ru_utime)+GST_TIMEVAL_TO_TIME(ruc.ru_utime);
  tuser=tnow-tuser_last;
  tuser_last=tnow;
  tnow=GST_TIMEVAL_TO_TIME(rus.ru_stime)+GST_TIMEVAL_TO_TIME(ruc.ru_stime);
  tsys=tnow-tsys_last;
  tsys_last=tnow;
  // version 2
  //times(&tms);
  // percentage
  cpuload=(guint)gst_util_uint64_scale(tuser+tsys,G_GINT64_CONSTANT(100),treal);
  GST_LOG("real %"GST_TIME_FORMAT", user %"GST_TIME_FORMAT", sys %"GST_TIME_FORMAT" => cpuload %d",GST_TIME_ARGS(treal),GST_TIME_ARGS(tuser),GST_TIME_ARGS(tsys),cpuload);
  //printf("user %6.4lf, sys %6.4lf\n",(double)tms.tms_utime/(double)clk,(double)tms.tms_stime/(double)clk);

  return(cpuload);
}
