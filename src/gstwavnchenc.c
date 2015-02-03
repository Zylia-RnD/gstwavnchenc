/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2015 Marcin Chryszczanowicz <<marcin.chryszczanowicz@zylia.pl>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-wavnchenc
 *
 * FIXME:Describe wavnchenc here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! wavnchenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "gstwavnchenc.h"
/* #include <gst/gst.h> */

#include <gst/audio/audio.h>
#include <gst/riff/riff-media.h>
#include <gst/base/gstbytewriter.h>

GST_DEBUG_CATEGORY_STATIC (gst_wav_nch_enc_debug);
#define GST_CAT_DEFAULT gst_wav_nch_enc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};


struct riff_struct
{
  guint8 id[4];                 /* RIFF */
  guint32 len;
  guint8 wav_id[4];             /* WAVE */
};

struct chunk_struct
{
  guint8 id[4];
  guint32 len;
};

struct common_struct
{
  guint16 wFormatTag;
  guint16 wChannels;
  guint32 dwSamplesPerSec;
  guint32 dwAvgBytesPerSec;
  guint16 wBlockAlign;
  guint16 wBitsPerSample;       /* Only for PCM */
};

struct wave_header
{
  struct riff_struct riff;
  struct chunk_struct format;
  struct common_struct common;
  struct chunk_struct data;
};

typedef struct
{
  /* Offset Size    Description   Value
   * 0x00   4       ID            unique identification value
   * 0x04   4       Position      play order position
   * 0x08   4       Data Chunk ID RIFF ID of corresponding data chunk
   * 0x0c   4       Chunk Start   Byte Offset of Data Chunk *
   * 0x10   4       Block Start   Byte Offset to sample of First Channel
   * 0x14   4       Sample Offset Byte Offset to sample byte of First Channel
   */
  guint32 id;
  guint32 position;
  guint8 data_chunk_id[4];
  guint32 chunk_start;
  guint32 block_start;
  guint32 sample_offset;
} GstWavNchEncCue;

typedef struct
{
  /* Offset Size    Description     Value
   * 0x00   4       Chunk ID        "labl" (0x6C61626C) or "note" (0x6E6F7465)
   * 0x04   4       Chunk Data Size depends on contained text
   * 0x08   4       Cue Point ID    0 - 0xFFFFFFFF
   * 0x0c           Text
   */
  guint8 chunk_id[4];
  guint32 chunk_data_size;
  guint32 cue_point_id;
  gchar *text;
} GstWavNchEncLabl, GstWavNchEncNote;

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "format = (string) " GST_AUDIO_FORMATS_ALL ", "
        "layout = (string) interleaved")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wav")
    );

#define gst_wav_nch_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWavNchEnc, gst_wav_nch_enc, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL)
    G_IMPLEMENT_INTERFACE (GST_TYPE_TOC_SETTER, NULL)
    );
/*
static void gst_wav_nch_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wav_nch_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
*/

static GstStateChangeReturn gst_wav_nch_enc_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_wav_nch_enc_sink_setcaps (GstPad * pad, GstCaps * caps);

