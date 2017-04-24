/*****************************************************************************\

INTEL CONFIDENTIAL
Copyright 2011
Intel Corporation All Rights Reserved.

The source code contained or described herein and all documents related to the
source code ("Material") are owned by Intel Corporation or its suppliers or
licensors. Title to the Material remains with Intel Corporation or its suppliers
and licensors. The Material contains trade secrets and proprietary and confidential
information of Intel or its suppliers and licensors. The Material is protected by
worldwide copyright and trade secret laws and treaty provisions. No part of the
Material may be used, copied, reproduced, modified, published, uploaded, posted
transmitted, distributed, or disclosed in any way without Intel’s prior express
written permission.

No license under any patent, copyright, trade secret or other intellectual
property right is granted to or conferred upon you by disclosure or delivery
of the Materials, either expressly, by implication, inducement, estoppel
or otherwise. Any license under such intellectual property rights must be
express and approved by Intel in writing.

File Name:  ID3D10ApiExtensionsV1_0.h

Abstract:   Defines D3D10 UMD Extensions

Notes:      This file is intended to be included outside the UMD to define
            the interface(s) for the UMD extensions.

\*****************************************************************************/
#pragma once

/*****************************************************************************\
MACRO: EXTENSION_INTERFACE_VERSION
\*****************************************************************************/
#define EXTENSION_INTERFACE_VERSION_1_0 0x00010000
#ifndef EXTENSION_INTERFACE_VERSION
    #define EXTENSION_INTERFACE_VERSION EXTENSION_INTERFACE_VERSION_1_0
#endif

namespace INTC
{

/*****************************************************************************\
CONST: CAPS_EXTENSION_KEY
PURPOSE: KEY to pass to UMD
\*****************************************************************************/
const char CAPS_EXTENSION_KEY[16] = {
    'I','N','T','C',
    'E','X','T','N',
    'C','A','P','S',
    'F','U','N','C' };

/*****************************************************************************\
STRUCT: EXTENSION_BASE
PURPOSE: Base data structure for extension initialization data
\*****************************************************************************/
struct EXTENSION_BASE
{
    // Input:
    char    Key[16];                // CAPS_EXTENSION_KEY
    UINT    ApplicationVersion;     // EXTENSION_INTERFACE_VERSION
};

/*****************************************************************************\
STRUCT: CAPS_EXTENSION_1_0
PURPOSE: Caps data structure
\*****************************************************************************/
struct CAPS_EXTENSION_1_0 : EXTENSION_BASE
{
    // Output:
    UINT    DriverVersion;          // EXTENSION_INTERFACE_VERSION
    UINT    DriverBuildNumber;      // BUILD_NUMBER
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef CAPS_EXTENSION_1_0  CAPS_EXTENSION;
#endif

#ifndef D3D10_UMD
/*****************************************************************************\

FUNCTION:
    GetExtensionCaps

PURPOSE:
    Gets extension caps table from Intel graphics driver

INPUT:
    ID3D11Device* pd3dDevice - the D3D11 device to query

OUTPUT:
    If HRESULT is S_OK, then CAPS_EXTENSION* pCaps will be initialized by
    the driver

\*****************************************************************************/
template<typename Type>
inline HRESULT GetExtensionCaps(
    ID3D11Device* pd3dDevice,
    Type* pCaps )
{
    D3D11_BUFFER_DESC desc;
    ZeroMemory( &desc, sizeof(desc) );
    desc.ByteWidth = sizeof(Type);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = pCaps;
    initData.SysMemPitch = sizeof(Type);
    initData.SysMemSlicePitch = 0;

    ZeroMemory( pCaps, sizeof(Type) );
    memcpy( pCaps->Key, CAPS_EXTENSION_KEY,
        sizeof(pCaps->Key) );
    pCaps->ApplicationVersion = EXTENSION_INTERFACE_VERSION;

    ID3D11Buffer* pBuffer = NULL;
    HRESULT result = pd3dDevice->CreateBuffer( 
        &desc,
        &initData,
        &pBuffer );

    if( pBuffer )
        pBuffer->Release();

    return result;
};
#endif

/*****************************************************************************\
CONST: RESOURCE_EXTENSION_KEY
PURPOSE: KEY to pass to UMD
\*****************************************************************************/
const char RESOURCE_EXTENSION_KEY[16] = {
    'I','N','T','C',
    'E','X','T','N',
    'R','E','S','O',
    'U','R','C','E' };

/*****************************************************************************\
STRUCT: RESOURCE_EXTENSION_TYPE_1_0
PURPOSE: Enumeration of supported resource extensions
\*****************************************************************************/
struct RESOURCE_EXTENSION_TYPE_1_0
{
    static const UINT RESOURCE_EXTENSION_RESERVED       = 0;
    static const UINT RESOURCE_EXTENSION_DIRECT_ACCESS  = 1;
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef RESOURCE_EXTENSION_TYPE_1_0 RESOURCE_EXTENSION_TYPE;
#endif

/*****************************************************************************\
STRUCT: RESOURCE_EXTENSION_1_0
PURPOSE: Resource extension interface structure
\*****************************************************************************/
struct RESOURCE_EXTENSION_1_0 : EXTENSION_BASE
{
    // Input:

    // Enumeration of the extension
    UINT    Type;       // RESOURCE_EXTENSION_TYPE

    // Extension data
    union
    {
        UINT    Data[16];
        UINT64  Data64[8];
    };
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef RESOURCE_EXTENSION_1_0  RESOURCE_EXTENSION;
#endif

/*****************************************************************************\
STRUCT: RESOURCE_EXTENSION_DIRECT_ACCESS
PURPOSE: Collection of types Used for The Direct Resource Access Extension
\*****************************************************************************/
struct RESOURCE_EXTENSION_DIRECT_ACCESS
{
    /*****************************************************************************\
    ENUM: CREATION_FLAGS
    PURPOSE: Enumeration for direct access extension controls
    \*****************************************************************************/
    enum CREATION_FLAGS
    {
        CREATION_FLAGS_DEFAULT_ALLOCATION = 0x0,
        CREATION_FLAGS_LINEAR_ALLOCATION = 0x1,
    };

    /*****************************************************************************\
    ENUM: MAP_TILE_TYPE
    PURPOSE: Enumeration for direct access tile type
    \*****************************************************************************/
    enum MAP_TILE_TYPE
    {
       MAP_TILE_TYPE_TILE_X = 0x0,
       MAP_TILE_TYPE_TILE_Y = 0x1, 
       MAP_TILE_TYPE_RESERVED_0 = 0x2,
       MAP_TILE_TYPE_LINEAR = 0x3, 
       MAP_TILE_TYPE_TILE_X_NO_CSX_SWIZZLE = 0x4,
       MAP_TILE_TYPE_TILE_Y_NO_CSX_SWIZZLE = 0x5, 
    };

