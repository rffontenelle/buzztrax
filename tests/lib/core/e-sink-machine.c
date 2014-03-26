/* Buzztrax
 * Copyright (C) 2009 Buzztrax team <buzztrax-devel@buzztrax.org>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "m-bt-core.h"
#include <gst/base/gstbasesink.h>
#include <gst/audio/gstaudiobasesink.h>

//-- globals

static BtApplication *app;
static BtSong *song;
static BtSettings *settings;

//-- fixtures

static void
case_setup (void)
{
  BT_CASE_START;
}

static void
test_setup (void)
{
  app = bt_test_application_new ();
  song = bt_song_new (app);
  settings = bt_settings_make ();
}

static void
test_teardown (void)
{
  g_object_unref (settings);
  g_object_checked_unref (song);
  g_object_checked_unref (app);
}

static void
case_teardown (void)
{
}

//-- helper

static GstElement *
get_sink_element (GstBin * bin)
{
  GstElement *e;
  GList *list = GST_BIN_CHILDREN (bin);
  GList *node;

  GST_INFO_OBJECT (bin, "looking for audio_sink in %d children",
      g_list_length (list));
  for (node = list; node; node = g_list_next (node)) {
    e = (GstElement *) node->data;
    GST_INFO_OBJECT (bin, "trying '%s'", GST_OBJECT_NAME (e));
    if (GST_IS_BIN (e) && GST_OBJECT_FLAG_IS_SET (e, GST_ELEMENT_FLAG_SINK)) {
      return get_sink_element ((GstBin *) e);
    }
    // while audiosink should subclass GstAudioBaseSink, they don't have to ...
    // we're relying on the bin implementation to ensure the sink takes audio
    if (GST_IS_BASE_SINK (e)) {
      return e;
    }
  }
  return NULL;
}


//-- tests

static void
test_bt_sink_machine_new (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", "fakesink", NULL);

  /* act */
  GError *err = NULL;
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", &err);

  /* assert */
  fail_unless (machine != NULL, NULL);
  fail_unless (err == NULL, NULL);

  /* cleanup */
  BT_TEST_END;
}

static void
test_bt_sink_machine_def_patterns (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", "fakesink", NULL);
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);

  /* act */
  GList *list = (GList *) check_gobject_get_ptr_property (machine, "patterns");

  /* assert */
  fail_unless (list != NULL, NULL);
  ck_assert_int_eq (g_list_length (list), 2);   /* break+mute */

  /* cleanup */
  g_list_foreach (list, (GFunc) g_object_unref, NULL);
  g_list_free (list);
  BT_TEST_END;
}

static void
test_bt_sink_machine_pattern (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", "fakesink", NULL);
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);

  /* act */
  BtCmdPattern *pattern =
      (BtCmdPattern *) bt_pattern_new (song, "pattern-name", 8L,
      BT_MACHINE (machine));

  /* assert */
  fail_unless (pattern != NULL, NULL);
  ck_assert_gobject_gulong_eq (pattern, "voices", 0);

  /* cleanup */
  g_object_unref (pattern);
  BT_TEST_END;
}

static void
test_bt_sink_machine_pattern_by_id (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", "fakesink", NULL);
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);

  /* act */
  BtCmdPattern *pattern =
      (BtCmdPattern *) bt_pattern_new (song, "pattern-name", 8L,
      BT_MACHINE (machine));

  /* assert */
  ck_assert_gobject_eq_and_unref (bt_machine_get_pattern_by_name (BT_MACHINE
          (machine), "pattern-name"), pattern);

  /* cleanup */
  g_object_unref (pattern);
  BT_TEST_END;
}

static void
test_bt_sink_machine_pattern_by_list (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", "fakesink", NULL);
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);
  BtCmdPattern *pattern =
      (BtCmdPattern *) bt_pattern_new (song, "pattern-name", 8L,
      BT_MACHINE (machine));
  GList *list = (GList *) check_gobject_get_ptr_property (machine, "patterns");

  /* act */
  GList *node = g_list_last (list);

  /* assert */
  fail_unless (node->data == pattern, NULL);

  /* cleanup */
  g_list_foreach (list, (GFunc) g_object_unref, NULL);
  g_list_free (list);
  g_object_unref (pattern);
  BT_TEST_END;
}

static void
test_bt_sink_machine_default (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  g_object_set (settings, "audiosink", NULL, NULL);

  /* act */
  GError *err = NULL;
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", &err);

  /* assert */
  fail_unless (machine != NULL, NULL);
  fail_unless (err == NULL, NULL);

  /* cleanup */
  BT_TEST_END;
}

