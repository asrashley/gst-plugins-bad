/* GStreamer unit test for MPEG-DASH
 *
 * Copyright (c) <2015> YouView TV Ltd
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>
#include "adaptive_demux_common.h"
#include <gst/check/gsttestclock.h>

#define DEMUX_ELEMENT_NAME "dashdemux"

#define COPY_OUTPUT_TEST_DATA(outputTestData,testData) do { \
    guint otdPos, otdLen = sizeof((outputTestData)) / sizeof((outputTestData)[0]); \
    for(otdPos=0; otdPos<otdLen; ++otdPos){ \
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->output_streams = g_list_append (GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->output_streams, &(outputTestData)[otdPos]); \
    } \
  } while(0)

typedef struct _GstDashDemuxTestInputData
{
  const gchar *uri;
  const guint8 *payload;
  guint64 size;
} GstDashDemuxTestInputData;

typedef struct _GstTestHTTPSrcTestData
{
  const GstDashDemuxTestInputData *input;
  GstStructure *data;
} GstTestHTTPSrcTestData;

typedef struct _GstDashDemuxTestCase
{
  GstAdaptiveDemuxTestCase parent;

  /* the number of Protection Events sent to each pad */
  GstStructure *countContentProtectionEvents;

  /* for live mpd, the wallclock time when MPD started to be available */
  GDateTime *availabilityStartTime;

  /* timeshift buffer depth, in ms. -1 for infinite */
  gint64 timeshiftBufferDepth;

  /* the number of seconds the server clock is ahead of client clock */
  gint64 clockCompensation;
} GstDashDemuxTestCase;

GType gst_dash_demux_test_case_get_type (void);
static void gst_dash_demux_test_case_dispose (GObject * object);
static void gst_dash_demux_test_case_finalize (GObject * object);
static void gst_dash_demux_test_case_clear (GstDashDemuxTestCase * test_case);

static GstDashDemuxTestCase *
gst_dash_demux_test_case_new (void)
    G_GNUC_MALLOC;

#define NTP_TO_UNIX_EPOCH G_GUINT64_CONSTANT(2208988800)        /* difference (in seconds) between NTP epoch and Unix epoch */
#define GST_TYPE_DASH_DEMUX_TEST_CASE \
  (gst_dash_demux_test_case_get_type())
#define GST_DASH_DEMUX_TEST_CASE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DASH_DEMUX_TEST_CASE, GstDashDemuxTestCase))
#define GST_DASH_DEMUX_TEST_CASE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DASH_DEMUX_TEST_CASE, GstDashDemuxTestCaseClass))
#define GST_DASH_DEMUX_TEST_CASE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DASH_DEMUX_TEST_CASE, GstDashDemuxTestCaseClass))
#define GST_IS_DASH_DEMUX_TEST_CASE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DASH_DEMUX_TEST_CASE))
#define GST_IS_DASH_DEMUX_TEST_CASE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DASH_DEMUX_TEST_CASE))

     static GstDashDemuxTestCase *gst_dash_demux_test_case_new (void)
{
  return g_object_newv (GST_TYPE_DASH_DEMUX_TEST_CASE, 0, NULL);
}

typedef struct _GstDashDemuxTestCaseClass
{
  GstAdaptiveDemuxTestCaseClass parent_class;
} GstDashDemuxTestCaseClass;

#define gst_dash_demux_test_case_parent_class parent_class

G_DEFINE_TYPE (GstDashDemuxTestCase, gst_dash_demux_test_case,
    GST_TYPE_ADAPTIVE_DEMUX_TEST_CASE);

static void
gst_dash_demux_test_case_class_init (GstDashDemuxTestCaseClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);

  object->dispose = gst_dash_demux_test_case_dispose;
  object->finalize = gst_dash_demux_test_case_finalize;
}

static void
gst_dash_demux_test_case_init (GstDashDemuxTestCase * test_case)
{
  test_case->countContentProtectionEvents = NULL;
  gst_dash_demux_test_case_clear (test_case);
}

static void
gst_dash_demux_test_case_clear (GstDashDemuxTestCase * test_case)
{
  if (test_case->countContentProtectionEvents) {
    gst_structure_free (test_case->countContentProtectionEvents);
    test_case->countContentProtectionEvents = NULL;
  }
  if (test_case->availabilityStartTime) {
    g_date_time_unref (test_case->availabilityStartTime);
    test_case->availabilityStartTime = NULL;
  }
  test_case->timeshiftBufferDepth = -1;
  test_case->clockCompensation = 0;
}

static void
gst_dash_demux_test_case_dispose (GObject * object)
{
  GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (object);

  gst_dash_demux_test_case_clear (testData);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dash_demux_test_case_finalize (GObject * object)
{
  /*GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dashdemux_http_src_start (GstTestHTTPSrc * src,
    const gchar * uri, GstTestHTTPSrcInput * input_data, gpointer user_data)
{
  const GstTestHTTPSrcTestData *test_case =
      (const GstTestHTTPSrcTestData *) user_data;
  for (guint i = 0; test_case->input[i].uri; ++i) {
    if (g_strcmp0 (test_case->input[i].uri, uri) == 0) {
      input_data->context = (gpointer) & test_case->input[i];
      input_data->size = test_case->input[i].size;
      if (test_case->input[i].size == 0)
        input_data->size = strlen ((gchar *) test_case->input[i].payload);
      return TRUE;
    }
  }
  return FALSE;
}

static GstFlowReturn
gst_dashdemux_http_src_create (GstTestHTTPSrc * src,
    guint64 offset,
    guint length, GstBuffer ** retbuf, gpointer context, gpointer user_data)
{
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) context;
  GstBuffer *buf;

  buf = gst_buffer_new_allocate (NULL, length, NULL);
  fail_if (buf == NULL, "Not enough memory to allocate buffer");

  if (input->payload) {
    gst_buffer_fill (buf, 0, input->payload + offset, length);
  } else {
    GstMapInfo info;
    guint pattern;

    pattern = offset - offset % sizeof (pattern);

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    for (guint64 i = 0; i < length; ++i) {
      gchar pattern_byte_to_write = (offset + i) % sizeof (pattern);
      if (pattern_byte_to_write == 0) {
        pattern = offset + i;
      }
      info.data[i] = (pattern >> (pattern_byte_to_write * 8)) & 0xFF;
    }
    gst_buffer_unmap (buf, &info);
  }
  *retbuf = buf;
  return GST_FLOW_OK;
}

static GDateTime *
gst_dash_demux_test_get_current_time (void)
{
  GstClock *clock;
  GstClockTime time;
  GTimeVal gtv;
  GDateTime *ret;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL);
  time = gst_clock_get_time (clock);

  gtv.tv_sec = time / GST_SECOND;
  gtv.tv_usec = time % GST_SECOND;
  ret = g_date_time_new_from_timeval_utc (&gtv);

  gst_object_unref (clock);
  return ret;
}

static GstFlowReturn
gst_dashdemux_http_src_create_mock_time_server (GstTestHTTPSrc * src,
    guint64 offset,
    guint length, GstBuffer ** retbuf, gpointer context, gpointer user_data)
{
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) context;
  const GstTestHTTPSrcTestData *test_case =
      (const GstTestHTTPSrcTestData *) user_data;
  GDateTime *now;
  GDateTime *newTime;
  GstMapInfo info;
  GstBuffer *buf;
  guint access_count = 0;

  if (!g_str_has_prefix (input->uri, "http://mocktime")) {
    return gst_dashdemux_http_src_create (src, offset, length, retbuf, context,
        user_data);
  }
  /* time server is a special case
   * Return the current time updated with the number of seconds configured in
   * payload
   */
  now = gst_dash_demux_test_get_current_time ();
  newTime = g_date_time_add_seconds (now, atoi ((const char *) input->payload));
  g_date_time_unref (now);

  if (g_strcmp0 (input->uri, "http://mocktime/http-xsdate") == 0) {
    gchar *newTimeString;
    newTimeString =
        g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
        g_date_time_get_year (newTime),
        g_date_time_get_month (newTime),
        g_date_time_get_day_of_month (newTime),
        g_date_time_get_hour (newTime),
        g_date_time_get_minute (newTime),
        g_date_time_get_second (newTime),
        g_date_time_get_microsecond (newTime));
    /* use strlen (newTimeString) rather than strlen (newTimeString)+1
       so that the buffer does not contain the zero terminator */
    buf = gst_buffer_new_wrapped (newTimeString, strlen (newTimeString));
    fail_if (buf == NULL, "Not enough memory to allocate buffer");
    if (test_case->data != NULL) {
      gst_structure_get_uint (test_case->data, "http-xsdate-request-count",
          &access_count);
      access_count++;
      gst_structure_set (test_case->data, "http-xsdate-request-count",
          G_TYPE_UINT, access_count, NULL);
    }
  } else if (g_strcmp0 (input->uri, "http://mocktime/http-ntp") == 0) {
    guint64 fraction;

    buf = gst_buffer_new_allocate (NULL, 8, NULL);
    fail_if (buf == NULL, "Not enough memory to allocate buffer");

    fraction = gst_util_uint64_scale (g_date_time_get_microsecond (newTime),
        G_GUINT64_CONSTANT (1) << 32, 1000000);
    fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
    GST_WRITE_UINT32_BE (info.data,
        g_date_time_to_unix (newTime) + NTP_TO_UNIX_EPOCH);
    GST_WRITE_UINT32_BE (info.data + 4, fraction);

    gst_buffer_unmap (buf, &info);
    if (test_case->data != NULL) {
      gst_structure_get_uint (test_case->data, "http-ntp-request-count",
          &access_count);
      access_count++;
      gst_structure_set (test_case->data, "http-ntp-request-count", G_TYPE_UINT,
          access_count, NULL);
    }

  } else {
    g_date_time_unref (newTime);
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s %s",
            "Unrecognised UTCtiming protocol", input->uri), ("%s %s",
            "Unrecognised UTCtiming protocol", input->uri));
    return GST_FLOW_ERROR;
  }

  g_date_time_unref (newTime);
  GST_BUFFER_OFFSET (buf) = 0;
  GST_BUFFER_OFFSET_END (buf) = gst_buffer_get_size (buf);
  *retbuf = buf;

  return GST_FLOW_OK;
}