    /*****************************************************************************\
    STRUCT: MAP_DATA
    PURPOSE: Direct Access Resource extension Map structure
    \*****************************************************************************/
    typedef struct MAP_DATA
    {
        void*   pBaseAddress;
        UINT    XOffset;
        UINT    YOffset;

        UINT     TileFormat;
        DWORD    Pitch;
        DWORD    Size;   
    };
};

#ifndef D3D10_UMD
/*****************************************************************************\
FUNCTION: SetResourceExtension
PURPOSE: Generic resource extension interface (intended for internal use)
\*****************************************************************************/
template<typename Type>
inline HRESULT SetResourceExtension(
    ID3D11Device* pd3dDevice,
    const Type* pExtnDesc )
{
    D3D11_BUFFER_DESC desc;
    ZeroMemory( &desc, sizeof(desc) );
    desc.ByteWidth = sizeof(Type);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory( &initData, sizeof(initData) );
    initData.pSysMem = pExtnDesc;
    initData.SysMemPitch = sizeof(Type);
    initData.SysMemSlicePitch = 0;

    ID3D11Buffer* pBuffer = NULL;
    HRESULT result = pd3dDevice->CreateBuffer( 
        &desc,
        &initData,
        &pBuffer );

    if( pBuffer )
        pBuffer->Release();

    return result;
}

/*****************************************************************************\

FUNCTION:
    SetDirectAccessResourceExtension

PURPOSE:
    Direct Access Resource extension interface

INPUT:
    ID3D11Device* pd3dDevice - the D3D11 device
    const UINT flags -

        RESOURCE_EXTENSION_DIRECT_ACCESS_DEFAULT_ALLOCATION: resource will
            be allocated using driver preferences, e.g. 2D resources may be
            allocated using swizzled memory

        RESOURCE_EXTENSION_DIRECT_ACCESS_LINEAR_ALLOCATION: forces resource to
            be allocated as linear

OUTPUT:
    HRESULT - S_OK if driver supported extension

\*****************************************************************************/
inline HRESULT SetDirectAccessResourceExtension(
    ID3D11Device* pd3dDevice,
    const UINT flags )
{
    RESOURCE_EXTENSION_1_0 extnDesc;
    ZeroMemory( &extnDesc, sizeof(extnDesc) );
    memcpy( &extnDesc.Key[0], RESOURCE_EXTENSION_KEY,
        sizeof(extnDesc.Key) );
    extnDesc.ApplicationVersion = EXTENSION_INTERFACE_VERSION;
    extnDesc.Type = RESOURCE_EXTENSION_TYPE_1_0::RESOURCE_EXTENSION_DIRECT_ACCESS;
    extnDesc.Data[0] = flags;

    return SetResourceExtension( pd3dDevice, &extnDesc );
}
#endif

/*****************************************************************************\
CONST: STATE_EXTENSION_KEY
PURPOSE: KEY to pass to UMD
\*****************************************************************************/
const char STATE_EXTENSION_KEY[16] = {
    'I','N','T','C',
    'E','X','T','N',
    'S','T','A','T',
    'E','O','B','J' };

/*****************************************************************************\
STRUCT: STATE_EXTENSION_TYPE_1_0
PURPOSE: Enumeration of supported resource extensions
\*****************************************************************************/
struct STATE_EXTENSION_TYPE_1_0
{
    static const UINT STATE_EXTENSION_RESERVED = 0;
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef STATE_EXTENSION_TYPE_1_0    STATE_EXTENSION_TYPE;
#endif

/*****************************************************************************\
STRUCT: STATE_EXTENSION_1_0
PURPOSE: UMD extension interface structure
\*****************************************************************************/
struct STATE_EXTENSION_1_0 : EXTENSION_BASE
{
    // Input:

    // Enumeration of the extension
    UINT    Type;       // STATE_EXTENSION_TYPE

    // Extension data
    union
    {
        UINT    Data[16];
        UINT64  Data64[8];
    };
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef STATE_EXTENSION_1_0 STATE_EXTENSION;
#endif

#ifndef D3D10_UMD
/*****************************************************************************\
FUNCTION: SetStateExtension
PURPOSE: Generic State extension interface (intended for internal use)
\*****************************************************************************/
template<typename Type>
inline HRESULT SetStateExtension(
    ID3D11Device* pd3dDevice,
    const Type* pExtnDesc )
{
    D3D11_BUFFER_DESC desc;
    ZeroMemory( &desc, sizeof(desc) );
    desc.ByteWidth = sizeof(Type);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory( &initData, sizeof(initData) );
    initData.pSysMem = pExtnDesc;
    initData.SysMemPitch = sizeof(Type);
    initData.SysMemSlicePitch = 0;

    ID3D11Buffer* pBuffer = NULL;
    HRESULT result = pd3dDevice->CreateBuffer( 
        &desc,
        &initData,
        &pBuffer );

    if( pBuffer )
        pBuffer->Release();

    return result;
}
#endif

/*****************************************************************************\
STRUCT: SHADER_EXTENSION_TYPE_1_0
PURPOSE: Enumeration of supported resource extensions
\*****************************************************************************/
struct SHADER_EXTENSION_TYPE_1_0
{
    static const UINT SHADER_EXTENSION_RESERVED             = 0;
    static const UINT SHADER_EXTENSION_UAV_SERIALIZATION    = 1;
};

#if EXTENSION_INTERFACE_VERSION == EXTENSION_INTERFACE_VERSION_1_0
typedef SHADER_EXTENSION_TYPE_1_0   SHADER_EXTENSION_TYPE;
#endif

} // namespace INTC
