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
#include "picture.h"
#include "buffer.h"
#include "context.h"
#include "surface.h"
#include "va_config.h"

#include "mpeg2.h"
#include "mpeg4.h"

#include <assert.h>
#include <string.h>

#include <errno.h>

#include <sys/ioctl.h>

#include <linux/videodev2.h>

/*
 * A Picture is an encoded input frame made of several buffers. A single input
 * can contain slice data, headers and IQ matrix. Each Picture is assigned a
 * request ID when created and each corresponding buffer might be turned into a
 * v4l buffers or extended control when rendered. Finally they are submitted to
 * kernel space when reaching EndPicture.
 */

VAStatus sunxi_cedrus_BeginPicture(VADriverContextP ctx, VAContextID context,
		VASurfaceID render_target)
{
	INIT_DRIVER_DATA
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	object_context_p obj_context;
	object_surface_p obj_surface;

	printf("> %s(%d)\n", __func__, render_target);

	obj_context = CONTEXT(context);
	assert(obj_context);

	obj_surface = SURFACE(render_target);
	assert(obj_surface);

	if (obj_surface->status == VASurfaceRendering) {
printf("%s: Go to rendering\n", __func__);
		vaStatus = sunxi_cedrus_SyncSurface(ctx, render_target);
		if (vaStatus != VA_STATUS_SUCCESS)
			return vaStatus;
	}

	obj_surface->status = VASurfaceRendering;
	obj_surface->request = (obj_context->num_rendered_surfaces)%INPUT_BUFFERS_NB+1;
	obj_surface->input_buf_index = obj_context->num_rendered_surfaces%INPUT_BUFFERS_NB;
	obj_context->num_rendered_surfaces ++;

printf("%s: surface %d\n", __func__, obj_surface->input_buf_index);
//	driver_data->slice_offset[obj_surface->input_buf_index] = 0;

	obj_context->current_render_target = obj_surface->base.id;

	return vaStatus;
}

VAStatus sunxi_cedrus_RenderPicture(VADriverContextP ctx, VAContextID context,
		VABufferID *buffers, int num_buffers)
{
	INIT_DRIVER_DATA
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	object_context_p obj_context;
	object_surface_p obj_surface;
	object_config_p obj_config;
	int i;

	printf("> %s()\n", __func__);

	obj_context = CONTEXT(context);
	assert(obj_context);

	obj_config = CONFIG(obj_context->config_id);
	if (NULL == obj_config)
	{
		vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
		return vaStatus;
	}

	obj_surface = SURFACE(obj_context->current_render_target);
	assert(obj_surface);

	printf("%s: current render surface: %d\n", __func__, obj_context->current_render_target);
	printf("%s: surface index %d\n", __func__, obj_surface->input_buf_index);
	printf("%s: rendering %d buffers\n", __func__, num_buffers);

	/* verify that we got valid buffer references */
	for(i = 0; i < num_buffers; i++)
	{
		printf("%s: buffer %d\n", __func__, buffers[i]);
		object_buffer_p obj_buffer = BUFFER(buffers[i]);
		assert(obj_buffer);
		if (NULL == obj_buffer)
		{
			vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
			break;
		}

		switch(obj_config->profile) {
			case VAProfileMPEG2Simple:
			case VAProfileMPEG2Main:
				if(obj_buffer->type == VASliceDataBufferType)
					vaStatus = sunxi_cedrus_render_mpeg2_slice_data(ctx, obj_context, obj_surface, obj_buffer);
				else if(obj_buffer->type == VAPictureParameterBufferType)
					vaStatus = sunxi_cedrus_render_mpeg2_picture_parameter(ctx, obj_context, obj_surface, obj_buffer);
				else if(obj_buffer->type == VASliceParameterBufferType)
					vaStatus = sunxi_cedrus_render_mpeg2_slice_parameter(ctx, obj_context, obj_surface, obj_buffer);
				else
printf("%s: MPEG2 profile else\n", __func__);
				break;
			case VAProfileMPEG4Simple:
			case VAProfileMPEG4AdvancedSimple:
			case VAProfileMPEG4Main:
printf("%s: MPEG4 profile type %d among %d/%d/%d\n", __func__, obj_buffer->type, VASliceDataBufferType, VAPictureParameterBufferType, VASliceParameterBufferType);
				if(obj_buffer->type == VASliceDataBufferType)
					vaStatus = sunxi_cedrus_render_mpeg4_slice_data(ctx, obj_context, obj_surface, obj_buffer);
				else if(obj_buffer->type == VAPictureParameterBufferType)
					vaStatus = sunxi_cedrus_render_mpeg4_picture_parameter(ctx, obj_context, obj_surface, obj_buffer);
				else if(obj_buffer->type == VASliceParameterBufferType)
					vaStatus = sunxi_cedrus_render_mpeg4_slice_parameter(ctx, obj_context, obj_surface, obj_buffer);
				else
printf("%s: MPEG4 profile else\n", __func__);
				break;
			default:
				break;
		}
	}

	return vaStatus;
}