/* get a time by adding the received offset (in seconds) to current time.
 * Negative offset means time will be in the past.
 */
static GDateTime *
timeFromNow (gdouble seconds)
{
  GDateTime *now;
  GDateTime *newTime;

  now = gst_dash_demux_test_get_current_time ();
  newTime = g_date_time_add_seconds (now, seconds);
  g_date_time_unref (now);
  return newTime;
}

/* Convert a GDateTime to xs:dateTime format */
static gchar *
toXSDateTime (GDateTime * time)
{
  /*
   * There is no g_date_time format to print the microsecond part,
   * so we must construct the string ourselves.
   */
  return g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02d.%06d",
      g_date_time_get_year (time),
      g_date_time_get_month (time),
      g_date_time_get_day_of_month (time),
      g_date_time_get_hour (time),
      g_date_time_get_minute (time),
      g_date_time_get_second (time), g_date_time_get_microsecond (time));
}

/* get the MPD play time corresponding to now
 * - availabilityStartTime: the time when mpd was first available
 * - clock compensation: the number of seconds the server clock is ahead of client clock
 */
static GstClockTime
getCurrentPresentationTime (GDateTime * availabilityStartTime,
    guint64 clockCompensation)
{
  GDateTime *now;
  GTimeSpan stream_now;

  now = gst_dash_demux_test_get_current_time ();
  stream_now = g_date_time_difference (now, availabilityStartTime);
  g_date_time_unref (now);

  return stream_now * GST_USECOND + clockCompensation * GST_SECOND;
}


/******************** Test specific code starts here **************************/

/*
 * Test an mpd with an audio and a video stream
 *
 */
GST_START_TEST (simpleTest)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {"http://unit.test/video.webm", NULL, 9000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
    {"video_00", 9000, NULL}
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/*
 * Test an mpd with 2 periods
 *
 */
GST_START_TEST (testTwoPeriods)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT300S\">"
      "  <Period id=\"Period0\" duration=\"PT0.1S\">"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio1.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video1.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "  </Period>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio2.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video2.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio1.webm", NULL, 5001},
    {"http://unit.test/video1.webm", NULL, 9001},
    {"http://unit.test/audio2.webm", NULL, 5002},
    {"http://unit.test/video2.webm", NULL, 9002},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5001, NULL},
    {"video_00", 9001, NULL},
    {"audio_01", 5002, NULL},
    {"video_01", 9002, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/* test setting a property on an object */
#define test_int_prop(object, name, value) \
do \
{ \
  int val = value; \
  int val_after; \
  g_object_set (object, name, val, NULL); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val, "property check failed for %s: set to %d, but got %d", \
      name, val, val_after); \
} while (0)

#define test_float_prop(object, name, value) \
do \
{ \
  float val = value; \
  float val_after; \
  g_object_set (object, name, val, NULL); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val, "property check failed for %s: set to %f, but got %f", \
      name, val, val_after); \
} while (0)

/* test setting an invalid value for a property on an object.
 * Expect an assert and the property to remain unchanged
 */
#define test_invalid_int_prop(object, name, value) \
do \
{ \
  int val_before; \
  int val_after; \
  int val = value; \
  g_object_get (object, name, &val_before, NULL); \
  ASSERT_WARNING (g_object_set (object, name, val, NULL)); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val_before, "property check failed for %s: before %d, after %d", \
      name, val_before, val_after); \
} while (0)

#define test_invalid_float_prop(object, name, value) \
do \
{ \
  float val_before; \
  float val_after; \
  float val = value; \
  g_object_get (object, name, &val_before, NULL); \
  ASSERT_WARNING (g_object_set (object, name, val, NULL)); \
  g_object_get (object, name, &val_after, NULL); \
  fail_unless (val_after == val_before, "property check failed for %s: before %f, after %f", \
      name, val_before, val_after); \
} while (0)

static void
setAndTestDashParams (GstAdaptiveDemuxTestEngine * engine, gpointer user_data)
{
  /*  GstDashDemuxTestCase * testData = (GstDashDemuxTestCase*)user_data; */
  GObject *dashdemux = G_OBJECT (engine->demux);

  test_int_prop (dashdemux, "connection-speed", 1000);
  test_invalid_int_prop (dashdemux, "connection-speed", 4294967 + 1);

  test_float_prop (dashdemux, "bitrate-limit", 1);
  test_invalid_float_prop (dashdemux, "bitrate-limit", 2.1);

  test_int_prop (dashdemux, "max-buffering-time", 15);
  test_invalid_int_prop (dashdemux, "max-buffering-time", 1);

  test_float_prop (dashdemux, "bandwidth-usage", 0.5);
  test_invalid_float_prop (dashdemux, "bandwidth-usage", 2);

  test_int_prop (dashdemux, "max-bitrate", 1000);
  test_invalid_int_prop (dashdemux, "max-bitrate", 10);
}

/*
 * Test setting parameters
 *
 */
GST_START_TEST (testParameters)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.pre_test = setAndTestDashParams;
  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/*
 * Test seeking
 *
 */
GST_START_TEST (testSeek)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 10000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 10000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on the first pad listed in GstAdaptiveDemuxTestOutputStreamData and the
   * first chunk of at least one byte has already arrived in AppSink
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->threshold_for_seek = 4687 + 1;

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->seek_event =
      gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_SET,
      5 * GST_MSECOND, GST_SEEK_TYPE_NONE, 0);

  gst_adaptive_demux_test_seek (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", GST_ADAPTIVE_DEMUX_TEST_CASE (testData));

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;


static void
run_seek_position_test (gdouble rate, GstSeekType start_type,
    guint64 seek_start, GstSeekType stop_type, guint64 seek_stop,
    GstSeekFlags flags, guint64 segment_start, guint64 segment_stop,
    gint segments)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet "
      "        mimeType=\"audio/mp4\" minBandwidth=\"128000\" "
      "        maxBandwidth=\"128000\" segmentAlignment=\"true\">"
      "      <SegmentTemplate timescale=\"48000\" "
      "          initialization=\"init-$RepresentationID$.mp4\" "
      "          media=\"$RepresentationID$-$Number$.mp4\" "
      "          startNumber=\"1\">"
      "        <SegmentTimeline>"
      "          <S t=\"0\" d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "          <S d=\"48000\" /> "
      "        </SegmentTimeline>"
      "      </SegmentTemplate>"
      "      <Representation id=\"audio\" bandwidth=\"128000\" "
      "          codecs=\"mp4a.40.2\" audioSamplingRate=\"48000\"> "
      "        <AudioChannelConfiguration "
      "            schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "            value=\"2\"> "
      "        </AudioChannelConfiguration> "
      "    </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/init-audio.mp4", NULL, 10000},
    {"http://unit.test/audio-1.mp4", NULL, 10000},
    {"http://unit.test/audio-2.mp4", NULL, 10000},
    {"http://unit.test/audio-3.mp4", NULL, 10000},
    {"http://unit.test/audio-4.mp4", NULL, 10000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    /* 1 from the init segment */
    {"audio_00", (1 + segments) * 10000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on the first pad listed in GstAdaptiveDemuxTestOutputStreamData and the
   * first chunk of at least one byte has already arrived in AppSink
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->threshold_for_seek = 4687 + 1;

  /* FIXME hack to avoid having a 0 seqnum */
  gst_util_seqnum_next ();

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->seek_event =
      gst_event_new_seek (rate, GST_FORMAT_TIME, flags, start_type,
      seek_start, stop_type, seek_stop);

  gst_adaptive_demux_test_seek (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", GST_ADAPTIVE_DEMUX_TEST_CASE (testData));

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_START_TEST (testSeekKeyUnitPosition)
{
  /* Seek to 1.5s with key unit, it should go back to 1.0s. 3 segments will be
   * pushed */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      1000 * GST_MSECOND, -1, 3);
}

GST_END_TEST;


GST_START_TEST (testSeekUpdateStopPosition)
{
  run_seek_position_test (1.0, GST_SEEK_TYPE_NONE, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 3000 * GST_MSECOND, 0, 0, 3000 * GST_MSECOND, 3);
}

GST_END_TEST;

GST_START_TEST (testSeekPosition)
{
  /* Seek to 1.5s without key unit, it should keep the 1.5s, but still push
   * from the 1st segment, so 3 segments will be
   * pushed */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH, 1500 * GST_MSECOND, -1, 3);
}

GST_END_TEST;

GST_START_TEST (testSeekSnapBeforePosition)
{
  /* Seek to 1.5s, snap before, it go to 1s */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_BEFORE,
      1000 * GST_MSECOND, -1, 3);
}

GST_END_TEST;


GST_START_TEST (testSeekSnapAfterPosition)
{
  /* Seek to 1.5s with snap after, it should move to 2s */
  run_seek_position_test (1.0, GST_SEEK_TYPE_SET, 1500 * GST_MSECOND,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_AFTER,
      2000 * GST_MSECOND, -1, 2);
}

GST_END_TEST;


GST_START_TEST (testReverseSeekSnapBeforePosition)
{
  run_seek_position_test (-1.0, GST_SEEK_TYPE_SET, 1000 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 2500 * GST_MSECOND,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_BEFORE, 1000 * GST_MSECOND,
      3000 * GST_MSECOND, 2);
}

GST_END_TEST;


GST_START_TEST (testReverseSeekSnapAfterPosition)
{
  run_seek_position_test (-1.0, GST_SEEK_TYPE_SET, 1000 * GST_MSECOND,
      GST_SEEK_TYPE_SET, 2500 * GST_MSECOND,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SNAP_AFTER, 1000 * GST_MSECOND,
      2000 * GST_MSECOND, 1);
}

