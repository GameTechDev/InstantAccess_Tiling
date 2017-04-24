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
#include "DXGIFormat.h"
#include "IGFXExtensionsHelper.h"

// These are the test patterns for writing to the DRA memory
//	Tiled writes in the tiled pattern (can calculate linear address from tiled)
//	Linear Rows writes rows of linear memory to the tiled address (can calculate tiled address from linear)
//	Linear Columns writes columns of linear memory to the tiled address (can calculate tiled address form linear)
//	Linear Intrinsics is an optimized swizzling from linear to tiled memory 
#define MODE_TILED 0
#define MODE_LINEAR_ROWS 1
#define MODE_LINEAR_COLUMNS 2
#define MODE_LINEAR_INTRINSICS 3

#define TEST_SOLID 10
#define TEST_GRADIENT 11
#define TEST_COPY 12
#define TEST_READ 13

struct TextureInfo { UINT heightInBlocks, widthInBlocks, mips, bytesPerBlock, allocateBytes; DXGI_FORMAT dxgiFormat;};

// The following functions write or read the first mip level of Direct Resource Access (DRA) Textures
// The functions demonstrate how to convert between linear memory (for example the memory layout of a 
// mapped staging texture, y*width + x) and the tiled and swizzled memory layout that the DRA textures use.
// The tiles are column major, 32 rows and 128 bits wide with 4 bits specifying the x position within a column, 
// 5 bits specifying the y position in the column (32 rows per tile), and the higher order bits specifying x 
// position of the column (number of bits dependent on texture width) and tile row.
// Alternate columns in a tile (columns 1, 3, 5...) have an additional swizzle where 4 rows are swapped. 
// 
// The memory accessed through the DRA extention is "write combined memory" which is intended for high throughput. 
// When writing to the memory sequentially, several writes can be combined into a single more efficient write. The 
// optimizations for writing to write combined memory make reading from that memory very slow, so this should avoided. 
//
// The functions show the performance of writing in tile order versus writing in linear order by rows and linear 
// order by columns. An additional highly optimized method (provided by Axel Mamode) demonstrate an efficient method
// for copying linear memory to tiled memory.
//
// Generally, writing in tile order is fastest. The optimized linear version provides a very effecient method for
// calculating the correct tiled address, so it is slightly faster than the simple tile order implementation when
// linear addresses must be calculated. Writing in linear order by rows is nearly as fast
// Fabian Geisen's blog ("The ryg blog") is very good source of information for tiling/swizzling and write combined memory
// particularly the posts: "Texture tiling and swizzling" (http://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/)
// and "Write combining is not your friend" (http://fgiesen.wordpress.com/2013/01/29/write-combining-is-not-your-friend/)
//
// WriteDRA_Solid
// Writes a solid color to the DRA buffer. The mode specifies how the memory is written. 
void WriteDRA_Solid(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubresourceData, TextureInfo *pTexInfo, UINT mip, UINT color);

// WriteDRA_Copy
// Writes a copy of a linearly mapped texture to the tiled DRA resource
void WriteDRA_Copy(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubResourceData, TextureInfo *pTexInfo,
                   UINT mip, D3D11_MAPPED_SUBRESOURCE &texData);

// ReadDRA
// Reads the memory of a DRA resource.
void ReadDRA(UINT mode, INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA * pGPUSubResourceData, TextureInfo *pTexInfo,
                   UINT mip, D3D11_MAPPED_SUBRESOURCE &texData);
