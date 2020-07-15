/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sps.h"

#include "h264_stream.h"

static h264_stream_t* h264_stream = NULL;
static int initial_width, initial_height;

void gs_sps_init(int width, int height) {
  h264_stream = h264_new();
  initial_width = width;
  initial_height = height;
}

void gs_sps_stop() {
  if (h264_stream) {
    h264_free(h264_stream);
    h264_stream = NULL;
  }
}

void gs_sps_fix(PLENTRY sps, int flags, uint8_t* out_buf, uint32_t* out_offset) {
  const char naluHeader[] = {0x00, 0x00, 0x00, 0x01};

  read_nal_unit(h264_stream, sps->data+4, sps->length-4);

  // Some decoders rely on H264 level to decide how many buffers are needed
  // Since we only need one frame buffered, we'll set level as low as we can
  // for known resolution combinations. Otherwise leave the profile alone (currently 5.0)
  if (initial_width == 1280 && initial_height == 720)
    h264_stream->sps->level_idc = 32; // Max 5 buffered frames at 1280x720x60
  else if (initial_width == 1920 && initial_height == 1080)
    h264_stream->sps->level_idc = 42; // Max 4 buffered frames at 1920x1080x60

  // Some decoders requires a reference frame count of 1 to decode successfully.
  h264_stream->sps->num_ref_frames = 1;

  // GFE 2.5.11 changed the SPS to add additional extensions
  // Some devices don't like these so we remove them here.
  h264_stream->sps->vui.video_signal_type_present_flag = 0;
  h264_stream->sps->vui.chroma_loc_info_present_flag = 0;

  if ((flags & GS_SPS_BITSTREAM_FIXUP) == GS_SPS_BITSTREAM_FIXUP) {
    // The SPS that comes in the current H264 bytestream doesn't set the bitstream_restriction_flag
    // or the max_dec_frame_buffering which increases decoding latency on some devices
    // log2_max_mv_length_horizontal and log2_max_mv_length_vertical are set to more
    // conservite values by GFE 25.11. We'll let those values stand.
    if (!h264_stream->sps->vui.bitstream_restriction_flag) {
      h264_stream->sps->vui.bitstream_restriction_flag = 1;
      h264_stream->sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
      h264_stream->sps->vui.max_bits_per_mb_denom = 1;
      h264_stream->sps->vui.log2_max_mv_length_horizontal = 16;
      h264_stream->sps->vui.log2_max_mv_length_vertical = 16;
      h264_stream->sps->vui.num_reorder_frames = 0;
    }

    // Some devices throw errors if max_dec_frame_buffering < num_ref_frames
    h264_stream->sps->vui.max_dec_frame_buffering = 1;

    // These values are the default for the fields, but they are more aggressive
    // than what GFE sends in 2.5.11, but it doesn't seem to cause picture problems.
    h264_stream->sps->vui.max_bytes_per_pic_denom = 2;
    h264_stream->sps->vui.max_bits_per_mb_denom = 1;
  } else // Devices that didn't/couldn't get bitstream restrictions before GFE 2.5.11 will continue to not receive them now
    h264_stream->sps->vui.bitstream_restriction_flag = 0;

  memcpy(out_buf+*out_offset, naluHeader, 4);
  *out_offset += 4;

  *out_offset += write_nal_unit(h264_stream, out_buf+*out_offset, 128);
}