GST_END_TEST;

typedef struct _SeekTaskContext
{
  GstAppSink *appsink;
  GstTask *task;
  GstDashDemuxTestCase *testData;
} SeekTaskContext;

/* function to generate a seek event. Will be run in a separate thread */
static void
testParallelSeekTaskDoSeek (gpointer user_data)
{
  SeekTaskContext *context = (SeekTaskContext *) user_data;
  GstTask *task = context->task;

  GST_DEBUG ("testSeekTaskDoSeek calling seek");

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  if (!gst_element_seek_simple (GST_ELEMENT (context->appsink), GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 5 * GST_MSECOND)) {
    fail ("Seek failed!\n");
  }
  GST_DEBUG ("seek ok");
  g_slice_free (SeekTaskContext, context);
  gst_task_stop (task);
}

/* function to generate a seek event. Will be run in a separate thread */
static void
testParallelSeekTaskDoSeek2 (gpointer user_data)
{
  SeekTaskContext *context = (SeekTaskContext *) user_data;
  GstTask *task = context->task;
  GstAdaptiveDemuxTestCase *testData =
      GST_ADAPTIVE_DEMUX_TEST_CASE (context->testData);

  GST_DEBUG ("testSeekTaskDoSeek calling seek");

  /* seek to beginning of the file.
   * Adaptive demux supports only GST_FORMAT_TIME requests, so this should
   * return error.
   */
  if (gst_element_seek_simple (GST_ELEMENT (context->appsink), GST_FORMAT_BYTES,
          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0)) {
    fail ("seek succeeded!");
  }
  GST_DEBUG ("seek failed, expected");

  g_mutex_lock (&testData->test_task_state_lock2);
  testData->test_task_state2 = TEST_TASK_STATE_EXITING;
  g_cond_signal (&testData->test_task_state_cond2);
  g_mutex_unlock (&testData->test_task_state_lock2);

  g_slice_free (SeekTaskContext, context);
  gst_task_stop (task);
}

/* function to be called during seek test when dash sends data to AppSink
 * It monitors the data sent and after a while will generate a seek request.
 */
static gboolean
testParallelSeekDashdemuxSendsData (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstBuffer * buffer, gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);
  SeekTaskContext *seekContext;
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;
  guint index = 0;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream (testData, stream,
      &index);
  fail_unless (testOutputStreamData != NULL);

  /* first entry in testData->output_streams is the
     PAD on which to perform the seek */
  if (index == 0 &&
      stream->total_received_size +
      stream->segment_received_size >= testData->threshold_for_seek) {

    GST_DEBUG ("starting seek task");

    /* the seek was to the beginning of the file, so expect to receive
     * what we have received until now + a whole file
     */
    testOutputStreamData->expected_size +=
        stream->total_received_size + stream->segment_received_size;

    /* wait for all streams to reach their seek threshold */
    GST_TEST_UNLOCK (engine);
    gst_adaptive_demux_test_barrier_wait (&testData->barrier);
    GST_TEST_LOCK (engine);

    /* invalidate thresholdForSeek so that we do not seek again next time we receive data */
    testData->threshold_for_seek = -1;

    /* start a separate task to perform the seek */
    g_mutex_lock (&testData->test_task_state_lock);
    testData->test_task_state =
        TEST_TASK_STATE_WAITING_FOR_TESTSRC_STATE_CHANGE;
    g_mutex_unlock (&testData->test_task_state_lock);

    seekContext = g_slice_new (SeekTaskContext);
    seekContext->appsink = stream->appsink;
    seekContext->testData = GST_DASH_DEMUX_TEST_CASE (testData);
    testData->test_task = seekContext->task =
        gst_task_new ((GstTaskFunction) testParallelSeekTaskDoSeek, seekContext,
        NULL);
    gst_task_set_lock (testData->test_task, &testData->test_task_lock);
    gst_task_start (testData->test_task);

    GST_DEBUG ("seek task started");

    /* wait for seekTask to run, send a flush start event to AppSink
     * and change the fakesouphttpsrc element state from PLAYING to PAUSED
     */
    GST_TEST_UNLOCK (engine);
    g_mutex_lock (&testData->test_task_state_lock);
    GST_DEBUG ("waiting for seek task to change state on fakesrc");
    while (testData->test_task_state != TEST_TASK_STATE_EXITING) {
      g_cond_wait (&testData->test_task_state_cond,
          &testData->test_task_state_lock);
    }
    g_mutex_unlock (&testData->test_task_state_lock);
    GST_TEST_LOCK (engine);

    /* we can continue now, but this buffer will be rejected by AppSink
     * because it is in flushing mode
     */
    GST_DEBUG ("seek task changed state on fakesrc, resuming");

  } else if (index == 1 &&
      stream->total_received_size +
      stream->segment_received_size >= testData->threshold_for_seek) {

    /* the seek was to the beginning of the file, so expect to receive
     * what we have received until now + a whole file
     */
    testOutputStreamData->expected_size +=
        stream->total_received_size + stream->segment_received_size;

    /* wait for all streams to reach their seek threshold */
    GST_TEST_UNLOCK (engine);
    gst_adaptive_demux_test_barrier_wait (&testData->barrier);
    GST_TEST_LOCK (engine);

    /* wait for audio stream to seek */
    GST_TEST_UNLOCK (engine);
    g_mutex_lock (&testData->test_task_state_lock);
    GST_DEBUG ("video stream waiting for seek task to change state on fakesrc");
    while (testData->test_task_state != TEST_TASK_STATE_EXITING) {
      g_cond_wait (&testData->test_task_state_cond,
          &testData->test_task_state_lock);
    }
    g_mutex_unlock (&testData->test_task_state_lock);
    GST_TEST_LOCK (engine);
  }

  return TRUE;
}

static gboolean
testParallelSeekDashdemuxSendsEvent (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstEvent * event, gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);
  SeekTaskContext *seekContext;
  guint index = 0;

  GST_DEBUG ("received event %s", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START &&
      index == 0 && testData->test_task2 == NULL) {
    gint64 end_time;
    gboolean timeout_expired = FALSE;

    /* start the second seek while the first one is still active */
    g_mutex_lock (&testData->test_task_state_lock2);
    testData->test_task_state2 =
        TEST_TASK_STATE_WAITING_FOR_TESTSRC_STATE_CHANGE;
    g_mutex_unlock (&testData->test_task_state_lock2);

    seekContext = g_slice_new (SeekTaskContext);
    /* send the seek on the other stream appsink */
    seekContext->appsink = ((GstAdaptiveDemuxTestOutputStream *)
        g_ptr_array_index (engine->output_streams, 1))->appsink;
    seekContext->testData = GST_DASH_DEMUX_TEST_CASE (testData);
    testData->test_task2 = seekContext->task =
        gst_task_new ((GstTaskFunction) testParallelSeekTaskDoSeek2,
        seekContext, NULL);
    gst_task_set_lock (testData->test_task2, &testData->test_task_lock2);
    gst_task_start (testData->test_task2);

    /* wait for seekTask2 to run */
    end_time = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND;
    GST_TEST_UNLOCK (engine);
    g_mutex_lock (&testData->test_task_state_lock2);
    GST_DEBUG ("waiting for seek task to run and fail");
    while (testData->test_task_state2 != TEST_TASK_STATE_EXITING &&
        timeout_expired == FALSE) {
      if (!g_cond_wait_until (&testData->test_task_state_cond2,
              &testData->test_task_state_lock2, end_time)) {
        /* timeout expired, second seek was serialised
         * test is passed */
        timeout_expired = TRUE;
      }
    }
    if (testData->test_task_state2 == TEST_TASK_STATE_EXITING) {
      /* second seek succeeded while the first one was ongoing. It should have
       * been serialised.
       * The test failed
       */
      fail ("second seek was not serialised!");
    }
    g_mutex_unlock (&testData->test_task_state_lock2);
    GST_TEST_LOCK (engine);
  }

  return TRUE;
}

/* callback called when main_loop detects a state changed event */
static void
testParallelSeekOnStateChanged (GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);
  //GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;
  GstState old_state, new_state;
  const char *srcName = GST_OBJECT_NAME (msg->src);

  gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
  GST_DEBUG ("Element %s changed state from %s to %s",
      GST_OBJECT_NAME (msg->src),
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (new_state));

  if (g_str_has_prefix (srcName, "srcbin-audio") &&
      old_state == GST_STATE_PLAYING && new_state == GST_STATE_PAUSED) {
    g_mutex_lock (&testData->test_task_state_lock);
    if (testData->test_task_state ==
        TEST_TASK_STATE_WAITING_FOR_TESTSRC_STATE_CHANGE) {
      GST_DEBUG ("changing seekTaskState");
      testData->test_task_state = TEST_TASK_STATE_EXITING;
      g_cond_broadcast (&testData->test_task_state_cond);
    }
    g_mutex_unlock (&testData->test_task_state_lock);
  }
}

static void
testParallelSeekPreTestCallback (GstAdaptiveDemuxTestEngine * engine,
    gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);
  GstBus *bus;

  /* media segment starts at 4687
   * Issue a seek request after media segment has started to be downloaded
   * on the first pad listed in GstAdaptiveDemuxTestOutputStreamData and the
   * first chunk of at least one byte has already arrived in AppSink
   */
  testData->threshold_for_seek = 4687 + 1;

  gst_adaptive_demux_test_barrier_init (&testData->barrier,
      g_list_length (testData->output_streams));

  /* register a callback to listen for state change events */
  bus = gst_pipeline_get_bus (GST_PIPELINE (engine->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (testParallelSeekOnStateChanged), testData);
}