static gboolean gst_wav_nch_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_wav_nch_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the wavnchenc's class */
static void
gst_wav_nch_enc_class_init (GstWavNchEncClass * klass)
{
  /*GObjectClass *gobject_class;*/
  GstElementClass *gstelement_class;

  /*gobject_class = (GObjectClass *) klass;*/
  gstelement_class = (GstElementClass *) klass;
  /*
  gobject_class->set_property = gst_wav_nch_enc_set_property;
  gobject_class->get_property = gst_wav_nch_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));
  */

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_wav_nch_enc_change_state);

  gst_element_class_set_details_simple(gstelement_class,
    "WAV n-channel audio muxer",
    "Codec/Muxer/Audio",
    "Encode n-channel raw audio into n-channel WAV file",
    "Marcin Chryszczanowicz <<marcin.chryszczanowicz@zylia.pl>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_wav_nch_enc_init (GstWavNchEnc * wavnchenc)
{
  wavnchenc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (wavnchenc->sinkpad, GST_DEBUG_FUNCPTR (gst_wav_nch_enc_chain));
  gst_pad_set_event_function (wavnchenc->sinkpad, GST_DEBUG_FUNCPTR (gst_wav_nch_enc_sink_event));

  gst_pad_use_fixed_caps (wavnchenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (wavnchenc), wavnchenc->sinkpad);

  wavnchenc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (wavnchenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (wavnchenc), wavnchenc->srcpad);
}
/*
static void
gst_wav_nch_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWavNchEnc *filter = GST_WAVNCHENC (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wav_nch_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWavNchEnc *filter = GST_WAVNCHENC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
*/
/* GstElement vmethod implementations */

#define WAV_HEADER_LEN 44

static GstBuffer *
gst_wav_nch_enc_create_header_buf (GstWavNchEnc * wavnchenc)
{
  struct wave_header wave;
  GstBuffer *buf;
  GstMapInfo map;
  guint8 *header;

  buf = gst_buffer_new_and_alloc (WAV_HEADER_LEN);
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  header = map.data;
  memset (header, 0, WAV_HEADER_LEN);

  memcpy (wave.riff.id, "RIFF", 4);
  wave.riff.len =
      wavnchenc->meta_length + wavnchenc->audio_length + WAV_HEADER_LEN - 8;
  memcpy (wave.riff.wav_id, "WAVE", 4);

  memcpy (wave.format.id, "fmt ", 4);
  wave.format.len = 16;

  wave.common.wChannels = wavnchenc->channels;
  wave.common.wBitsPerSample = wavnchenc->width;
  wave.common.dwSamplesPerSec = wavnchenc->rate;
  wave.common.wFormatTag = wavnchenc->format;
  wave.common.wBlockAlign = (wavnchenc->width / 8) * wave.common.wChannels;
  wave.common.dwAvgBytesPerSec =
      wave.common.wBlockAlign * wave.common.dwSamplesPerSec;

  memcpy (wave.data.id, "data", 4);
  wave.data.len = wavnchenc->audio_length;

  memcpy (header, (char *) wave.riff.id, 4);
  GST_WRITE_UINT32_LE (header + 4, wave.riff.len);
  memcpy (header + 8, (char *) wave.riff.wav_id, 4);
  memcpy (header + 12, (char *) wave.format.id, 4);
  GST_WRITE_UINT32_LE (header + 16, wave.format.len);
  GST_WRITE_UINT16_LE (header + 20, wave.common.wFormatTag);
  GST_WRITE_UINT16_LE (header + 22, wave.common.wChannels);
  GST_WRITE_UINT32_LE (header + 24, wave.common.dwSamplesPerSec);
  GST_WRITE_UINT32_LE (header + 28, wave.common.dwAvgBytesPerSec);
  GST_WRITE_UINT16_LE (header + 32, wave.common.wBlockAlign);
  GST_WRITE_UINT16_LE (header + 34, wave.common.wBitsPerSample);
  memcpy (header + 36, (char *) wave.data.id, 4);
  GST_WRITE_UINT32_LE (header + 40, wave.data.len);

  gst_buffer_unmap (buf, &map);

  return buf;
}

static GstFlowReturn
gst_wav_nch_enc_push_header (GstWavNchEnc * wavnchenc)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstSegment segment;

  /* seek to beginning of file */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  if (!gst_pad_push_event (wavnchenc->srcpad, gst_event_new_segment (&segment))) {
    GST_WARNING_OBJECT (wavnchenc, "Seek to the beginning failed");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (wavnchenc, "writing header, meta_size=%u, audio_size=%u",
      wavnchenc->meta_length, wavnchenc->audio_length);

  outbuf = gst_wav_nch_enc_create_header_buf (wavnchenc);
  GST_BUFFER_OFFSET (outbuf) = 0;

  ret = gst_pad_push (wavnchenc->srcpad, outbuf);

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (wavnchenc, "push header failed: flow = %s",
        gst_flow_get_name (ret));
  }

  return ret;
}

