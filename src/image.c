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

#include "sunxi_cedrus.h"
#include "image.h"
#include "surface.h"
#include "buffer.h"

#include <assert.h>
#include <string.h>

#include "tiled_yuv.h"

VAStatus SunxiCedrusCreateImage(VADriverContextP context, VAImageFormat *format,
	int width, int height, VAImage *image)
{
	struct sunxi_cedrus_driver_data *driver_data =
		(struct sunxi_cedrus_driver_data *) context->pDriverData;
	struct object_image *image_object;
	VABufferID buffer_id;
	VAImageID id;
	VAStatus status;
	int sizeY, sizeUV;

	sizeY = width * height;
	sizeUV = (width * (height + 1) / 2);

	id = object_heap_allocate(&driver_data->image_heap);
	image_object = IMAGE(id);
	if (image_object == NULL)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	status = SunxiCedrusCreateBuffer(context, 0, VAImageBufferType, sizeY + sizeUV, 1, NULL, &buffer_id);
	if (status != VA_STATUS_SUCCESS) {
		object_heap_free(&driver_data->image_heap, (struct object_base *) image_object);
		return status;
	}

	image_object->buffer_id = buffer_id;

	memset(image, 0, sizeof(*image));

	image->format = *format;
	image->width = width;
	image->height = height;
	image->num_planes = 2;
	image->pitches[0] = (width + 31) & ~31;
	image->pitches[1] = (width + 31) & ~31;
	image->offsets[0] = 0;
	image->offsets[1] = sizeY;
	image->data_size  = sizeY + sizeUV;
	image->buf = buffer_id;
	image->image_id = id;

	return VA_STATUS_SUCCESS;
}

VAStatus SunxiCedrusDestroyImage(VADriverContextP context, VAImageID image_id)
{
	struct sunxi_cedrus_driver_data *driver_data =
		(struct sunxi_cedrus_driver_data *) context->pDriverData;
	struct object_image *image_object;
	VAStatus status;

	image_object = IMAGE(image_id);
	if (image_object == NULL)
		return VA_STATUS_ERROR_INVALID_IMAGE;

	status = SunxiCedrusDestroyBuffer(context, image_object->buffer_id);
	if (status != VA_STATUS_SUCCESS)
		return status;

	object_heap_free(&driver_data->image_heap, (struct object_base *) image_object);

	return VA_STATUS_SUCCESS;
}

void ConvertMb32420ToNv21Y(char* pSrc,char* pDst,int nWidth, int nHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dstAsm = NULL;
    char *srcAsm = NULL;
    char bufferU[32];
    int nWidthMatchFlag = 0;
    int nCopyMbWidth = 0;

    nLineStride = (nWidth + 15) &~15;
    nMbWidth = (nWidth+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight+31)&~31;
    nMbHeight /= 32;
    ptr = pSrc;

    nWidthMatchFlag = 0;
	nCopyMbWidth = nMbWidth-1;

    if(nMbWidth*32 == nLineStride)
    {
    	nWidthMatchFlag = 1;
    	nCopyMbWidth = nMbWidth;

    }
    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nCopyMbWidth; j++)
    	{
    		for(m=0; m<32; m++)
    		{
    			if((i*32 + m) >= nHeight)
    		  	{
    				ptr += 32;
    		    	continue;
    		  	}
    			srcAsm = ptr;
    			lineNum = i*32 + m;           //line num
    			offset =  lineNum*nLineStride + j*32;
    			dstAsm = pDst+ offset;

    			 asm volatile (
    					        "vld1.8         {d0 - d3}, [%[srcAsm]]              \n\t"
    					        "vst1.8         {d0 - d3}, [%[dstAsm]]              \n\t"
    					       	: [dstAsm] "+r" (dstAsm), [srcAsm] "+r" (srcAsm)
    					       	:  //[srcY] "r" (srcY)
    					       	: "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31");
    			ptr += 32;
    		}
    	}

    	if(nWidthMatchFlag == 1)
    	{
    		continue;
    	}
    	for(m=0; m<32; m++)
    	{
    		if((i*32 + m) >= nHeight)
    		{
    			ptr += 32;
    	    	continue;
    	   	}
    		dstAsm = bufferU;
    		srcAsm = ptr;
    	 	lineNum = i*32 + m;           //line num
    		offset =  lineNum*nLineStride + j*32;

    	   	 asm volatile (
    	    	      "vld1.8         {d0 - d3}, [%[srcAsm]]              \n\t"
    	              "vst1.8         {d0 - d3}, [%[dstAsm]]              \n\t"
    	         	    : [dstAsm] "+r" (dstAsm), [srcAsm] "+r" (srcAsm)
    	    	     	:  //[srcY] "r" (srcY)
    	    	    	: "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31");
    	   	ptr += 32;
    	   	for(k=0; k<32; k++)
    	   	{
    	   		if((j*32+ k) >= nLineStride)
    	   	   	{
    	   			break;
    	   	  	}
    	   	 	pDst[offset+k] = bufferU[k];
    	   	}
    	}
    }
}