/* function to do test seek cleanup */
static void
testParallelSeekPostTestCallback (GstAdaptiveDemuxTestEngine * engine,
    gpointer user_data)
{
  GstAdaptiveDemuxTestCase *testData = GST_ADAPTIVE_DEMUX_TEST_CASE (user_data);

  fail_if (testData->test_task == NULL,
      "seek test did not create task to perform the seek");

  fail_if (testData->test_task2 == NULL,
      "seek test did not create second task to perform the seek");

  gst_adaptive_demux_test_barrier_clear (&testData->barrier);
}

/*
 * Test making 2 seek requests in parallel
 *
 * The test has 2 streams: an audio stream and a video stream
 * Each stream will download thresholdForSeek bytes and will wait for the
 * other stream to download thresholdForSeek bytes. This ensures that both
 * streams update the expectedBytes at the same time, during the flushing seek.
 * After both streams have downloaded the thresholdForSeek bytes, the audio stream
 * will run a seek request on a separate thread. Both streams will wait for this
 * seek request to start the seek process (they listen for state changes in the
 * adaptive demux src element).
 * The seek request is a flushing seek so adaptive demux will send a flush to
 * all downstream elements. The test registers a callback to listen for this
 * flush event.
 * When a flush event was detected on the audio stream (so we know the first
 * seek is in progress), the callback will run a second seek request in a
 * different task. This seek request is an invalid seek (it will try to seek
 * using GST_FORMAT_BYTES instead of supported GST_FORMAT_TIME) but this is
 * sufficient to have a seek request reach adaptive demux. If adaptive demux
 * has properly implemented the serialisation of seek requests, this second seek
 * should block. The callback that started this second seek will wait for 1
 * second to see if the seek task completes or not. If the task correctly serialise
 * on adaptive demux, the callback will timeout his wait and declare the test pass.
 * If the callback detects that the second seek task terminated, the test is failed.
 * When the second seek request serialises, the flush callback will timeout and
 * will exit, unblocking the first seek request. This manages to finish and will
 * let the second seek request run, but its run will not produce effects. The
 * streams download resumes and the test will validate at the end that the first
 * seek request was successful.
 *
 */