static gboolean
gst_wav_nch_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstWavNchEnc *wavnchenc;
  GstStructure *structure;
  const gchar *name;
  gint chans, rate;
  GstCaps *ccaps;

  wavnchenc = GST_WAVNCHENC (gst_pad_get_parent (pad));

  ccaps = gst_pad_get_current_caps (pad);
  if (wavnchenc->sent_header && ccaps && !gst_caps_can_intersect (caps, ccaps)) {
    gst_caps_unref (ccaps);
    GST_WARNING_OBJECT (wavnchenc, "cannot change format in middle of stream");
    goto fail;
  }
  if (ccaps)
    gst_caps_unref (ccaps);

  GST_DEBUG_OBJECT (wavnchenc, "got caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (structure);

  if (!gst_structure_get_int (structure, "channels", &chans) ||
      !gst_structure_get_int (structure, "rate", &rate)) {
    GST_WARNING_OBJECT (wavnchenc, "caps incomplete");
    goto fail;
  }

  if (strcmp (name, "audio/x-raw") == 0) {
    GstAudioInfo info;

    if (!gst_audio_info_from_caps (&info, caps))
      goto fail;

    if (GST_AUDIO_INFO_IS_INTEGER (&info))
      wavnchenc->format = GST_RIFF_WAVE_FORMAT_PCM;
    else if (GST_AUDIO_INFO_IS_FLOAT (&info))
      wavnchenc->format = GST_RIFF_WAVE_FORMAT_IEEE_FLOAT;
    else
      goto fail;

    wavnchenc->width = GST_AUDIO_INFO_WIDTH (&info);
  } else if (strcmp (name, "audio/x-alaw") == 0) {
    wavnchenc->format = GST_RIFF_WAVE_FORMAT_ALAW;
    wavnchenc->width = 8;
  } else if (strcmp (name, "audio/x-mulaw") == 0) {
    wavnchenc->format = GST_RIFF_WAVE_FORMAT_MULAW;
    wavnchenc->width = 8;
  } else {
    GST_WARNING_OBJECT (wavnchenc, "Unsupported format %s", name);
    goto fail;
  }

  wavnchenc->channels = chans;
  wavnchenc->rate = rate;

  GST_LOG_OBJECT (wavnchenc,
      "accepted caps: format=0x%04x chans=%u width=%u rate=%u",
      wavnchenc->format, wavnchenc->channels, wavnchenc->width, wavnchenc->rate);

  gst_object_unref (wavnchenc);
  return TRUE;

fail:
  gst_object_unref (wavnchenc);
  return FALSE;
}

