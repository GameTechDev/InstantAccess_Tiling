/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////
#include "InstantAccess_Tiling.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cassert>

#include "resource.h"

#include <stdio.h>
#include "emmintrin.h"

// Each function uses the following helper functions to convert to and from tiled addresses.

UINT swizzle_x(UINT x /*in bytes*/)
{
	UINT x_low = (x & 0xF); 
	UINT x_high = (x & 0xFFFFFFF0);
	return (x_low | (x_high << 5)); // 5 bits are coming from Y in the tile
}

// UnswizzleX also contains tile row information in high bits
// must mask out using row pitch in calling function
UINT UnswizzleX(UINT x)
{
	UINT x_low = x & 0xF;
	UINT x_high = (0xFFFFFFF0 << 5) & x;
	return x_low | (x_high>>5);
}

UINT swizzle_y(UINT y /* in texels */)
{
	UINT y_low = (y & 0x1f);
	return y_low << 4; // TileY is always 4bits 
}

UINT UnswizzleY(UINT y)
{
	UINT unswizzled = y ^ ((y & (1 << 9)) >> 3);
	return ((0x1f << 4) & unswizzled) >> 4;
}

bool IsPow2(UINT input) { return (input & (input - 1)) == 0; }

// TileY contains a swizzling operation on the memory addressing path.
// this is what this bit operation computes (swizzled[6] = tiled[6] ^ tiled[9])
UINT swizzleAddress(UINT tiledAddr) 
{ 
	return tiledAddr ^ ((tiledAddr & (1 << 9)) >> 3);
}

#define one_g (1 << 8)
#define two_g (2 << 8)
#define three_g (3 << 8)