GST_START_TEST (testParallelSeek)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 10000},
    {"http://unit.test/video.webm", NULL, 10000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 10000, NULL},
    {"video_00", 10000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;
  test_callbacks.pre_test = testParallelSeekPreTestCallback;
  test_callbacks.post_test = testParallelSeekPostTestCallback;
  test_callbacks.demux_sent_data = testParallelSeekDashdemuxSendsData;
  test_callbacks.demux_sent_event = testParallelSeekDashdemuxSendsEvent;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

static void
testDownloadErrorMessageCallback (GstAdaptiveDemuxTestEngine * engine,
    GstMessage * msg, gpointer user_data)
{
  GError *err = NULL;
  gchar *dbg_info = NULL;

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_parse_error (msg, &err, &dbg_info);
  GST_DEBUG ("Error from element %s : %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  fail_unless_equals_string (GST_OBJECT_NAME (msg->src), DEMUX_ELEMENT_NAME);
  g_error_free (err);
  g_free (dbg_info);
  g_main_loop_quit (engine->loop);
}

/*
 * Test error case of failing to download a segment
 */
GST_START_TEST (testDownloadError)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT0.5S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio_file_not_available.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 0, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.bus_error_message = testDownloadErrorMessageCallback;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

static GstFlowReturn
test_fragment_download_error_src_create (GstTestHTTPSrc * src,
    guint64 offset,
    guint length, GstBuffer ** retbuf, gpointer context, gpointer user_data)
{
  const GstDashDemuxTestInputData *input =
      (const GstDashDemuxTestInputData *) context;
  const GstTestHTTPSrcTestData *http_src_test_data =
      (const GstTestHTTPSrcTestData *) user_data;
  guint64 threshold_for_trigger;

  fail_unless (input != NULL);
  gst_structure_get_uint64 (http_src_test_data->data, "threshold_for_trigger",
      &threshold_for_trigger);

  if (!g_str_has_suffix (input->uri, ".mpd") && offset >= threshold_for_trigger) {
    GST_DEBUG ("network_error %s %" G_GUINT64_FORMAT " @ %" G_GUINT64_FORMAT,
        input->uri, offset, threshold_for_trigger);
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (("A network error occurred, or the server closed the connection unexpectedly.")), ("A network error occurred, or the server closed the connection unexpectedly."));
    return GST_FLOW_ERROR;
  }
  return gst_dashdemux_http_src_create (src, offset, length, retbuf, context,
      user_data);
}

/*
 * Test header download error
 * Let the adaptive demux download a few bytes, then instruct the
 * GstTestHTTPSrc element to generate an error while the fragment header
 * is still being downloaded.
 */
GST_START_TEST (testHeaderDownloadError)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT0.5S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  /* generate error while the headers are still being downloaded
   * threshold_for_trigger must be less than the size of headers
   * (initialization + index) which is 4687.
   */
  guint64 threshold_for_trigger = 2000;

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    /* adaptive demux tries for 4 times (MAX_DOWNLOAD_ERROR_COUNT + 1) before giving up */
    {"audio_00", threshold_for_trigger * 4, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = test_fragment_download_error_src_create;
  http_src_test_data.data = gst_structure_new_empty (__FUNCTION__);
  gst_structure_set (http_src_test_data.data, "threshold_for_trigger",
      G_TYPE_UINT64, threshold_for_trigger, NULL);
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;
  test_callbacks.bus_error_message = testDownloadErrorMessageCallback;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  /* download in chunks of threshold_for_trigger size.
   * This means the first chunk will succeed, the second will generate
   * error because we already exceeded threshold_for_trigger bytes.
   */
  gst_test_http_src_set_default_blocksize (threshold_for_trigger);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/*
 * Test media download error on the last media fragment.
 * Let the adaptive demux download a few bytes, then instruct the
 * GstTestHTTPSrc element to generate an error while the last media fragment
 * is being downloaded.
 * Adaptive demux will not retry downloading the last media fragment. It will
 * be considered eos.
 */
GST_START_TEST (testMediaDownloadErrorLastFragment)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT0.5S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  /* generate error on the first media fragment */
  guint64 threshold_for_trigger = 4687;

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    /* adaptive demux will not retry because this is the last fragment */
    {"audio_00", threshold_for_trigger, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = test_fragment_download_error_src_create;
  http_src_test_data.data = gst_structure_new_empty (__FUNCTION__);
  gst_structure_set (http_src_test_data.data, "threshold_for_trigger",
      G_TYPE_UINT64, threshold_for_trigger, NULL);
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/*
 * Test media download error on a media fragment which is not the last one.
 * Let the adaptive demux download a few bytes, then instruct the
 * GstTestHTTPSrc element to generate an error while a media fragment
 * is being downloaded.
 */
GST_START_TEST (testMediaDownloadErrorMiddleFragment)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT10S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentList duration=\"1\">"
      "          <SegmentURL indexRange=\"1-10\""
      "                      mediaRange=\"11-30\">"
      "          </SegmentURL>"
      "          <SegmentURL indexRange=\"31-60\""
      "                      mediaRange=\"61-100\">"
      "          </SegmentURL>"
      "          <SegmentURL indexRange=\"101-150\""
      "                      mediaRange=\"151-210\">"
      "          </SegmentURL>"
      "        </SegmentList>"
      "      </Representation></AdaptationSet></Period></MPD>";

  /* generate error on the second media fragment */
  guint64 threshold_for_trigger = 31;

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    /* adaptive demux will download only the first media fragment */
    {"audio_00", 20, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = test_fragment_download_error_src_create;
  http_src_test_data.data = gst_structure_new_empty (__FUNCTION__);
  gst_structure_set (http_src_test_data.data, "threshold_for_trigger",
      G_TYPE_UINT64, threshold_for_trigger, NULL);
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;
  test_callbacks.bus_error_message = testDownloadErrorMessageCallback;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/* generate queries to adaptive demux */
static gboolean
testQueryCheckDataReceived (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstBuffer * buffer, gpointer user_data)
{
  GList *pads;
  GstPad *pad;
  GstQuery *query;
  gboolean ret;
  gint64 duration;
  gboolean seekable;
  gint64 segment_start;
  gint64 segment_end;
  gboolean live;
  GstClockTime min_latency;
  GstClockTime max_latency;
  gchar *uri;
  gchar *redirect_uri;
  gboolean redirect_permanent;

  pads = GST_ELEMENT_PADS (stream->appsink);

  /* AppSink should have only 1 pad */
  fail_unless (pads != NULL);
  fail_unless (g_list_length (pads) == 1);
  pad = GST_PAD (pads->data);

  /* duration query */
  query = gst_query_new_duration (GST_FORMAT_TIME);
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_duration (query, NULL, &duration);
  /* mediaPresentationDuration=\"PT135.743S\" */
  fail_unless (duration == 135743 * GST_MSECOND);
  gst_query_unref (query);

  /* seek query */
  query = gst_query_new_seeking (GST_FORMAT_TIME);
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
      &segment_end);
  fail_unless (seekable == TRUE);
  fail_unless (segment_start == 0);
  fail_unless (segment_end == duration);
  gst_query_unref (query);

  /* latency query */
  query = gst_query_new_latency ();
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_latency (query, &live, &min_latency, &max_latency);
  fail_unless (live == FALSE);
  fail_unless (min_latency == 0);
  fail_unless (max_latency == -1);
  gst_query_unref (query);

  /* uri query */
  query = gst_query_new_uri ();
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_uri (query, &uri);
  gst_query_parse_uri_redirection (query, &redirect_uri);
  gst_query_parse_uri_redirection_permanent (query, &redirect_permanent);
  fail_unless (g_strcmp0 (uri, "http://unit.test/test.mpd") == 0);
  /* adaptive demux does not reply with redirect information */
  fail_unless (redirect_uri == NULL);
  fail_unless (redirect_permanent == FALSE);
  g_free (uri);
  g_free (redirect_uri);
  gst_query_unref (query);

  return gst_adaptive_demux_test_check_received_data (engine,
      stream, buffer, user_data);
}

/*
 * Test queries
 *
 */
GST_START_TEST (testQuery)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testQueryCheckDataReceived;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME,
      "http://unit.test/test.mpd", &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

static gboolean
testContentProtectionDashdemuxSendsEvent (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstEvent * event, gpointer user_data)
{
  GstDashDemuxTestCase *test_case = GST_DASH_DEMUX_TEST_CASE (user_data);
  const gchar *system_id;
  GstBuffer *data;
  const gchar *origin;
  GstMapInfo info;
  gchar *value;
  gchar *name;
  guint event_count = 0;

  GST_DEBUG ("received event %s", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_TYPE (event) != GST_EVENT_PROTECTION) {
    return TRUE;
  }

  /* we expect content protection events only on video pad */
  name = gst_pad_get_name (stream->pad);
  fail_unless (g_strcmp0 (name, "video_00") == 0);
  gst_event_parse_protection (event, &system_id, &data, &origin);

  gst_buffer_map (data, &info, GST_MAP_READ);

  value = g_malloc (info.size + 1);
  strncpy (value, (gchar *) info.data, info.size);
  value[info.size] = 0;
  gst_buffer_unmap (data, &info);

  if (g_strcmp0 (system_id, "11111111-aaaa-bbbb-cccc-123456789abc") == 0) {
    fail_unless (g_strcmp0 (origin, "dash/mpd") == 0);
    fail_unless (g_strcmp0 (value, "test value") == 0);
  } else if (g_strcmp0 (system_id, "5e629af5-38da-4063-8977-97ffbd9902d4") == 0) {
    const gchar *str;

    fail_unless (g_strcmp0 (origin, "dash/mpd") == 0);

    /* We can't do a simple compare of value (which should be an XML dump
       of the ContentProtection element), because the whitespace
       formatting from xmlDump might differ between versions of libxml */
    str = strstr (value, "<ContentProtection");
    fail_if (str == NULL);
    str = strstr (value, "<mas:MarlinContentIds>");
    fail_if (str == NULL);
    str = strstr (value, "<mas:MarlinContentId>");
    fail_if (str == NULL);
    str = strstr (value, "urn:marlin:kid:02020202020202020202020202020202");
    fail_if (str == NULL);
    str = strstr (value, "</ContentProtection>");
    fail_if (str == NULL);
  } else {
    fail ("unexpected content protection event '%s'", system_id);
  }

  g_free (value);

  fail_if (test_case->countContentProtectionEvents == NULL);
  gst_structure_get_uint (test_case->countContentProtectionEvents, name,
      &event_count);
  event_count++;
  gst_structure_set (test_case->countContentProtectionEvents, name, G_TYPE_UINT,
      event_count, NULL);

  g_free (name);
  return TRUE;
}

/*
 * Test content protection
 * Configure 3 content protection sources:
 * - a uuid scheme/value pair
 * - a non uuid scheme/value pair (dash recognises only uuid schemes)
 * - a complex uuid scheme, with trailing spaces and capital letters in scheme uri
 * Only the uuid scheme will be recognised. We expect to receive 2 content
 * protection events
 */
GST_START_TEST (testContentProtection)
{
  const gchar *mpd =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\""
      "     type=\"static\""
      "     minBufferTime=\"PT1.500S\""
      "     mediaPresentationDuration=\"PT135.743S\">"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <BaseURL>audio.webm</BaseURL>"
      "        <SegmentBase indexRange=\"4452-4686\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-4451\" />"
      "        </SegmentBase>"
      "      </Representation>"
      "    </AdaptationSet>"
      "    <AdaptationSet mimeType=\"video/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <ContentProtection schemeIdUri=\"urn:uuid:11111111-AAAA-BBBB-CCCC-123456789ABC\" value=\"test value\"/>"
      "      <ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/>"
      "      <ContentProtection schemeIdUri=\" URN:UUID:5e629af5-38da-4063-8977-97ffbd9902d4\" xmlns:mas=\"urn:marlin:mas:1-0:services:schemas:mpd\">"
      "        <mas:MarlinContentIds>"
      "          <mas:MarlinContentId>urn:marlin:kid:02020202020202020202020202020202</mas:MarlinContentId>"
      "        </mas:MarlinContentIds>"
      "      </ContentProtection>"
      "      <Representation id=\"242\""
      "                      codecs=\"vp9\""
      "                      width=\"426\""
      "                      height=\"240\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"490208\">"
      "        <BaseURL>video.webm</BaseURL>"
      "        <SegmentBase indexRange=\"234-682\""
      "                     indexRangeExact=\"true\">"
      "          <Initialization range=\"0-233\" />"
      "        </SegmentBase>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", (guint8 *) mpd, 0},
    {"http://unit.test/audio.webm", NULL, 5000},
    {"http://unit.test/video.webm", NULL, 9000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 5000, NULL},
    {"video_00", 9000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  guint event_count = 0;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      gst_adaptive_demux_test_check_received_data;
  test_callbacks.appsink_eos =
      gst_adaptive_demux_test_check_size_of_received_data;
  test_callbacks.demux_sent_event = testContentProtectionDashdemuxSendsEvent;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->countContentProtectionEvents =
      gst_structure_new_empty ("countContentProtectionEvents");
  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  fail_unless (gst_structure_has_field_typed
      (testData->countContentProtectionEvents, "video_00", G_TYPE_UINT));

  gst_structure_get_uint (testData->countContentProtectionEvents, "video_00",
      &event_count);
  fail_unless (event_count == 2);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
}

GST_END_TEST;

/* function to validate data received by AppSink */
static gboolean
testLiveStreamCheckDataReceived (GstAdaptiveDemuxTestEngine *
    engine, GstAdaptiveDemuxTestOutputStream * stream, GstBuffer * buffer,
    gpointer user_data)
{
  GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (user_data);
  GstAdaptiveDemuxTestCase *commonData =
      GST_ADAPTIVE_DEMUX_TEST_CASE (testData);
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream
      (GST_ADAPTIVE_DEMUX_TEST_CASE (testData), stream, NULL);
  fail_unless (testOutputStreamData != NULL);

  gst_adaptive_demux_test_check_received_data (engine, stream, buffer,
      user_data);

  {
    GstQuery *query;
    GList *pads;
    GstPad *pad;
    gboolean ret;
    gboolean seekable;
    gint64 segment_start;
    gint64 segment_end;
    GstClockTime streamTime;

    pads = GST_ELEMENT_PADS (stream->appsink);

    /* AppSink should have only 1 pad */
    fail_unless (pads != NULL);
    fail_unless (g_list_length (pads) == 1);
    pad = GST_PAD (pads->data);

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    ret = gst_pad_peer_query (pad, query);
    fail_unless (ret == TRUE);
    gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
        &segment_end);
    fail_unless (seekable == TRUE);
    if (testData->timeshiftBufferDepth == -1) {
      /* infinite timeshift buffer, start should be 0 */
      fail_unless (segment_start == 0);
    } else {
      fail_unless (segment_start + testData->timeshiftBufferDepth ==
          segment_end);
    }

    streamTime = getCurrentPresentationTime (testData->availabilityStartTime,
        testData->clockCompensation);

    if (stream->total_received_size == 0) {
      /* this is the first segment that is downloaded. It should be segment 3.
       * Segment 3 starts to be available at its end time
       * (segment availability time is 9s).
       * So, a seek query during the download of segment 3 should have a start time 0
       * and end time between second 9 and current time.
       * Ideally, end time should be current time - segment duration, but adaptive
       * demux returns current time as segment stop.
       * See https://bugzilla.gnome.org/show_bug.cgi?id=753751
       */
      fail_unless (segment_end >= 9 * GST_SECOND && segment_end <= streamTime);
    } else if (stream->total_received_size == 3000) {
      /* this is the second segment that is downloaded. It should be segment 4.
       * Segment 4 starts to be available at its end time
       * (segment availability time is 12s).
       */
      fail_unless (segment_end >= 12 * GST_SECOND && segment_end <= streamTime);
    } else {
      fail ("unexpected totalReceivedSize");
    }

    gst_query_unref (query);
  }

  /* for a live stream, no EOS signal is sent, so we must monitor the amount
   * of data received.
   */
  if (stream->total_received_size +
      stream->segment_received_size +
      gst_buffer_get_size (buffer) == testOutputStreamData->expected_size) {

    /* signal to the application that another stream has finished */
    commonData->count_of_finished_streams++;

    if (commonData->count_of_finished_streams ==
        g_list_length (commonData->output_streams)) {
      g_main_loop_quit (engine->loop);
    }
  }

  return TRUE;
}

/*
 * Test a live stream mpd with an audio stream.
 *
 * There are 4 segments, 3s each.
 * We set the mpd availability 6s before now.
 * We expect to download the last 2 segments, and to wait for the first one to
 * be available.
 *
 */
GST_START_TEST (testLiveStream)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://mocktime/http-xsdate\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "0", 0}, /* server is 0s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 3000 + 4000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testLiveStreamCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

/* function to validate data received by AppSink */
static gboolean
testLiveStreamPresentationDelayCheckDataReceived (GstAdaptiveDemuxTestEngine *
    engine, GstAdaptiveDemuxTestOutputStream * stream, GstBuffer * buffer,
    gpointer user_data)
{
  GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (user_data);
  GstAdaptiveDemuxTestCase *commonData =
      GST_ADAPTIVE_DEMUX_TEST_CASE (testData);
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream
      (GST_ADAPTIVE_DEMUX_TEST_CASE (testData), stream, NULL);
  fail_unless (testOutputStreamData != NULL);

  gst_adaptive_demux_test_check_received_data (engine, stream, buffer,
      user_data);

  {
    GstQuery *query;
    GList *pads;
    GstPad *pad;
    gboolean ret;
    gboolean seekable;
    gint64 segment_start;
    gint64 segment_end;
    GstClockTime streamTime;

    pads = GST_ELEMENT_PADS (stream->appsink);

    /* AppSink should have only 1 pad */
    fail_unless (pads != NULL);
    fail_unless (g_list_length (pads) == 1);
    pad = GST_PAD (pads->data);

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    ret = gst_pad_peer_query (pad, query);
    fail_unless (ret == TRUE);
    gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
        &segment_end);
    fail_unless (seekable == TRUE);
    if (testData->timeshiftBufferDepth == -1) {
      /* infinite timeshift buffer, start should be 0 */
      fail_unless (segment_start == 0);
    } else {
      fail_unless (segment_start + testData->timeshiftBufferDepth ==
          segment_end);
    }

    streamTime = getCurrentPresentationTime (testData->availabilityStartTime,
        testData->clockCompensation);

    if (stream->total_received_size == 0) {
      /* this is the first segment that is downloaded. It should be segment 2.
       * Segment 2 starts to be available at its end time
       * (segment availability time is 6s).
       * But that is in the past (current time is 6.xx seconds, because we have
       * 2s presentation delay), so we do not need to wait for it to be available.
       * A seek query during the download of segment 2 should have a start time 0
       * and end time 6 seconds (end of segment 2).
       */
      fail_unless (segment_end >= 6 * GST_SECOND && segment_end <= streamTime);
    } else if (stream->total_received_size == 2000) {
      /* this is the second segment that is downloaded. It should be segment 3.
       */
      fail_unless (segment_end >= 9 * GST_SECOND && segment_end <= streamTime);
    } else if (stream->total_received_size == 2000 + 3000) {
      fail_unless (segment_end >= 12 * GST_SECOND && segment_end <= streamTime);
    } else {
      fail ("unexpected totalReceivedSize");
    }
    gst_query_unref (query);
  }

  /* for a live stream, no EOS signal is sent, so we must monitor the amount
   * of data received.
   */
  if (stream->total_received_size +
      stream->segment_received_size +
      gst_buffer_get_size (buffer) == testOutputStreamData->expected_size) {

    /* signal to the application that another stream has finished */
    commonData->count_of_finished_streams++;

    if (commonData->count_of_finished_streams ==
        g_list_length (commonData->output_streams)) {
      g_main_loop_quit (engine->loop);
    }
  }

  return TRUE;
}

/*
 * Test a live stream mpd with an audio stream.
 *
 * There are 4 segments, 3s each.
 * We set the mpd availability 6s before now.
 * SuggestedPresentationDelay is 3s, so we expect to download the last 3 segments
 * without waiting for the first one.
 * The test should take a little over 6s.
 *
 */
GST_START_TEST (testLiveStreamPresentationDelay)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     suggestedPresentationDelay=\"PT3S\""
      "     timeShiftBufferDepth=\"PT5S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://mocktime/http-xsdate\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "0", 0}, /* server is 0s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 2000 + 3000 + 4000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data =
      testLiveStreamPresentationDelayCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;
  testData->timeshiftBufferDepth = 5 * GST_SECOND;

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

/* generate queries to adaptive demux */
static gboolean
testQueryLiveStreamCheckDataReceived (GstAdaptiveDemuxTestEngine *
    engine, GstAdaptiveDemuxTestOutputStream * stream, GstBuffer * buffer,
    gpointer user_data)
{
  GList *pads;
  GstPad *pad;
  GstQuery *query;
  gboolean ret;
  gboolean live;
  GstClockTime min_latency;
  GstClockTime max_latency;
  gchar *uri;
  gchar *redirect_uri;
  gboolean redirect_permanent;

  pads = GST_ELEMENT_PADS (stream->appsink);

  /* AppSink should have only 1 pad */
  fail_unless (pads != NULL);
  fail_unless (g_list_length (pads) == 1);
  pad = GST_PAD (pads->data);

  /* duration query for a live stream should fail */
  query = gst_query_new_duration (GST_FORMAT_TIME);
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == FALSE);
  gst_query_unref (query);

  /* latency query */
  query = gst_query_new_latency ();
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_latency (query, &live, &min_latency, &max_latency);
  fail_unless (live == FALSE);
  fail_unless (min_latency == 0);
  fail_unless (max_latency == -1);
  gst_query_unref (query);

  /* uri query */
  query = gst_query_new_uri ();
  ret = gst_pad_peer_query (pad, query);
  fail_unless (ret == TRUE);
  gst_query_parse_uri (query, &uri);
  gst_query_parse_uri_redirection (query, &redirect_uri);
  gst_query_parse_uri_redirection_permanent (query, &redirect_permanent);
  fail_unless (g_strcmp0 (uri, "http://unit.test/test.mpd") == 0);
  /* adaptive demux does not reply with redirect information */
  fail_unless (redirect_uri == NULL);
  fail_unless (redirect_permanent == FALSE);
  g_free (uri);
  g_free (redirect_uri);
  gst_query_unref (query);

  /* seek query is tested by testLiveStreamCheckDataReceived */
  return testLiveStreamCheckDataReceived (engine, stream, buffer, user_data);
}