static void
gst_wavparse_tags_foreach (const GstTagList * tags, const gchar * tag,
    gpointer data)
{
  const struct
  {
    guint32 fcc;
    const gchar *tag;
  } rifftags[] = {
    {
    GST_RIFF_INFO_IARL, GST_TAG_LOCATION}, {
    GST_RIFF_INFO_IART, GST_TAG_ARTIST}, {
    GST_RIFF_INFO_ICMT, GST_TAG_COMMENT}, {
    GST_RIFF_INFO_ICOP, GST_TAG_COPYRIGHT}, {
    GST_RIFF_INFO_ICRD, GST_TAG_DATE}, {
    GST_RIFF_INFO_IGNR, GST_TAG_GENRE}, {
    GST_RIFF_INFO_IKEY, GST_TAG_KEYWORDS}, {
    GST_RIFF_INFO_INAM, GST_TAG_TITLE}, {
    GST_RIFF_INFO_IPRD, GST_TAG_ALBUM}, {
    GST_RIFF_INFO_ISBJ, GST_TAG_ALBUM_ARTIST}, {
    GST_RIFF_INFO_ISFT, GST_TAG_ENCODER}, {
    GST_RIFF_INFO_ISRC, GST_TAG_ISRC}, {
    0, NULL}
  };
  gint n;
  gchar *str = NULL;
  GstByteWriter *bw = data;
  for (n = 0; rifftags[n].fcc != 0; n++) {
    if (!strcmp (rifftags[n].tag, tag)) {
      if (rifftags[n].fcc == GST_RIFF_INFO_ICRD) {
        GDate *date;
        /* special case for the date tag */
        if (gst_tag_list_get_date (tags, tag, &date)) {
          str =
              g_strdup_printf ("%04d:%02d:%02d", g_date_get_year (date),
              g_date_get_month (date), g_date_get_day (date));
          g_date_free (date);
        }
      } else {
        gst_tag_list_get_string (tags, tag, &str);
      }
      if (str) {
        gst_byte_writer_put_uint32_le (bw, rifftags[n].fcc);
        gst_byte_writer_put_uint32_le (bw, GST_ROUND_UP_2 (strlen (str)));
        gst_byte_writer_put_string (bw, str);
        g_free (str);
        str = NULL;
        break;
      }
    }
  }

}

static GstFlowReturn
gst_wav_nch_enc_write_tags (GstWavNchEnc * wavnchenc)
{
  const GstTagList *user_tags;
  GstTagList *tags;
  guint size;
  GstBuffer *buf;
  GstByteWriter bw;

  g_return_val_if_fail (wavnchenc != NULL, GST_FLOW_OK);

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (wavnchenc));
  if ((!wavnchenc->tags) && (!user_tags)) {
    GST_DEBUG_OBJECT (wavnchenc, "have no tags");
    return GST_FLOW_OK;
  }
  tags =
      gst_tag_list_merge (user_tags, wavnchenc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (wavnchenc)));

  GST_DEBUG_OBJECT (wavnchenc, "writing tags");

  gst_byte_writer_init_with_size (&bw, 1024, FALSE);

  /* add LIST INFO chunk */
  gst_byte_writer_put_data (&bw, (const guint8 *) "LIST", 4);
  gst_byte_writer_put_uint32_le (&bw, 0);
  gst_byte_writer_put_data (&bw, (const guint8 *) "INFO", 4);

  /* add tags */
  gst_tag_list_foreach (tags, gst_wavparse_tags_foreach, &bw);

  /* sets real size of LIST INFO chunk */
  size = gst_byte_writer_get_pos (&bw);
  gst_byte_writer_set_pos (&bw, 4);
  gst_byte_writer_put_uint32_le (&bw, size - 8);

  gst_tag_list_unref (tags);

  buf = gst_byte_writer_reset_and_get_buffer (&bw);
  wavnchenc->meta_length += gst_buffer_get_size (buf);
  return gst_pad_push (wavnchenc->srcpad, buf);
}

static gboolean
gst_wav_nch_enc_is_cue_id_unique (guint32 id, GList * list)
{
  GstWavNchEncCue *cue;

  while (list) {
    cue = list->data;
    if (cue->id == id)
      return FALSE;
    list = g_list_next (list);
  }

  return TRUE;
}

static gboolean
gst_wav_nch_enc_parse_cue (GstWavNchEnc * wavnchenc, guint32 id, GstTocEntry * entry)
{
  gint64 start;
  GstWavNchEncCue *cue;

  g_return_val_if_fail (entry != NULL, FALSE);

  gst_toc_entry_get_start_stop_times (entry, &start, NULL);

  cue = g_new (GstWavNchEncCue, 1);
  cue->id = id;
  cue->position = gst_util_uint64_scale_round (start, wavnchenc->rate, GST_SECOND);
  memcpy (cue->data_chunk_id, "data", 4);
  cue->chunk_start = 0;
  cue->block_start = 0;
  cue->sample_offset = cue->position;
  wavnchenc->cues = g_list_append (wavnchenc->cues, cue);

  return TRUE;
}

