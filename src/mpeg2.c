/*
 * Copyright (c) 2016 Florent Revest, <florent.revest@free-electrons.com>
 *               2007 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sunxi_cedrus_drv_video.h"
#include "mpeg2.h"

#include <assert.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

/*
 * This file takes care of filling v4l2's frame API MPEG2 headers extended
 * controls from VA's data structures.
 */

VAStatus sunxi_cedrus_render_mpeg2_slice_data(VADriverContextP ctx,
		object_context_p obj_context, object_surface_p obj_surface,
		object_buffer_p obj_buffer)
{
	INIT_DRIVER_DATA
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	struct v4l2_buffer buf;
	struct v4l2_plane plane[1];

	printf("> %s()\n", __func__);

#if 0
	memset(plane, 0, sizeof(struct v4l2_plane));

	/* Query */
	memset(&(buf), 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = obj_surface->input_buf_index;
	buf.length = 1;
	buf.m.planes = plane;

	assert(ioctl(driver_data->mem2mem_fd, VIDIOC_QUERYBUF, &buf)==0);
#endif

	/* Keep track of offset */

//	driver_data->slice_offset[obj_surface->input_buf_index] += obj_buffer->size;

/*
	char *src_buf = mmap(NULL, obj_buffer->size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			driver_data->mem2mem_fd, buf.m.planes[0].m.mem_offset);
	assert(src_buf != MAP_FAILED);

printf("Queried buffer for index %d is at 0x%x\n", buf.index, src_buf);
printf("obj buffer is at 0x%x\n", obj_buffer->buffer_data);

printf("plane mem offset 0x%x\n", buf.m.planes[0].m.mem_offset);


	memcpy(src_buf, obj_buffer->buffer_data, obj_buffer->size);
//	munmap(src_buf);
*/



	printf("slice size at populate time: %d\n", obj_buffer->size);

	return vaStatus;
}

VAStatus sunxi_cedrus_render_mpeg2_picture_parameter(VADriverContextP ctx,
		object_context_p obj_context, object_surface_p obj_surface,
		object_buffer_p obj_buffer)
{
	INIT_DRIVER_DATA
	VAStatus vaStatus = VA_STATUS_SUCCESS;

	printf("> %s()\n", __func__);

	printf("%s with buffer %x data %x\n", __func__, obj_buffer, obj_buffer->buffer_data);

	VAPictureParameterBufferMPEG2 *pic_param = (VAPictureParameterBufferMPEG2 *)obj_buffer->buffer_data;
	obj_context->mpeg2_frame_hdr.type = MPEG2;

	obj_context->mpeg2_frame_hdr.width = pic_param->horizontal_size;
	obj_context->mpeg2_frame_hdr.height = pic_param->vertical_size;

	obj_context->mpeg2_frame_hdr.picture_coding_type = pic_param->picture_coding_type;

	char slice;

	if (pic_param->picture_coding_type == 0)
		slice = 'B';
	else if (pic_param->picture_coding_type == 1)
		slice = 'P';
	else if (pic_param->picture_coding_type == 2)
		slice = 'I';
	else
		slice = 'U';

	printf(">> %s: got a %c slice!\n", __func__, slice);

	obj_context->mpeg2_frame_hdr.f_code[0][0] = (pic_param->f_code >> 12) & 0xf;
	obj_context->mpeg2_frame_hdr.f_code[0][1] = (pic_param->f_code >>  8) & 0xf;
	obj_context->mpeg2_frame_hdr.f_code[1][0] = (pic_param->f_code >>  4) & 0xf;
	obj_context->mpeg2_frame_hdr.f_code[1][1] = pic_param->f_code & 0xf;

	obj_context->mpeg2_frame_hdr.intra_dc_precision = pic_param->picture_coding_extension.bits.intra_dc_precision;
	obj_context->mpeg2_frame_hdr.picture_structure = pic_param->picture_coding_extension.bits.picture_structure;
	obj_context->mpeg2_frame_hdr.top_field_first = pic_param->picture_coding_extension.bits.top_field_first;
	obj_context->mpeg2_frame_hdr.frame_pred_frame_dct = pic_param->picture_coding_extension.bits.frame_pred_frame_dct;
	obj_context->mpeg2_frame_hdr.concealment_motion_vectors = pic_param->picture_coding_extension.bits.concealment_motion_vectors;
	obj_context->mpeg2_frame_hdr.q_scale_type = pic_param->picture_coding_extension.bits.q_scale_type;
	obj_context->mpeg2_frame_hdr.intra_vlc_format = pic_param->picture_coding_extension.bits.intra_vlc_format;
	obj_context->mpeg2_frame_hdr.alternate_scan = pic_param->picture_coding_extension.bits.alternate_scan;


	object_surface_p fwd_surface = SURFACE(pic_param->forward_reference_picture);
printf("getting forward / backwards from %x at %x\n", pic_param->forward_reference_picture, fwd_surface);
	if(fwd_surface)
		obj_context->mpeg2_frame_hdr.forward_index = fwd_surface->output_buf_index;
	else
		obj_context->mpeg2_frame_hdr.forward_index = obj_surface->output_buf_index;


	object_surface_p bwd_surface = SURFACE(pic_param->backward_reference_picture);
printf("getting forward / backwards from %x at %x\n", pic_param->backward_reference_picture, bwd_surface);
	if(bwd_surface)
		obj_context->mpeg2_frame_hdr.backward_index = bwd_surface->output_buf_index;
	else
		obj_context->mpeg2_frame_hdr.backward_index = obj_surface->output_buf_index;

	printf("forward buffer %d\n", obj_surface->output_buf_index);

	return vaStatus;
}

VAStatus sunxi_cedrus_render_mpeg2_slice_parameter(VADriverContextP ctx,
		object_context_p obj_context, object_surface_p obj_surface,
		object_buffer_p obj_buffer)
{
	VASliceParameterBufferMPEG2 *slice_param = (VASliceParameterBufferMPEG2 *)obj_buffer->buffer_data;

	printf("> %s()\n", __func__);

/*
	obj_context->mpeg2_frame_hdr.slice_pos = slice_param->slice_data_offset * 8 + slice_param->macroblock_offset;
	obj_context->mpeg2_frame_hdr.slice_len = slice_param->slice_data_size * 8;
*/
	if (slice_param->slice_data_flag == VA_SLICE_DATA_FLAG_ALL)
		printf("Whole slice is in the buffer my friend!\n");
	else if (slice_param->slice_data_flag == VA_SLICE_DATA_FLAG_BEGIN)
		printf("The slice is only beginning my friend!\n");
	else if (slice_param->slice_data_flag == VA_SLICE_DATA_FLAG_MIDDLE)
		printf("The slice is in the middle my friend!\n");
	else if (slice_param->slice_data_flag == VA_SLICE_DATA_FLAG_END)
		printf("This is the last slice my friend!\n");

	printf("slice size at slice time: %d with offset %d and mb offset %d\n", slice_param->slice_data_size, slice_param->slice_data_offset, slice_param->macroblock_offset);

//	obj_context->mpeg2_frame_hdr.quant_scale = slice_param->quantiser_scale_code; // FIXME TODO

	return VA_STATUS_SUCCESS;
}