void WriteDRA_Copy(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubResourceData, TextureInfo *pTexInfo,
				   UINT mip, D3D11_MAPPED_SUBRESOURCE &texData)
{
	const UINT TileH = 32; // height of tile in blocks

	// From loaded texture
	const UINT texWidthInBlock = pTexInfo->widthInBlocks;
	const UINT texHeightInBlock = pTexInfo->heightInBlocks;
	const UINT bytesPerBlock = pTexInfo->bytesPerBlock;
	// Width in bytes of the map (size of the first row of blocks)
	UINT mapPitch = pGPUSubResourceData->Pitch;
	// Offset to the mip
	const UINT xoffset = pGPUSubResourceData->XOffset; // in bytes
	const UINT yoffset = pGPUSubResourceData->YOffset; // in blocks

	// Mip height and width
	// this is incorrect for non-power-of-two sizes...
	// 12 texels, e.g. are 3 DXT blocks. 6 texels are 2.
	assert(IsPow2(texHeightInBlock) && IsPow2(texWidthInBlock));
	const UINT mipHeightInBlock = (texHeightInBlock >> mip) > 0 ? (texHeightInBlock >> mip) : 1;
	const UINT mipWidthInBlock  = (texWidthInBlock >> mip) > 0 ? (texWidthInBlock >> mip) : 1;
	const UINT mipWidthInBytes  = mipWidthInBlock * bytesPerBlock; 

	// Base address of Tiled Memory
	UINT_PTR destBase = (UINT_PTR)pGPUSubResourceData->pBaseAddress;      

	// This is the begining of rygs method
	UINT offs_x0 = swizzle_x(xoffset);
	UINT offs_y  = swizzle_y(yoffset); 
	// incr_y corresponds to the byte size of a full row of tiles.
	UINT incr_y  = swizzle_x(mapPitch);
   
    if (mode == MODE_LINEAR_ROWS)
    {
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {
                __m128i * pSrc = (__m128i *)((BYTE*)texData.pData + y*texData.RowPitch);
                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                }
            }
        }
        if(pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {
                __m128i * pSrc = (__m128i *)((BYTE*)texData.pData + y*texData.RowPitch);
                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
                    UINT offset = swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4);
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + offset);
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                }
            }
        }           
	}
	else if(mode == MODE_LINEAR_COLUMNS)
	{
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            for (UINT x = 0; x < mipWidthInBlock; x += 4)
            {
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    __m128i * pSrc = (__m128i *)((BYTE*)texData.pData + y*texData.RowPitch + x * 4);
                    UINT yadd = (y / 32) * 32;
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                }
            }
        }
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            for (UINT x = 0; x < mipWidthInBlock; x += 4)
            {
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    __m128i * pSrc = (__m128i *)((BYTE*)texData.pData + y*texData.RowPitch + x * 4);
                    UINT yadd = (y / 32) * 32;
                    UINT offset = swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4);
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + offset);
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                }
            }

        }
	}
	else if(mode == MODE_TILED)
	{
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {

            __m128i* thisCL = (__m128i*) destBase;
            UINT offset = 0;
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {

                UINT yadd = (y / 32) * 32;

                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
                    UINT usy = UnswizzleY(offset) + yadd;
                    __m128i * pSrc = (__m128i*)((BYTE*)texData.pData + texData.RowPitch * usy + (UnswizzleX(offset) & (mipWidthInBytes - 1)));
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                    offset += 16;
                }
            }
        }
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            __m128i* thisCL = (__m128i*) destBase;
            UINT offset = 0;
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {

                UINT yadd = (y / 32) * 32;

                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
					UINT usy = (((0x1f << 4) & offset) >> 4) + yadd;
                    __m128i * pSrc = (__m128i*)((BYTE*)texData.pData + texData.RowPitch * usy + (UnswizzleX(offset) & (mipWidthInBytes - 1)));
                    _mm_stream_si128(thisCL, *pSrc);
                    thisCL++;
                    pSrc++;
                    offset += 16;
                }
            }
        }

	}
    else if (mode == MODE_LINEAR_INTRINSICS)
    {
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            // we use 2 different code paths depending on whether we can process a single CPU cacheline worth of data
            // (which, in TileY, corresponds to a 16Bx4rows of data - 2x4 DXT1 blocks, 1x4 DXT5 blocks, 4x4 RBBA8...) 
            // at a time or if we have to rely on finer-grained, non-aligned
            // access (which only happens at the lowest mipmap levels)
            // If we do have enough data to process, the inner loop processes 4 source block rows at a time, 
            // in chunks of 16B per row  
            if (xoffset % 16 == 0 && yoffset % 4 == 0 &&
                mipWidthInBytes % 16 == 0 && mipHeightInBlock % 4 == 0)
            {
                // swizzle_x/swizzle_y are leveraged to compute the increment needed when moving 
                // into the 2d destination surface in X and Y direction. 
                // we want x_mask to represent the increment for 16 bytes
                UINT x_mask = swizzle_x((UINT)-16);
                // Likewise for y direction, we want 4 rows at a time
                UINT y_mask = swizzle_y((UINT)-4);

                // offs_y (below) only encodes the y offset used for addressing _within the tile_.
                // offs_x0 combines 2 parts of the addressing: 
                // 1. the complete x offset
                // 2. the part of the y offset that is used to know which tile row the current set of rows is part of.
                //    This is what the next line computes (`yoffset / TileH' is the tile row index) 
                // As a result, when offs_y wraps (i.e. the algorithm wraps into the next tile row), offs_x0 needs to be updated to 
                // the next row of tiles (with incr_y again)
                offs_x0 += incr_y * (yoffset / TileH);
                BYTE * baseSrc = (BYTE*)texData.pData;
                for (UINT y = 0; y < mipHeightInBlock; y += 4)
                {
                    // read 4 texel rows at time
                    __m128i *src0 = (__m128i *) (baseSrc + y * mipWidthInBytes);
                    __m128i *src1 = (__m128i *) (baseSrc + (y + 1) * mipWidthInBytes);
                    __m128i *src2 = (__m128i *) (baseSrc + (y + 2) * mipWidthInBytes);
                    __m128i *src3 = (__m128i *) (baseSrc + (y + 3) * mipWidthInBytes);
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 16)
                    {
                        // inner loop reads a single cacheline at a time.
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzledAddr);
                        // now stream the 64B of data to their final destination
                        _mm_stream_si128(thisCL, *src0);
                        thisCL++;
                        src0++;
                        _mm_stream_si128(thisCL, *src1);
                        thisCL++;
                        src1++;
                        _mm_stream_si128(thisCL, *src2);
                        thisCL++;
                        src2++;
                        _mm_stream_si128(thisCL, *src3);
                        thisCL++;
                        src3++;
                        // move to next 4x4 in source order. 
                        // This uses a couple of tricks based on bit propagation and 2's complement.
                        // read rygs method to understand it.
                        offs_x = (offs_x - x_mask) & x_mask;
                    }
                    // same trick as for offs_x
                    offs_y = (offs_y - y_mask) & y_mask;
                    // wrap into next tile row if required
                    if (!offs_y) offs_x0 += incr_y;
                }


            }
            else {
                // the 1x1 path follows exactly the same pattern as the 4x4 path,
                // but its inner loop only processes a single UINT, and as such is less cache/CPU friendly.
                // read the first implementation for additional details.
                UINT x_mask = swizzle_x((UINT)-4);
                UINT y_mask = swizzle_y(~0u);

                offs_x0 += incr_y * (yoffset / TileH);
                BYTE * baseSrc = (BYTE*)texData.pData;
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT *src = (UINT *)(baseSrc + y * mipWidthInBytes);
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 4)
                    {
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        *((UINT *)((BYTE*)destBase + swizzledAddr)) = *src++;
                        offs_x = (offs_x - x_mask) & x_mask;
                    }

                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) { offs_x0 += incr_y; }
                }
            }
        }
        if (pGPUSubResourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            // we use 2 different code paths depending on whether we can process a single CPU cacheline worth of data
            // (which, in TileY, corresponds to a 16Bx4rows of data - 2x4 DXT1 blocks, 1x4 DXT5 blocks, 4x4 RBBA8...) 
            // at a time or if we have to rely on finer-grained, non-aligned
            // access (which only happens at the lowest mipmap levels)
            // If we do have enough data to process, the inner loop processes 4 source block rows at a time, 
            // in chunks of 16B per row  
            if (xoffset % 16 == 0 && yoffset % 4 == 0 &&
                mipWidthInBytes % 16 == 0 && mipHeightInBlock % 4 == 0)
            {
                // swizzle_x/swizzle_y are leveraged to compute the increment needed when moving 
                // into the 2d destination surface in X and Y direction. 
                // we want x_mask to represent the increment for 16 bytes
                UINT x_mask = swizzle_x((UINT)-16);
                // Likewise for y direction, we want 4 rows at a time
                UINT y_mask = swizzle_y((UINT)-4);

                // offs_y (below) only encodes the y offset used for addressing _within the tile_.
                // offs_x0 combines 2 parts of the addressing: 
                // 1. the complete x offset
                // 2. the part of the y offset that is used to know which tile row the current set of rows is part of.
                //    This is what the next line computes (`yoffset / TileH' is the tile row index) 
                // As a result, when offs_y wraps (i.e. the algorithm wraps into the next tile row), offs_x0 needs to be updated to 
                // the next row of tiles (with incr_y again)
                offs_x0 += incr_y * (yoffset / TileH);
                BYTE * baseSrc = (BYTE*)texData.pData;
                for (UINT y = 0; y < mipHeightInBlock; y += 4)
                {
                    // read 4 texel rows at time
                    __m128i *src0 = (__m128i *) (baseSrc + y * mipWidthInBytes);
                    __m128i *src1 = (__m128i *) (baseSrc + (y + 1) * mipWidthInBytes);
                    __m128i *src2 = (__m128i *) (baseSrc + (y + 2) * mipWidthInBytes);
                    __m128i *src3 = (__m128i *) (baseSrc + (y + 3) * mipWidthInBytes);
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 16)
                    {
                        // inner loop reads a single cacheline at a time.
                        UINT tiledAddr = offs_y + offs_x;
                        __m128i * thisCL = (__m128i *)((BYTE*)destBase + tiledAddr);
                        // now stream the 64B of data to their final destination
                        _mm_stream_si128(thisCL, *src0);
                        thisCL++;
                        src0++;
                        _mm_stream_si128(thisCL, *src1);
                        thisCL++;
                        src1++;
                        _mm_stream_si128(thisCL, *src2);
                        thisCL++;
                        src2++;
                        _mm_stream_si128(thisCL, *src3);
                        thisCL++;
                        src3++;
                        // move to next 4x4 in source order. 
                        // This uses a couple of tricks based on bit propagation and 2's complement.
                        // read rygs method to understand it.
                        offs_x = (offs_x - x_mask) & x_mask;
                    }
                    // same trick as for offs_x
                    offs_y = (offs_y - y_mask) & y_mask;
                    // wrap into next tile row if required
                    if (!offs_y) offs_x0 += incr_y;
                }


            }
            else {
                // the 1x1 path follows exactly the same pattern as the 4x4 path,
                // but its inner loop only processes a single UINT, and as such is less cache/CPU friendly.
                // read the first implementation for additional details.
                UINT x_mask = swizzle_x((UINT)-4);
                UINT y_mask = swizzle_y(~0u);

                offs_x0 += incr_y * (yoffset / TileH);
                BYTE * baseSrc = (BYTE*)texData.pData;
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT *src = (UINT *)(baseSrc + y * mipWidthInBytes);
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 4)
                    {
                        UINT tiledAddr = offs_y + offs_x;
                        *((UINT *)((BYTE*)destBase + tiledAddr)) = *src++;
                        offs_x = (offs_x - x_mask) & x_mask;
                    }

                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) { offs_x0 += incr_y; }
                }
            }
        }
    }
}