static gboolean
gst_wav_nch_enc_parse_labl (GstWavNchEnc * wavnchenc, guint32 id, GstTocEntry * entry)
{
  gchar *tag;
  GstTagList *tags;
  GstWavNchEncLabl *labl;

  g_return_val_if_fail (entry != NULL, FALSE);

  tags = gst_toc_entry_get_tags (entry);
  if (!tags) {
    GST_INFO_OBJECT (wavnchenc, "no tags for entry: %d", id);
    return FALSE;
  }
  if (!gst_tag_list_get_string (tags, GST_TAG_TITLE, &tag)) {
    GST_INFO_OBJECT (wavnchenc, "no title tag for entry: %d", id);
    return FALSE;
  }

  labl = g_new (GstWavNchEncLabl, 1);
  memcpy (labl->chunk_id, "labl", 4);
  labl->chunk_data_size = 4 + strlen (tag) + 1;
  labl->cue_point_id = id;
  labl->text = tag;

  GST_DEBUG_OBJECT (wavnchenc, "got labl: '%s'", tag);

  wavnchenc->labls = g_list_append (wavnchenc->labls, labl);

  return TRUE;
}

static gboolean
gst_wav_nch_enc_parse_note (GstWavNchEnc * wavnchenc, guint32 id, GstTocEntry * entry)
{
  gchar *tag;
  GstTagList *tags;
  GstWavNchEncNote *note;

  g_return_val_if_fail (entry != NULL, FALSE);
  tags = gst_toc_entry_get_tags (entry);
  if (!tags) {
    GST_INFO_OBJECT (wavnchenc, "no tags for entry: %d", id);
    return FALSE;
  }
  if (!gst_tag_list_get_string (tags, GST_TAG_COMMENT, &tag)) {
    GST_INFO_OBJECT (wavnchenc, "no comment tag for entry: %d", id);
    return FALSE;
  }

  note = g_new (GstWavNchEncNote, 1);
  memcpy (note->chunk_id, "note", 4);
  note->chunk_data_size = 4 + strlen (tag) + 1;
  note->cue_point_id = id;
  note->text = tag;

  GST_DEBUG_OBJECT (wavnchenc, "got note: '%s'", tag);

  wavnchenc->notes = g_list_append (wavnchenc->notes, note);

  return TRUE;
}

static gboolean
gst_wav_nch_enc_write_cues (guint8 ** data, GList * list)
{
  GstWavNchEncCue *cue;

  while (list) {
    cue = list->data;
    GST_WRITE_UINT32_LE (*data, cue->id);
    GST_WRITE_UINT32_LE (*data + 4, cue->position);
    memcpy (*data + 8, (gchar *) cue->data_chunk_id, 4);
    GST_WRITE_UINT32_LE (*data + 12, cue->chunk_start);
    GST_WRITE_UINT32_LE (*data + 16, cue->block_start);
    GST_WRITE_UINT32_LE (*data + 20, cue->sample_offset);
    *data += 24;
    list = g_list_next (list);
  }

  return TRUE;
}

static gboolean
gst_wav_nch_enc_write_labls (guint8 ** data, GList * list)
{
  GstWavNchEncLabl *labl;

  while (list) {
    labl = list->data;
    memcpy (*data, (gchar *) labl->chunk_id, 4);
    GST_WRITE_UINT32_LE (*data + 4, labl->chunk_data_size);
    GST_WRITE_UINT32_LE (*data + 8, labl->cue_point_id);
    memcpy (*data + 12, (gchar *) labl->text, strlen (labl->text));
    *data += 8 + GST_ROUND_UP_2 (labl->chunk_data_size);
    list = g_list_next (list);
  }

  return TRUE;
}

