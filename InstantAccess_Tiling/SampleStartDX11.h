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
#ifndef __CPUT_SAMPLESTARTDX11_H__
#define __CPUT_SAMPLESTARTDX11_H__

#include <stdio.h>
#include "CPUT_DX11.h"
#include <D3D11_3.h>
#include <DirectXMath.h>
#include <time.h>
#include "CPUTSprite.h"
#include "CPUTTextureDX11.h"
#ifdef USE_SSAO
#include "..\SSAO\SSAOTechnique.h"
#endif

#include "InstantAccess_Tiling.h"
#define MODE_DX 3


// define some controls
const CPUTControlID ID_MAIN_PANEL = 10;
const CPUTControlID ID_SECONDARY_PANEL = 20;
const CPUTControlID ID_FULLSCREEN_BUTTON = 100;
const CPUTControlID ID_TEST_CONTROL = 1000;
const CPUTControlID ID_IGNORE_CONTROL_ID = -1;

//-----------------------------------------------------------------------------
class MySample : public CPUT_DX11
{
private:
    float                  mfElapsedTime;
    CPUTAssetSet          *mpAssetSet;
    CPUTCameraControllerFPS  *mpCameraController;
    CPUTSprite            *mpDebugSprite;

    CPUTAssetSet          *mpShadowCameraSet;
    CPUTRenderTargetDepth *mpShadowRenderTarget;

    CPUTText              *mpFPSCounter;
    CPUTText       *mpText;
    D3D11_MAPPED_SUBRESOURCE mTestData;
    CPUTTextureDX11 *mpTestTexture;
	CPUTTextureDX11 *mpDestTexture;

#ifdef USE_SSAO
    SSAOTechnique          mSSAO;
#endif
    ID3D11Texture2D *mpDRATextureCPU;
    TextureInfo testTextureInfo;
    bool mHasDRA;
    UINT mMode;
    UINT mTest;
public:
    MySample() : 
        mpAssetSet(NULL),
        mpCameraController(NULL),
        mpDebugSprite(NULL),
        mpShadowCameraSet(NULL),
        mpShadowRenderTarget(NULL),
        mHasDRA(false),
        mpDRATextureCPU(NULL),
        mpTestTexture(NULL),
		mpDestTexture(NULL),
        mMode(MODE_TILED),
		mTest(TEST_SOLID)
    {}
    virtual ~MySample()
    {
        // Note: these two are defined in the base.  We release them because we addref them.
        SAFE_RELEASE(mpCamera);
        SAFE_RELEASE(mpShadowCamera);
        SAFE_RELEASE(mpDRATextureCPU);
        SAFE_RELEASE(mpAssetSet);
        SAFE_DELETE( mpCameraController );
        SAFE_DELETE( mpDebugSprite);
        SAFE_RELEASE(mpShadowCameraSet);
        SAFE_DELETE( mpShadowRenderTarget );
        SAFE_RELEASE(mpTestTexture);
		SAFE_RELEASE(mpDestTexture);
    }
	bool Initialized() { return mHasDRA; };

    virtual CPUTEventHandledCode HandleKeyboardEvent(CPUTKey key);
    virtual CPUTEventHandledCode HandleMouseEvent(int x, int y, int wheel, CPUTMouseState state);
    virtual void                 HandleCallbackEvent( CPUTEventID Event, CPUTControlID ControlID, CPUTControl *pControl );

    virtual void Create();
    virtual void Render(double deltaSeconds);
    virtual void Update(double deltaSeconds);
    virtual void ResizeWindow(UINT width, UINT height);
};
#endif // __CPUT_SAMPLESTARTDX11_H__