static void
test_bt_sink_machine_fallback (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  /* we have no test settings anymore, but we could write to the system
   * settings directy, as we're using the memory backend, we need the GSettings
   * instance though (which is private to BtSettings
   gchar *settings_str = NULL;
   bt_test_settings_set (BT_TEST_SETTINGS (settings), "system-audiosink",
   &settings_str);
   */
  g_object_set (settings, "audiosink", NULL, NULL);

  /* act */
  GError *err = NULL;
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", &err);

  /* assert */
  fail_unless (machine != NULL, NULL);
  fail_unless (err == NULL, NULL);

  /* cleanup */
  BT_TEST_END;
}

static void
test_bt_sink_machine_actual_sink (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);

  /* act */
  GstElement *sink_bin =
      GST_ELEMENT (check_gobject_get_object_property (machine, "machine"));
  gst_element_set_state (sink_bin, GST_STATE_READY);
  GstElement *sink = get_sink_element ((GstBin *) sink_bin);

  /* assert */
  fail_unless (sink != NULL, NULL);

  /* cleanup */
  gst_object_unref (sink_bin);
  BT_TEST_END;
}

/* the parameter index _i is 2bits for latency, 2bits for bpm, 2 bits for tpb */
static void
test_bt_sink_machine_latency (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  BtSongInfo *song_info =
      BT_SONG_INFO (check_gobject_get_object_property (song, "song-info"));
  BtSinkMachine *machine = bt_sink_machine_new (song, "master", NULL);
  GstElement *sink_bin =
      GST_ELEMENT (check_gobject_get_object_property (machine, "machine"));
  gst_element_set_state (sink_bin, GST_STATE_READY);
  GstElement *sink = get_sink_element ((GstBin *) sink_bin);
  if (!sink || !GST_IS_AUDIO_BASE_SINK (sink))
    goto Cleanup;
  guint latency = 20 + 20 * (_i & 0x3);
  gulong bpm = 80 + 20 * ((_i >> 2) & 0x3);
  gulong tpb = 4 + 2 * ((_i >> 4) & 0x3);

  /* act */
  // set various bpm, tpb on song_info, set various latency on settings
  // assert the resulting latency-time properties on the audio_sink
  g_object_set (settings, "latency", latency, NULL);
  g_object_set (song_info, "bpm", bpm, "tpb", tpb, NULL);

  /* assert */
  gulong st, c_bpm, c_tpb;
  gint64 latency_time, c_latency_time;
  g_object_get (sink_bin, "subticks-per-tick", &st, "ticks-per-beat", &c_tpb,
      "beats-per-minute", &c_bpm, NULL);
  g_object_get (sink, "latency-time", &c_latency_time, NULL);
  latency_time = GST_TIME_AS_USECONDS ((GST_SECOND * 60) / (bpm * tpb * st));

  GST_INFO_OBJECT (sink,
      "bpm=%3lu=%3lu, tpb=%lu=%lu, stpb=%2lu, target-latency=%2u , latency-time=%6" G_GINT64_FORMAT "=%6"
      G_GINT64_FORMAT ", delta=%+4" G_GINT64_FORMAT, bpm, c_bpm, tpb, c_tpb, st,
      latency, latency_time, c_latency_time,
      (latency_time - ((gint) latency * 1000)) / 1000);

  ck_assert_ulong_eq (c_bpm, bpm);
  ck_assert_ulong_eq (c_tpb, tpb);
  ck_assert_int64_eq (c_latency_time, latency_time);

  /* cleanup */
Cleanup:
  gst_object_unref (sink_bin);
  g_object_unref (song_info);
  BT_TEST_END;
}

TCase *
bt_sink_machine_example_case (void)
{
  TCase *tc = tcase_create ("BtSinkMachineExamples");

  tcase_add_test (tc, test_bt_sink_machine_new);
  tcase_add_test (tc, test_bt_sink_machine_def_patterns);
  tcase_add_test (tc, test_bt_sink_machine_pattern);
  tcase_add_test (tc, test_bt_sink_machine_pattern_by_id);
  tcase_add_test (tc, test_bt_sink_machine_pattern_by_list);
  tcase_add_test (tc, test_bt_sink_machine_default);
  tcase_add_test (tc, test_bt_sink_machine_fallback);
  tcase_add_test (tc, test_bt_sink_machine_actual_sink);
  tcase_add_loop_test (tc, test_bt_sink_machine_latency, 0, 64);
  tcase_add_checked_fixture (tc, test_setup, test_teardown);
  tcase_add_unchecked_fixture (tc, case_setup, case_teardown);
  return (tc);
}