/*
 * Test a live stream mpd with an audio stream.
 *
 * There are 4 segments, 3s each.
 * We set the mpd availability 6s before now.
 * We expect to download the last 2 segments, and to wait for the first one to
 * be available
 *
 */
GST_START_TEST (testQueryLiveStream)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://mocktime/http-xsdate\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "0", 0}, /* server is 0s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 3000 + 4000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testQueryLiveStreamCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

/* function to validate data received by AppSink */
static gboolean
testSeekLiveStreamCheckDataReceived (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstBuffer * buffer, gpointer user_data)
{
  GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (user_data);
  GstAdaptiveDemuxTestCase *commonData =
      GST_ADAPTIVE_DEMUX_TEST_CASE (testData);
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream
      (GST_ADAPTIVE_DEMUX_TEST_CASE (testData), stream, NULL);
  fail_unless (testOutputStreamData != NULL);

  gst_adaptive_demux_test_check_received_data (engine, stream, buffer,
      user_data);

  {
    GstQuery *query;
    GList *pads;
    GstPad *pad;
    gboolean ret;
    gboolean seekable;
    gint64 segment_start;
    gint64 segment_end;
    GstClockTime streamTime;

    pads = GST_ELEMENT_PADS (stream->appsink);

    /* AppSink should have only 1 pad */
    fail_unless (pads != NULL);
    fail_unless (g_list_length (pads) == 1);
    pad = GST_PAD (pads->data);

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    ret = gst_pad_peer_query (pad, query);
    fail_unless (ret == TRUE);
    gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
        &segment_end);
    fail_unless (seekable == TRUE);
    if (testData->timeshiftBufferDepth == -1) {
      /* infinite timeshift buffer, start should be 0 */
      fail_unless (segment_start == 0);
    } else {
      fail_unless (segment_start + testData->timeshiftBufferDepth ==
          segment_end);
    }

    streamTime = getCurrentPresentationTime (testData->availabilityStartTime,
        testData->clockCompensation);

    if (stream->total_received_size == 0) {
      /* this is the first segment that is downloaded. It should be segment 3.
       * Segment 3 starts to be available at its end time
       * (segment availability time is 9s).
       * So, a seek query during the download of segment 3 should have a start time 0
       * and end time between second 9 and current time.
       * Ideally, end time should be current time - segment duration, but adaptive
       * demux returns current time as segment stop.
       * See https://bugzilla.gnome.org/show_bug.cgi?id=753751
       */
      fail_unless (segment_end >= 9 * GST_SECOND && segment_end <= streamTime);
    } else if (stream->total_received_size == commonData->threshold_for_seek) {
      /* this is the first segment that is downloaded after seek.
       * It should be segment 1.
       */
      fail_unless (segment_end >= 9 * GST_SECOND && segment_end <= streamTime);
    } else if (stream->total_received_size ==
        commonData->threshold_for_seek + 1000) {
      /* this is the second segment that is downloaded after seek.
       * It should be segment 2.
       * It is already available, so it is downloaded immediately after
       * segment 1.
       */
      fail_unless (segment_end >= 9 * GST_SECOND && segment_end <= streamTime);
    } else {
      fail ("unexpected totalReceivedSize");
    }
    gst_query_unref (query);
  }

  /* for a live stream, no EOS signal is sent, so we must monitor the amount
   * of data received.
   */
  if (stream->total_received_size +
      stream->segment_received_size +
      gst_buffer_get_size (buffer) == testOutputStreamData->expected_size) {

    /* signal to the application that another stream has finished */
    commonData->count_of_finished_streams++;

    if (commonData->count_of_finished_streams ==
        g_list_length (commonData->output_streams)) {
      g_main_loop_quit (engine->loop);
    }
  }

  return TRUE;
}

/*
 * Test seek during live stream.
 *
 * There are 4 segments, 3s each. Segment 1 starts at 0s, segment 2 starts at 3s,
 * segment 3 starts at 6s, segment 4 starts at 9s.
 * We set the mpd availability 6s before now.
 * Adaptive demux will automatically seek and start playing the segment
 * corresponding to the current time. That is segment 3.
 * Because segment 3 will be available at its end time (9s) adaptive demux
 * will wait 2+ seconds for it to be available.
 * After segment3 starts to be downloaded, the test will issue a seek request to
 * the beginning of the stream. The test will wait for the first 2 segments
 * to be downloaded and then will terminate.
 *
 */