static gboolean
gst_wav_nch_enc_write_notes (guint8 ** data, GList * list)
{
  GstWavNchEncNote *note;

  while (list) {
    note = list->data;
    memcpy (*data, (gchar *) note->chunk_id, 4);
    GST_WRITE_UINT32_LE (*data + 4, note->chunk_data_size);
    GST_WRITE_UINT32_LE (*data + 8, note->cue_point_id);
    memcpy (*data + 12, (gchar *) note->text, strlen (note->text));
    *data += 8 + GST_ROUND_UP_2 (note->chunk_data_size);
    list = g_list_next (list);
  }

  return TRUE;
}

static GstFlowReturn
gst_wav_nch_enc_write_toc (GstWavNchEnc * wavnchenc)
{
  GList *list;
  GstToc *toc;
  GstTocEntry *entry, *subentry;
  GstBuffer *buf;
  GstMapInfo map;
  guint8 *data;
  guint32 ncues, size, cues_size, labls_size, notes_size;

  if (!wavnchenc->toc) {
    GST_DEBUG_OBJECT (wavnchenc, "have no toc, checking toc_setter");
    wavnchenc->toc = gst_toc_setter_get_toc (GST_TOC_SETTER (wavnchenc));
  }
  if (!wavnchenc->toc) {
    GST_WARNING_OBJECT (wavnchenc, "have no toc");
    return GST_FLOW_OK;
  }

  toc = gst_toc_ref (wavnchenc->toc);
  size = 0;
  cues_size = 0;
  labls_size = 0;
  notes_size = 0;

  /* check if the TOC entries is valid */
  list = gst_toc_get_entries (toc);
  entry = list->data;
  if (gst_toc_entry_is_alternative (entry)) {
    list = gst_toc_entry_get_sub_entries (entry);
    while (list) {
      subentry = list->data;
      if (!gst_toc_entry_is_sequence (subentry))
        return FALSE;
      list = g_list_next (list);
    }
    list = gst_toc_entry_get_sub_entries (entry);
  }
  if (gst_toc_entry_is_sequence (entry)) {
    while (list) {
      entry = list->data;
      if (!gst_toc_entry_is_sequence (entry))
        return FALSE;
      list = g_list_next (list);
    }
    list = gst_toc_get_entries (toc);
  }

  ncues = g_list_length (list);
  GST_DEBUG_OBJECT (wavnchenc, "number of cue entries: %d", ncues);

  while (list) {
    guint32 id = 0;
    gint64 id64;
    const gchar *uid;

    entry = list->data;
    uid = gst_toc_entry_get_uid (entry);
    id64 = g_ascii_strtoll (uid, NULL, 0);
    /* check if id unique compatible with guint32 else generate random */
    if (id64 >= 0 && gst_wav_nch_enc_is_cue_id_unique (id64, wavnchenc->cues)) {
      id = (guint32) id64;
    } else {
      do {
        id = g_random_int ();
      } while (!gst_wav_nch_enc_is_cue_id_unique (id, wavnchenc->cues));
    }
    gst_wav_nch_enc_parse_cue (wavnchenc, id, entry);
    gst_wav_nch_enc_parse_labl (wavnchenc, id, entry);
    gst_wav_nch_enc_parse_note (wavnchenc, id, entry);
    list = g_list_next (list);
  }

  /* count cues size */
  if (wavnchenc->cues) {
    cues_size = 24 * g_list_length (wavnchenc->cues);
    size += 12 + cues_size;
  } else {
    GST_WARNING_OBJECT (wavnchenc, "cue's not found");
    return FALSE;
  }
  /* count labls size */
  if (wavnchenc->labls) {
    list = wavnchenc->labls;
    while (list) {
      GstWavNchEncLabl *labl;
      labl = list->data;
      labls_size += 8 + GST_ROUND_UP_2 (labl->chunk_data_size);
      list = g_list_next (list);
    }
    size += labls_size;
  }
  /* count notes size */
  if (wavnchenc->notes) {
    list = wavnchenc->notes;
    while (list) {
      GstWavNchEncNote *note;
      note = list->data;
      notes_size += 8 + GST_ROUND_UP_2 (note->chunk_data_size);
      list = g_list_next (list);
    }
    size += notes_size;
  }
  if (wavnchenc->labls || wavnchenc->notes) {
    size += 12;
  }

  buf = gst_buffer_new_and_alloc (size);
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  data = map.data;
  memset (data, 0, size);

  /* write Cue Chunk */
  if (wavnchenc->cues) {
    memcpy (data, (gchar *) "cue ", 4);
    GST_WRITE_UINT32_LE (data + 4, 4 + cues_size);
    GST_WRITE_UINT32_LE (data + 8, ncues);
    data += 12;
    gst_wav_nch_enc_write_cues (&data, wavnchenc->cues);

    /* write Associated Data List Chunk */
    if (wavnchenc->labls || wavnchenc->notes) {
      memcpy (data, (gchar *) "LIST", 4);
      GST_WRITE_UINT32_LE (data + 4, 4 + labls_size + notes_size);
      memcpy (data + 8, (gchar *) "adtl", 4);
      data += 12;
      if (wavnchenc->labls)
        gst_wav_nch_enc_write_labls (&data, wavnchenc->labls);
      if (wavnchenc->notes)
        gst_wav_nch_enc_write_notes (&data, wavnchenc->notes);
    }
  }

  /* free resources */
  if (toc)
    gst_toc_unref (toc);
  if (wavnchenc->cues)
    g_list_free_full (wavnchenc->cues, g_free);
  if (wavnchenc->labls)
    g_list_free_full (wavnchenc->labls, g_free);
  if (wavnchenc->notes)
    g_list_free_full (wavnchenc->notes, g_free);

  gst_buffer_unmap (buf, &map);
  wavnchenc->meta_length += gst_buffer_get_size (buf);

  return gst_pad_push (wavnchenc->srcpad, buf);
}