void WriteDRA_Solid(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubresourceData, TextureInfo *pTexInfo, UINT mip, UINT color)
{
	const UINT TileH = 32; // height of tile in blocks

	// From loaded texture
	const UINT texWidthInBlock = pTexInfo->widthInBlocks;
	const UINT texHeightInBlock = pTexInfo->heightInBlocks;
	const UINT bytesPerBlock = pTexInfo->bytesPerBlock;
	// Width in bytes of the map (size of the first row of blocks)
	UINT mapPitch = pGPUSubresourceData->Pitch;
	// Offset to the mip
	const UINT xoffset = pGPUSubresourceData->XOffset; // in bytes
	const UINT yoffset = pGPUSubresourceData->YOffset; // in blocks

	// Mip height and width
	// this is incorrect for non-power-of-two sizes...
	// 12 texels, e.g. are 3 DXT blocks. 6 texels are 2.
	assert(IsPow2(texHeightInBlock) && IsPow2(texWidthInBlock));
	const UINT mipHeightInBlock = (texHeightInBlock >> mip) > 0 ? (texHeightInBlock >> mip) : 1;
	const UINT mipWidthInBlock  = (texWidthInBlock >> mip) > 0 ? (texWidthInBlock >> mip) : 1;
	const UINT mipWidthInBytes  = mipWidthInBlock * bytesPerBlock; 

	// Base address of Tiled Memory
	UINT_PTR destBase = (UINT_PTR)pGPUSubresourceData->pBaseAddress;      

	// This is the begining of rygs method
	UINT offs_x0 = swizzle_x(xoffset);
	UINT offs_y  = swizzle_y(yoffset); 
	// incr_y corresponds to the byte size of a full row of tiles.
	UINT incr_y  = swizzle_x(mapPitch);

    if (mode == MODE_LINEAR_ROWS)
    {
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
            __m128i *src0 = (__m128i *) (&baseSrc0);
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {
                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *src0);
                }
            }
        }
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
            __m128i *src0 = (__m128i *) (&baseSrc0);
            for (UINT y = 0; y < mipHeightInBlock; y++)
            {
                for (UINT x = 0; x < mipWidthInBlock; x += 4)
                {
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));
                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *src0);
                }
            }
        }
    }
	else if(mode == MODE_LINEAR_COLUMNS)
	{
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
            __m128i *src0 = (__m128i *) (&baseSrc0);
            for (UINT x = 0; x < mipWidthInBlock; x += 4)
            {
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));

                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *src0);
                }
            }
        }
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
            __m128i *src0 = (__m128i *) (&baseSrc0);
            for (UINT x = 0; x < mipWidthInBlock; x += 4)
            {
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT swizzled = swizzleAddress(swizzle_y(yoffset + y)
                        + incr_y * ((yoffset + y) / TileH) + swizzle_x(xoffset + x * 4));

                    __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzled);
                    _mm_stream_si128(thisCL, *src0);
                }
            }
        }
	}
	else if(mode == MODE_TILED)
	{
		__m128i * thisCL = (__m128i *)((BYTE*)destBase);//

		__declspec(align(16)) UINT baseSrc0[] = {color, color, color, color};
		__m128i *src0 = (__m128i *) (&baseSrc0);
		for(UINT x = 0; x < mipWidthInBlock*mipHeightInBlock/4; x++)
		{
			_mm_stream_si128(thisCL++, *src0);       
		}

 	}
	else if(mode == MODE_LINEAR_INTRINSICS)
	{
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y)
        {
            if (xoffset % 16 == 0 && yoffset % 4 == 0 &&
                mipWidthInBytes % 16 == 0 && mipHeightInBlock % 4 == 0)
            {
                UINT x_mask = swizzle_x((UINT)-16);
                UINT y_mask = swizzle_y((UINT)-4);

                offs_x0 += incr_y * (yoffset / TileH);

                __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
                __m128i *src0 = (__m128i *) (&baseSrc0);

                for (UINT y = 0; y < mipHeightInBlock; y += 4)
                {
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 16)
                    {
                        // inner loop reads a single cacheline at a time.
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzledAddr);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        offs_x = (offs_x - x_mask) & x_mask;
                    }
                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) offs_x0 += incr_y;
                }
            }
            else {
                UINT x_mask = swizzle_x((UINT)-4);
                UINT y_mask = swizzle_y(~0u);

                offs_x0 += incr_y * (yoffset / TileH);
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 4)
                    {
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        *((UINT *)((BYTE*)destBase + swizzledAddr)) = color;
                        offs_x = (offs_x - x_mask) & x_mask;
                    }

                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) { offs_x0 += incr_y; }
                }
            }
        }
        if (pGPUSubresourceData->TileFormat == INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE)
        {
            if (xoffset % 16 == 0 && yoffset % 4 == 0 &&
                mipWidthInBytes % 16 == 0 && mipHeightInBlock % 4 == 0)
            {
                UINT x_mask = swizzle_x((UINT)-16);
                UINT y_mask = swizzle_y((UINT)-4);

                offs_x0 += incr_y * (yoffset / TileH);

                __declspec(align(16)) UINT baseSrc0[] = { color, color, color, color };
                __m128i *src0 = (__m128i *) (&baseSrc0);

                for (UINT y = 0; y < mipHeightInBlock; y += 4)
                {
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 16)
                    {
                        // inner loop reads a single cacheline at a time.
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        __m128i * thisCL = (__m128i *)((BYTE*)destBase + swizzledAddr);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        _mm_stream_si128(thisCL++, *src0);
                        offs_x = (offs_x - x_mask) & x_mask;
                    }
                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) offs_x0 += incr_y;
                }
            }
            else {
                UINT x_mask = swizzle_x((UINT)-4);
                UINT y_mask = swizzle_y(~0u);

                offs_x0 += incr_y * (yoffset / TileH);
                for (UINT y = 0; y < mipHeightInBlock; y++)
                {
                    UINT offs_x = offs_x0;

                    for (UINT x = 0; x < mipWidthInBytes; x += 4)
                    {
                        UINT tiledAddr = offs_y + offs_x;
                        UINT swizzledAddr = swizzleAddress(tiledAddr);
                        *((UINT *)((BYTE*)destBase + swizzledAddr)) = color;
                        offs_x = (offs_x - x_mask) & x_mask;
                    }

                    offs_y = (offs_y - y_mask) & y_mask;
                    if (!offs_y) { offs_x0 += incr_y; }
                }
            }
        }
	}
}