GST_START_TEST (testSeekLiveStream)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://mocktime/http-xsdate\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "0", 0}, /* server is 0s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  /* the test will seek to the beginning of the stream and we will allow it to
   * download the first 2 segments, so we expect to receive
   * thresholdForSeek + size of the first 2 segments.
   * We configure here the size of the first 2 segments and the test will update
   * expectedSize during seek with the amount of bytes downloaded when the seek
   * occurred
   */
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 1000 + 2000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testSeekLiveStreamCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;
  test_callbacks.pre_test = testSeekPreTestCallback;
  test_callbacks.post_test = testSeekPostTestCallback;
  test_callbacks.demux_sent_data = testSeekAdaptiveDemuxSendsData;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);

  testData->availabilityStartTime = availabilityStartTime;

  /* first segment to be downloaded has size 3000
   * Issue a seek request after first segment has started to be downloaded
   * on audio_00 stream and the first chunk of GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE
   * has already arrived in AppSink
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->threshold_for_seek = 1;

  /* seek to 5ms.
   * Because there is only one fragment, we expect the whole file to be
   * downloaded again
   */
  GST_ADAPTIVE_DEMUX_TEST_CASE (testData)->seek_event =
      gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_SET,
      5 * GST_MSECOND, GST_SEEK_TYPE_NONE, 0);

  /* download in small chunks to allow seek to happen during the file download.
   * The first file to be downloaded has size 3000 (segment 3) so blocksize
   * must be smaller than this.
   * It also needs to be greater than the first segment size (1000) so that
   * the test can detect when the second segment is being downloaded.
   * TODO: improve recognition of segment currently downloaded.
   */
  gst_test_http_src_set_default_blocksize (1024);

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, GST_ADAPTIVE_DEMUX_TEST_CASE (testData));

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

/**
 * gst_dashdemux_test_download_once_src_start:
 * A version of the src_start function that only allows an entry from
 * GstTestHTTPSrcInput to be used once. Useful for cases such as
 * returning a different manifest when performing a refresh of a live
 * stream.
 */
static gboolean
gst_dashdemux_test_download_once_src_start (GstTestHTTPSrc * src,
    const gchar * uri, GstTestHTTPSrcInput * input_data, gpointer user_data)
{
  const GstTestHTTPSrcTestData *test_case =
      (const GstTestHTTPSrcTestData *) user_data;
  guint64 once_mask = 0;
  guint64 always_mask = 0;

  gst_structure_get (test_case->data, "download-once",
      GST_TYPE_BITMASK, &once_mask, NULL);
  gst_structure_get (test_case->data, "download-always",
      GST_TYPE_BITMASK, &always_mask, NULL);
  for (guint i = 0; test_case->input[i].uri; ++i) {
    if (strcmp (test_case->input[i].uri, uri) == 0) {
      guint64 bitpos = G_GUINT64_CONSTANT (1) << i;
      if ((once_mask & bitpos) && !(always_mask & bitpos)) {
        GST_DEBUG ("already used entry %d", i);
        continue;
      }
      input_data->context = (gpointer) & test_case->input[i];
      input_data->size = test_case->input[i].size;
      if (test_case->input[i].size == 0)
        input_data->size = strlen ((gchar *) test_case->input[i].payload);
      GST_DEBUG ("open URI %d: %s", i, uri);
      once_mask |= bitpos;
      gst_structure_set (test_case->data, "download-once",
          GST_TYPE_BITMASK, once_mask, NULL);
      return TRUE;
    }
  }
  return FALSE;
}

/* function to validate data received by AppSink */
static gboolean
testClockCompensationCheckDataReceived (GstAdaptiveDemuxTestEngine * engine,
    GstAdaptiveDemuxTestOutputStream * stream,
    GstBuffer * buffer, gpointer user_data)
{
  GstDashDemuxTestCase *testData = GST_DASH_DEMUX_TEST_CASE (user_data);
  GstAdaptiveDemuxTestCase *commonData =
      GST_ADAPTIVE_DEMUX_TEST_CASE (testData);
  GstAdaptiveDemuxTestExpectedOutput *testOutputStreamData;

  testOutputStreamData =
      gst_adaptive_demux_test_find_test_data_by_stream (commonData, stream,
      NULL);
  fail_unless (testOutputStreamData != NULL);

  gst_adaptive_demux_test_check_received_data (engine, stream, buffer,
      user_data);

  {
    GstQuery *query;
    GList *pads;
    GstPad *pad;
    gboolean ret;
    gboolean seekable;
    gint64 segment_start;
    gint64 segment_end;
    GstClockTime streamTime;

    pads = GST_ELEMENT_PADS (stream->appsink);

    /* AppSink should have only 1 pad */
    fail_unless (pads != NULL);
    fail_unless (g_list_length (pads) == 1);
    pad = GST_PAD (pads->data);

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    ret = gst_pad_peer_query (pad, query);
    fail_unless (ret == TRUE);
    gst_query_parse_seeking (query, NULL, &seekable, &segment_start,
        &segment_end);
    fail_unless (seekable == TRUE);
    if (testData->timeshiftBufferDepth == -1) {
      /* infinite timeshift buffer, start should be 0 */
      fail_unless (segment_start == 0);
    } else {
      fail_unless (segment_start + testData->timeshiftBufferDepth ==
          segment_end);
    }

    streamTime = getCurrentPresentationTime (testData->availabilityStartTime,
        testData->clockCompensation);

    if (stream->total_received_size == 0) {
      /* this is the first segment that is downloaded. It should be segment 4.
       * Segment 4 starts to be available at its end time
       * (segment availability time is 12s).
       * So, a seek query during the download of segment 4 should have a start time 0
       * and end time between second 12 and current time.
       * Ideally, end time should be current time - segment duration, but adaptive
       * demux returns current time as segment stop.
       * See https://bugzilla.gnome.org/show_bug.cgi?id=753751
       */
      fail_unless (segment_end >= 12 * GST_SECOND && segment_end <= streamTime);
    }
    gst_query_unref (query);
  }

  /* for a live stream, no EOS signal is sent, so we must monitor the amount
   * of data received.
   */
  if (stream->total_received_size +
      stream->segment_received_size +
      gst_buffer_get_size (buffer) == testOutputStreamData->expected_size) {

    /* signal to the application that another stream has finished */
    commonData->count_of_finished_streams++;

    if (commonData->count_of_finished_streams ==
        g_list_length (commonData->output_streams)) {
      g_main_loop_quit (engine->loop);
    }
  }

  return TRUE;
}