void ConvertMb32420ToNv21C(char* pSrc,char* pDst,int nPicWidth, int nPicHeight)
{
	int nMbWidth = 0;
	int nMbHeight = 0;
	int i = 0;
	int j = 0;
	int m = 0;
	int k = 0;
    int nLineStride=0;
    int lineNum = 0;
    int offset = 0;
    char* ptr = NULL;
    char *dst0Asm = NULL;
    char *dst1Asm = NULL;
    char *srcAsm = NULL;
    char bufferV[16], bufferU[16];
    int nWidth = 0;
    int nHeight = 0;

    nWidth = (nPicWidth+1)/2;
    nHeight = (nPicHeight+1)/2;

    nLineStride = (nWidth*2 + 15) &~15;
    nMbWidth = (nWidth*2+31)&~31;
    nMbWidth /= 32;

    nMbHeight = (nHeight+31)&~31;
    nMbHeight /= 32;


    ptr = pSrc;

    for(i=0; i<nMbHeight; i++)
    {
    	for(j=0; j<nMbWidth; j++)
    	{
    		for(m=0; m<32; m++)
    		{
    			if((i*32 + m) >= nHeight)
    			{
    				ptr += 32;
    				continue;
        		}

    			dst0Asm = bufferU;
    			dst1Asm = bufferV;
    			srcAsm = ptr;
    			lineNum = i*32 + m;           //line num
    			offset =  lineNum*nLineStride + j*32;

    			asm volatile(
    					"vld2.8         {d0-d3}, [%[srcAsm]]              \n\t"
    			    	"vst1.8         {d0,d1}, [%[dst0Asm]]              \n\t"
    			    	"vst1.8         {d2,d3}, [%[dst1Asm]]              \n\t"
    			    	: [dst0Asm] "+r" (dst0Asm), [dst1Asm] "+r" (dst1Asm), [srcAsm] "+r" (srcAsm)
    			        :  //[srcY] "r" (srcY)
    			        : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d28", "d29", "d30", "d31"
    			     );
    			ptr += 32;


    			for(k=0; k<16; k++)
    			{
    				if((j*32+ 2*k) >= nLineStride)
    				{
    					break;
    				}
    				pDst[offset+2*k]   = bufferV[k];
    			   	pDst[offset+2*k+1] = bufferU[k];
    			}
    		}
    	}
    }
}


VAStatus SunxiCedrusDeriveImage(VADriverContextP context,
	VASurfaceID surface_id, VAImage *image)
{
	struct sunxi_cedrus_driver_data *driver_data =
		(struct sunxi_cedrus_driver_data *) context->pDriverData;
	struct object_surface *surface_object;
	struct object_buffer *buffer_object;
	VAImageFormat format;
	VAStatus status;

	surface_object = SURFACE(surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	if (surface_object->status == VASurfaceRendering) {
		status = SunxiCedrusSyncSurface(context, surface_id);
		if (status != VA_STATUS_SUCCESS)
			return status;
	} else if (surface_object->status == VASurfaceReady) {
		return VA_STATUS_SUCCESS;
	}

	format.fourcc = VA_FOURCC_NV12;

	status = SunxiCedrusCreateImage(context, &format, surface_object->width, surface_object->height, image);
	if (status != VA_STATUS_SUCCESS)
		return status;

	buffer_object = BUFFER(image->buf);
	if (buffer_object == NULL)
		return VA_STATUS_ERROR_INVALID_BUFFER;

	/* TODO: Use an appropriate DRM plane instead */
	ConvertMb32420ToNv21Y(surface_object->destination_data[0], buffer_object->data, image->width, image->height);
	ConvertMb32420ToNv21C(surface_object->destination_data[1], buffer_object->data + image->width*image->height, image->width, image->height);

//	tiled_to_planar(surface_object->destination_data[0], buffer_object->data, image->pitches[0], image->width, image->height);
//	tiled_to_planar(surface_object->destination_data[1], buffer_object->data + image->width*image->height, image->pitches[1], image->width, image->height/2);

	surface_object->status = VASurfaceReady;

	return VA_STATUS_SUCCESS;
}

VAStatus SunxiCedrusQueryImageFormats(VADriverContextP context,
	VAImageFormat *formats, int *formats_count)
{
	formats[0].fourcc = VA_FOURCC_NV12;
	*formats_count = 1;

	return VA_STATUS_SUCCESS;
}

VAStatus SunxiCedrusSetImagePalette(VADriverContextP context,
	VAImageID image_id, unsigned char *palette)
{
	return VA_STATUS_SUCCESS;
}

VAStatus SunxiCedrusGetImage(VADriverContextP context, VASurfaceID surface_id,
	int x, int y, unsigned int width, unsigned int height,
	VAImageID image_id)
{
	return VA_STATUS_SUCCESS;
}

VAStatus SunxiCedrusPutImage(VADriverContextP context, VASurfaceID surface_id,
	VAImageID image, int src_x, int src_y, unsigned int src_width,
	unsigned int src_height, int dst_x, int dst_y, unsigned int dst_width,
	unsigned int dst_height)
{
	return VA_STATUS_SUCCESS;
}