void ReadDRA(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubresourceData, TextureInfo *pTexInfo,
			 UINT mip, D3D11_MAPPED_SUBRESOURCE &texData)
{
	const UINT TileH = 32; // height of tile in blocks

	// From loaded texture
	const UINT texWidthInBlock = pTexInfo->widthInBlocks;
	const UINT texHeightInBlock = pTexInfo->heightInBlocks;
	const UINT bytesPerBlock = pTexInfo->bytesPerBlock;
	// Width in bytes of the map (size of the first row of blocks)
	UINT mapPitch = pGPUSubresourceData->Pitch;
	// Offset to the mip
	const UINT xoffset = pGPUSubresourceData->XOffset; // in bytes
	const UINT yoffset = pGPUSubresourceData->YOffset; // in blocks

	// Mip height and width
	// this is incorrect for non-power-of-two sizes...
	// 12 texels, e.g. are 3 DXT blocks. 6 texels are 2.
	assert(IsPow2(texHeightInBlock) && IsPow2(texWidthInBlock));
	const UINT mipHeightInBlock = (texHeightInBlock >> mip) > 0 ? (texHeightInBlock >> mip) : 1;
	const UINT mipWidthInBlock  = (texWidthInBlock >> mip) > 0 ? (texWidthInBlock >> mip) : 1;
	
	// Base address of Tiled Memory
	__m128i* srcBase = (__m128i*)pGPUSubresourceData->pBaseAddress;
	__m128i* destBase = (__m128i*)texData.pData;
	
	// incr_y corresponds to the byte size of a full row of tiles.
	UINT incr_y  = swizzle_x(mapPitch);

	if(mode == MODE_LINEAR_ROWS)
	{
		__m128i *thisCL = NULL;
		for(UINT y = 0; y < mipHeightInBlock; y++)
		{
			for(UINT x = 0; x < mipWidthInBlock; x+=4)
			{
				UINT swizzled = swizzleAddress(swizzle_y(yoffset+y) 
					+ incr_y * ((yoffset+y) / TileH) + (swizzle_x(xoffset+x*4)));
				thisCL = (__m128i *)((BYTE*)srcBase + swizzled);
				_mm_stream_si128(destBase++, *thisCL);
			}
		}
	}
	else if(mode == MODE_LINEAR_COLUMNS)
	{
		__m128i* thisCL = NULL;
		for(UINT x = 0; x < mipWidthInBlock; x+=4)
		{
			for(UINT y = 0; y < mipHeightInBlock; y++)
			{
				UINT swizzled = swizzleAddress(swizzle_y(yoffset+y) 
					+ incr_y * ((yoffset+y) / TileH) + (swizzle_x(xoffset+x*4)));
				thisCL = (__m128i *)((BYTE*)srcBase + swizzled);
				_mm_stream_si128(destBase++, *thisCL);
			}
		}
	}
	else if(mode == MODE_TILED)
	{
		destBase = (__m128i*)texData.pData;
		for(UINT x = 0; x < mipWidthInBlock*mipHeightInBlock; x+=4)
		{
		    _mm_stream_si128(destBase, *srcBase);
            destBase++;
            srcBase++;
		}
	}
}