static void
run_clock_compensation_http_xsdate_test (guint minimumUpdatePeriod)
{
  gchar *mpd;
  const gchar *mpd_fmt =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"%s\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT%dS\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://mocktime/http-xsdate\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "3", 0}, /* server is 3s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {"http://unit.test/audio5.webm", NULL, 4100},
    {"http://unit.test/audio6.webm", NULL, 4200},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 4000 + 4100 + 4200, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;
  guint access_count = 0;

  clock = gst_test_clock_new_with_start_time (GST_SECOND * 3600);
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd =
      g_strdup_printf (mpd_fmt, availabilityStartTimeString,
      minimumUpdatePeriod);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  http_src_test_data.data = gst_structure_new_empty (__FUNCTION__);
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testClockCompensationCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;
  testData->clockCompensation = 3;      /* server is 3s ahead */

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  fail_unless (gst_structure_has_field_typed
      (http_src_test_data.data, "http-xsdate-request-count", G_TYPE_UINT));
  gst_structure_get_uint (http_src_test_data.data, "http-xsdate-request-count",
      &access_count);
  assert_equals_int (access_count, 1);
  g_object_unref (testData);
  gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

/*
 * Test clock compensation during a live stream.
 *
 * There are 6 segments, 3s each.
 * We set the mpd availability 6s before now.
 * The server is 3s ahead of the client, which means it is currently generating
 * segment 4.
 * We expect the client to download segment 4 and to wait for it to
 * be available.
 *
 */
GST_START_TEST (testClockCompensationHttpXSdate)
{
  run_clock_compensation_http_xsdate_test (500);
}

GST_END_TEST;

/*
 * Test clock compensation during a live stream, where the manifest
 * updates during the test. This is to test that a refresh of the
 * manifest does not cause extra requests to the time server.
 *
 * There are 6 segments, 3s each.
 * We set the mpd availability 6s before now.
 * The server is 3s ahead of the client, which means it is currently generating
 * segment 4.
 * We expect the client to download segment 4 and to wait for it to
 * be available.
 *
 */
GST_START_TEST (testClockCompensationHttpXSdateWithManifestRefresh)
{
  run_clock_compensation_http_xsdate_test (3);
}

GST_END_TEST;

/*
 * Test clock compensation during a live stream, where the manifest
 * updates during the test. This is to test that a refresh of the
 * manifest that changes the supported UTC timing method causes dashdemux
 * to switch to the new method.
 *
 */
GST_START_TEST (testClockCompensationMethodChange)
{
  const gchar *mpd_fmt =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"%s\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT3S\">"
      "  <UTCTiming schemeIdUri=\"%s\" value=\"%s\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  gchar *mpd1, *mpd2;
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-xsdate", (guint8 *) "3", 0}, /* server is 3s ahead */
    {"http://mocktime/http-ntp", (guint8 *) "3", 0},    /* server is 3s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {"http://unit.test/audio5.webm", NULL, 4100},
    {"http://unit.test/audio6.webm", NULL, 4200},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 4000 + 4100 + 4200, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;
  guint access_count = 0;
  const guint64 download_always_mask = ~1;

  clock = gst_test_clock_new_with_start_time (GST_SECOND * 3600);
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd1 =
      g_strdup_printf (mpd_fmt, availabilityStartTimeString,
      "urn:mpeg:dash:utc:http-xsdate:2014", "http://mocktime/http-xsdate");
  mpd2 =
      g_strdup_printf (mpd_fmt, availabilityStartTimeString,
      "urn:mpeg:dash:utc:http-ntp:2014", "http://mocktime/http-ntp");
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd1;
  inputTestData[1].payload = (guint8 *) mpd2;

  http_src_callbacks.src_start = gst_dashdemux_test_download_once_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  http_src_test_data.data = gst_structure_new (__FUNCTION__,
      "download-always", GST_TYPE_BITMASK, download_always_mask, NULL);
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testClockCompensationCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;
  testData->clockCompensation = 3;      /* server is 3s ahead */

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  fail_unless (gst_structure_has_field_typed
      (http_src_test_data.data, "http-xsdate-request-count", G_TYPE_UINT));
  gst_structure_get_uint (http_src_test_data.data, "http-xsdate-request-count",
      &access_count);
  assert_equals_int (access_count, 1);
  fail_unless (gst_structure_has_field_typed
      (http_src_test_data.data, "http-ntp-request-count", G_TYPE_UINT));
  gst_structure_get_uint (http_src_test_data.data, "http-ntp-request-count",
      &access_count);
  assert_equals_int (access_count, 1);
  g_object_unref (testData);
  gst_structure_free (http_src_test_data.data);
  g_free (mpd1);
  g_free (mpd2);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

static gboolean
testClockCompensationHttpHead_http_src_start (GstTestHTTPSrc * src,
    const gchar * uri, GstTestHTTPSrcInput * input_data, gpointer user_data)
{
  gboolean ret;

  ret = gst_dashdemux_http_src_start (src, uri, input_data, user_data);
  if (ret && g_strcmp0 (uri, "http://mocktime/http-head") == 0) {
    GDateTime *now;
    GDateTime *new_time;
    gchar *date_str;
    const GstDashDemuxTestInputData *test_input_data =
        (const GstDashDemuxTestInputData *) input_data->context;

    now = gst_dash_demux_test_get_current_time ();
    fail_unless (now != NULL);
    new_time =
        g_date_time_add_seconds (now,
        atoi ((const char *) test_input_data->payload));
    fail_unless (new_time != NULL);
    g_date_time_unref (now);
    date_str = g_date_time_format (new_time, "%a, %e %b %Y %T %Z");
    fail_unless (date_str != NULL);
    g_date_time_unref (new_time);
    input_data->response_headers =
        gst_structure_new (TEST_HTTP_SRC_RESPONSE_HEADERS_NAME, "Date",
        G_TYPE_STRING, date_str, NULL);
    fail_unless (input_data->response_headers != NULL);
    g_free (date_str);
  }
  return ret;
}

/*
 * Test clock compensation during a live stream.
 *
 * There are 4 segments, 3s each.
 * We set the mpd availability 6s before now.
 * The server is 3-4s ahead of the client (HTTP HEAD does not retrieve
 * milliseconds, so its precision is 1s), which means it is currently generating
 * segment 4.
 * We expect the client to download segment 4 and to wait for it to
 * be available
 *
 */
GST_START_TEST (testClockCompensationHttpHead)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-head:2014\" value=\"http://mocktime/http-head\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-head", (guint8 *) "4", 0},   /* server is 3-4s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 4000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = testClockCompensationHttpHead_http_src_start;
  http_src_callbacks.src_create = gst_dashdemux_http_src_create;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testClockCompensationCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;
  testData->clockCompensation = 4;      /* server is 3-4s ahead */

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

/*
 * Test clock compensation during a live stream.
 *
 * There are 4 segments, 3s each.
 * We set the mpd availability 6s before now.
 * The server is 3s ahead of the client, which means it is currently generating
 * segment 4.
 * We expect the client to download segment 4 and to wait for it to
 * be available.
 *
 * The mpd also contains an xsdate UTC timing scheme, but its address is not
 * configured in inputTestData so the fake http src element will not reply
 * to that address. Adaptive demux should attempt to use the second
 * UTC timing scheme (the HTTP NTP server).
 *
 */
GST_START_TEST (testClockCompensationHttpNtp)
{
  gchar *mpd;
  const gchar *mpd_1 =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
      "     xmlns=\"urn:mpeg:DASH:schema:MPD:2011\""
      "     xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 DASH-MPD.xsd\""
      "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\""
      "     type=\"dynamic\" availabilityStartTime=\"";
  GDateTime *availabilityStartTime;
  gchar *availabilityStartTimeString;
  const gchar *mpd_2 = "\""
      "     minBufferTime=\"PT1.500S\""
      "     minimumUpdatePeriod=\"PT500S\">"
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-ntp:2012\" value=\"http://mocktime/http-ntp\"/>"
      "  <Period>"
      "    <AdaptationSet mimeType=\"audio/webm\""
      "                   subsegmentAlignment=\"true\">"
      "      <Representation id=\"171\""
      "                      codecs=\"vorbis\""
      "                      audioSamplingRate=\"44100\""
      "                      startWithSAP=\"1\""
      "                      bandwidth=\"129553\">"
      "        <AudioChannelConfiguration"
      "           schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\""
      "           value=\"2\" />"
      "        <SegmentTemplate duration=\"3\""
      "                         media=\"audio$Number$.webm\""
      "                         >"
      "        </SegmentTemplate>"
      "      </Representation></AdaptationSet></Period></MPD>";

  GstDashDemuxTestInputData inputTestData[] = {
    {"http://unit.test/test.mpd", NULL, 0},
    {"http://mocktime/http-ntp", (guint8 *) "3", 0},    /* server is 3s ahead */
    {"http://unit.test/audio1.webm", NULL, 1000},
    {"http://unit.test/audio2.webm", NULL, 2000},
    {"http://unit.test/audio3.webm", NULL, 3000},
    {"http://unit.test/audio4.webm", NULL, 4000},
    {NULL, NULL, 0},
  };
  GstAdaptiveDemuxTestExpectedOutput outputTestData[] = {
    {"audio_00", 4000, NULL},
  };
  GstTestHTTPSrcCallbacks http_src_callbacks = { 0 };
  GstTestHTTPSrcTestData http_src_test_data = { 0 };
  GstAdaptiveDemuxTestCallbacks test_callbacks = { 0 };
  GstDashDemuxTestCase *testData;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_system_clock_set_default (clock);

  availabilityStartTime = timeFromNow (-6);
  availabilityStartTimeString = toXSDateTime (availabilityStartTime);
  mpd = g_strdup_printf ("%s%s%s", mpd_1, availabilityStartTimeString, mpd_2);
  g_free (availabilityStartTimeString);
  inputTestData[0].payload = (guint8 *) mpd;

  http_src_callbacks.src_start = gst_dashdemux_http_src_start;
  http_src_callbacks.src_create =
      gst_dashdemux_http_src_create_mock_time_server;
  http_src_test_data.input = inputTestData;
  gst_test_http_src_install_callbacks (&http_src_callbacks,
      &http_src_test_data);

  test_callbacks.appsink_received_data = testClockCompensationCheckDataReceived;
  test_callbacks.appsink_eos = gst_adaptive_demux_test_unexpected_eos;

  testData = gst_dash_demux_test_case_new ();
  COPY_OUTPUT_TEST_DATA (outputTestData, testData);
  testData->availabilityStartTime = availabilityStartTime;
  testData->clockCompensation = 3;      /* server is 3s ahead */

  gst_adaptive_demux_test_run (DEMUX_ELEMENT_NAME, "http://unit.test/test.mpd",
      &test_callbacks, testData);

  g_object_unref (testData);
  if (http_src_test_data.data)
    gst_structure_free (http_src_test_data.data);
  g_free (mpd);
  gst_system_clock_set_default (NULL);
  gst_object_unref (clock);
}

GST_END_TEST;

static Suite *
dash_demux_suite (void)
{
  Suite *s = suite_create ("dash_demux");
  TCase *tc_basicTests = tcase_create ("basicTests");
  TCase *tc_liveTests = tcase_create ("liveTests");

  tcase_add_test (tc_basicTests, simpleTest);
  tcase_add_test (tc_basicTests, testTwoPeriods);
  tcase_add_test (tc_basicTests, testParameters);
  tcase_add_test (tc_basicTests, testSeek);
  tcase_add_test (tc_basicTests, testSeekKeyUnitPosition);
  tcase_add_test (tc_basicTests, testSeekPosition);
  tcase_add_test (tc_basicTests, testSeekUpdateStopPosition);
  tcase_add_test (tc_basicTests, testSeekSnapBeforePosition);
  tcase_add_test (tc_basicTests, testSeekSnapAfterPosition);
  tcase_add_test (tc_basicTests, testReverseSeekSnapBeforePosition);
  tcase_add_test (tc_basicTests, testReverseSeekSnapAfterPosition);
  tcase_add_test (tc_basicTests, testParallelSeek);
  tcase_add_test (tc_basicTests, testDownloadError);
  tcase_add_test (tc_basicTests, testHeaderDownloadError);
  tcase_add_test (tc_basicTests, testMediaDownloadErrorLastFragment);
  tcase_add_test (tc_basicTests, testMediaDownloadErrorMiddleFragment);
  tcase_add_test (tc_basicTests, testQuery);
  tcase_add_test (tc_basicTests, testContentProtection);

  tcase_add_test (tc_liveTests, testLiveStream);
  tcase_add_test (tc_liveTests, testLiveStreamPresentationDelay);
  tcase_add_test (tc_liveTests, testQueryLiveStream);
  tcase_add_test (tc_liveTests, testSeekLiveStream);
  tcase_add_test (tc_liveTests, testClockCompensationHttpXSdate);
  tcase_add_test (tc_liveTests,
      testClockCompensationHttpXSdateWithManifestRefresh);
  tcase_add_test (tc_liveTests, testClockCompensationHttpHead);
  tcase_add_test (tc_liveTests, testClockCompensationHttpNtp);
  tcase_add_test (tc_liveTests, testClockCompensationMethodChange);

  tcase_add_unchecked_fixture (tc_basicTests, gst_adaptive_demux_test_setup,
      gst_adaptive_demux_test_teardown);
  tcase_add_unchecked_fixture (tc_liveTests, gst_adaptive_demux_test_setup,
      gst_adaptive_demux_test_teardown);

  suite_add_tcase (s, tc_basicTests);
  suite_add_tcase (s, tc_liveTests);

  return s;
}

GST_CHECK_MAIN (dash_demux);