VAStatus sunxi_cedrus_EndPicture(VADriverContextP ctx, VAContextID context)
{
	INIT_DRIVER_DATA
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	object_context_p obj_context;
	object_surface_p obj_surface;
	struct v4l2_buffer out_buf, cap_buf;
	struct v4l2_plane plane[1];
	struct v4l2_plane planes[2];
	struct v4l2_ext_control ctrl;
	struct v4l2_ext_controls ctrls;
	struct media_request_new media_request;
	object_config_p obj_config;
	int request_fd;
int rc;

	printf("> %s()\n", __func__);

	obj_context = CONTEXT(context);
	assert(obj_context);

	obj_surface = SURFACE(obj_context->current_render_target);
	assert(obj_surface);

	obj_config = CONFIG(obj_context->config_id);
	if (NULL == obj_config)
	{
		vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
		return vaStatus;
	}

	/*
	 * The real rendering is done in EndPicture instead of RenderPicture
	 * because the v4l2 driver expects to have the full corresponding
	 * extended control when a buffer is queued and we don't know in which
	 * order the different RenderPicture will be called.
	 */

	printf("%s: current render surface: %d\n", __func__, obj_context->current_render_target);
	printf("%s: surface index %d\n", __func__, obj_surface->input_buf_index);

	request_fd = driver_data->request_fds[obj_surface->input_buf_index];

	if(request_fd < 0) {
		assert(ioctl(driver_data->mem2mem_fd, VIDIOC_NEW_REQUEST, &media_request)==0);
		driver_data->request_fds[obj_surface->input_buf_index] = media_request.fd;
		request_fd = media_request.fd;
	}

	memset(plane, 0, sizeof(struct v4l2_plane));
	memset(planes, 0, 2 * sizeof(struct v4l2_plane));
	memset(&ctrl, 0, sizeof(struct v4l2_ext_control));
	memset(&ctrls, 0, sizeof(struct v4l2_ext_controls));

	memset(&(out_buf), 0, sizeof(out_buf));
	out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	out_buf.memory = V4L2_MEMORY_MMAP;
	out_buf.index = obj_surface->input_buf_index;
	out_buf.length = 1;
	out_buf.m.planes = plane;

	switch(obj_config->profile) {
		case VAProfileMPEG2Simple:
		case VAProfileMPEG2Main:
printf("%s: MPEG2 profile\n", __func__);
	obj_context->mpeg2_frame_hdr.slice_pos = 0;
	obj_context->mpeg2_frame_hdr.slice_len = driver_data->slice_offset[obj_surface->input_buf_index] * 8;
printf("%s: MPEG2 slice len total is %d\n", __func__, obj_context->mpeg2_frame_hdr.slice_len);

			out_buf.m.planes[0].bytesused = driver_data->slice_offset[obj_surface->input_buf_index];
printf("%s: ending on %d bytes used\n", __func__, out_buf.m.planes[0].bytesused);
			ctrl.id = V4L2_CID_MPEG_VIDEO_MPEG2_FRAME_HDR;
			ctrl.ptr = &obj_context->mpeg2_frame_hdr;
			ctrl.size = sizeof(obj_context->mpeg2_frame_hdr);
			break;
		case VAProfileMPEG4Simple:
		case VAProfileMPEG4AdvancedSimple:
		case VAProfileMPEG4Main:
printf("%s: MPEG4 profile\n", __func__);
			out_buf.m.planes[0].bytesused = obj_context->mpeg4_frame_hdr.slice_len/8;
			ctrl.id = V4L2_CID_MPEG_VIDEO_MPEG4_FRAME_HDR;
			ctrl.ptr = &obj_context->mpeg4_frame_hdr;
			ctrl.size = sizeof(obj_context->mpeg4_frame_hdr);
			break;
		default:
printf("%s: MPEG2 dummy/fallback non-profile\n", __func__);
			out_buf.m.planes[0].bytesused = 0;
			ctrl.id = V4L2_CID_MPEG_VIDEO_MPEG2_FRAME_HDR;
			ctrl.ptr = NULL;
			ctrl.size = 0;
			break;
	}

	driver_data->slice_offset[obj_surface->input_buf_index] = 0;

	memset(&(cap_buf), 0, sizeof(cap_buf));
	cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	cap_buf.memory = V4L2_MEMORY_MMAP;
	cap_buf.index = obj_surface->output_buf_index;
	cap_buf.length = 2;
	cap_buf.m.planes = planes;

	ctrls.controls = &ctrl;
	ctrls.count = 1;
	ctrls.request_fd = request_fd;

	rc = ioctl(driver_data->mem2mem_fd, VIDIOC_S_EXT_CTRLS, &ctrls);

	if (rc) {
		printf("ioctl VIDIOC_S_EXT_CTRLS failed with %d/%d/%s\n", rc, errno, strerror(errno));
		assert(0);
	}

	out_buf.request_fd = request_fd;

	if(ioctl(driver_data->mem2mem_fd, VIDIOC_QBUF, &cap_buf)) {
		obj_surface->status = VASurfaceSkipped;
		sunxi_cedrus_msg("Error when queuing output: %s\n", strerror(errno));
		return VA_STATUS_ERROR_UNKNOWN;
	}

	if(ioctl(driver_data->mem2mem_fd, VIDIOC_QBUF, &out_buf)) {
		obj_surface->status = VASurfaceSkipped;
		sunxi_cedrus_msg("Error when queuing input: %s\n", strerror(errno));

		ioctl(driver_data->mem2mem_fd, VIDIOC_DQBUF, &cap_buf);
		return VA_STATUS_ERROR_UNKNOWN;
	}

//	sunxi_cedrus_SyncSurface(ctx, obj_context->current_render_target);

	/* For now, assume that we are done with rendering right away */
	obj_context->current_render_target = -1;

	return vaStatus;
}