/* this function handles sink events */
static gboolean
gst_wav_nch_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstWavNchEnc *wavnchenc;
  GstTagList *tags;
  GstToc *toc;

  wavnchenc = GST_WAVNCHENC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_wav_nch_enc_sink_setcaps (pad, caps);

      /* have our own src caps */
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_EOS:
    {
      GstFlowReturn flow;
      GST_DEBUG_OBJECT (wavnchenc, "got EOS");

      flow = gst_wav_nch_enc_write_toc (wavnchenc);
      if (flow != GST_FLOW_OK) {
        GST_WARNING_OBJECT (wavnchenc, "error pushing toc: %s",
            gst_flow_get_name (flow));
      }
      flow = gst_wav_nch_enc_write_tags (wavnchenc);
      if (flow != GST_FLOW_OK) {
        GST_WARNING_OBJECT (wavnchenc, "error pushing tags: %s",
            gst_flow_get_name (flow));
      }

      /* write header with correct length values */
     /* gst_wav_nch_enc_push_header (wavnchenc); */

      /* we're done with this file */
      wavnchenc->finished_properly = TRUE;

      /* and forward the EOS event */
      res = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_SEGMENT:
      /* Just drop it, it's probably in TIME format
       * anyway. We'll send our own newsegment event */
      gst_event_unref (event);
      break;
    case GST_EVENT_TOC:
      gst_event_parse_toc (event, &toc, NULL);
      if (toc) {
        if (wavnchenc->toc != toc) {
          if (wavnchenc->toc)
            gst_toc_unref (wavnchenc->toc);
          wavnchenc->toc = toc;
        } else {
          gst_toc_unref (toc);
        }
      }
      res = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &tags);
      if (tags) {
        if (wavnchenc->tags != tags) {
          if (wavnchenc->tags)
            gst_tag_list_unref (wavnchenc->tags);
          wavnchenc->tags = gst_tag_list_ref (tags);
        }
      }
      res = gst_pad_event_default (pad, parent, event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_wav_nch_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstWavNchEnc *wavnchenc = GST_WAVNCHENC (parent);
  GstFlowReturn flow = GST_FLOW_OK;

  if (wavnchenc->channels <= 0) {
    GST_ERROR_OBJECT (wavnchenc, "Got data without caps");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (G_UNLIKELY (!wavnchenc->sent_header)) {
    gst_pad_set_caps (wavnchenc->srcpad,
        gst_static_pad_template_get_caps (&src_factory));

    /* starting a file, means we have to finish it properly */
    wavnchenc->finished_properly = FALSE;

    /* push initial bogus header, it will be updated on EOS */
    flow = gst_wav_nch_enc_push_header (wavnchenc);
    if (flow != GST_FLOW_OK) {
      GST_WARNING_OBJECT (wavnchenc, "error pushing header: %s",
          gst_flow_get_name (flow));
      return flow;
    }
    GST_DEBUG_OBJECT (wavnchenc, "wrote dummy header");
    wavnchenc->audio_length = 0;
    wavnchenc->sent_header = TRUE;
  }

  GST_LOG_OBJECT (wavnchenc,
      "pushing %" G_GSIZE_FORMAT " bytes raw audio, ts=%" GST_TIME_FORMAT,
      gst_buffer_get_size (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  buf = gst_buffer_make_writable (buf);

  GST_BUFFER_OFFSET (buf) = WAV_HEADER_LEN + wavnchenc->audio_length;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  wavnchenc->audio_length += gst_buffer_get_size (buf);

  flow = gst_pad_push (wavnchenc->srcpad, buf);

  return flow;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
wavnchenc_init (GstPlugin * wavnchenc)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template wavnchenc' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_wav_nch_enc_debug, "wavnchenc",
      0, "WAV n-channel encoder element");

  return gst_element_register (wavnchenc, "wavnchenc", GST_RANK_PRIMARY,
      GST_TYPE_WAVNCHENC);
}


static GstStateChangeReturn
gst_wav_nch_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavNchEnc *wavnchenc = GST_WAVNCHENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      wavnchenc->format = 0;
      wavnchenc->channels = 0;
      wavnchenc->width = 0;
      wavnchenc->rate = 0;
      /* use bogus size initially, we'll write the real
       * header when we get EOS and know the exact length */
      wavnchenc->audio_length = 0x7FFF0000;
      wavnchenc->meta_length = 0;
      wavnchenc->sent_header = FALSE;
      /* its true because we haven't writen anything */
      wavnchenc->finished_properly = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!wavnchenc->finished_properly) {
        GST_ELEMENT_WARNING (wavnchenc, STREAM, MUX,
            ("Wav stream not finished properly"),
            ("Wav stream not finished properly, no EOS received "
                "before shutdown"));
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT (wavnchenc, "tags: %p", wavnchenc->tags);
      if (wavnchenc->tags) {
        gst_tag_list_unref (wavnchenc->tags);
        wavnchenc->tags = NULL;
      }
      GST_DEBUG_OBJECT (wavnchenc, "toc: %p", wavnchenc->toc);
      if (wavnchenc->toc) {
        gst_toc_unref (wavnchenc->toc);
        wavnchenc->toc = NULL;
      }
      gst_tag_setter_reset_tags (GST_TAG_SETTER (wavnchenc));
      gst_toc_setter_reset (GST_TOC_SETTER (wavnchenc));
      break;
    default:
      break;
  }

  return ret;
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstwavnchenc"
#endif

/* gstreamer looks for this structure to register wavnchencs
 *
 * exchange the string 'Template wavnchenc' with your wavnchenc description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    wavnchenc,
    "Encode raw audio into WAV or n-channel WAV",
    wavnchenc_init,
    "1.0.0",
    "LGPL",
    "GStreamer Zylia Plug-ins source release",
    "http://zylia.pl"
)
