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
#include "SampleStartDX11.h"
#include "CPUTRenderTarget.h"
#include "IGFXExtensionsHelper.h"

const UINT SHADOW_WIDTH_HEIGHT = 2048;

#define ID_TEST_SOLID 2001
#define ID_TEST_COPY 2002
#define ID_TEST_READ 2003
#define ID_TEST_SOLID_DROPDOWN 3001
#define ID_TEST_COPY_DROPDOWN 3002

// set file to open
cString g_OpenFilePath;
cString g_OpenShaderPath;
cString g_OpenFileName;

extern float3 gLightDir;
extern char *gpDefaultShaderSource;
double UpdateAverage(double newValue);

// Application entry point.  Execution begins here.
//-----------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Prevent unused parameter compiler warnings
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

#ifdef DEBUG
    // tell VS to report leaks at any exit of the program
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    CPUTResult result=CPUT_SUCCESS;
    int returnCode=0;

    // create an instance of my sample
    MySample *pSample = new MySample();

    // We make the assumption we are running from the executable's dir in
    // the CPUT SampleStart directory or it won't be able to use the relative paths to find the default
    // resources    
    cString ResourceDirectory;
    CPUTOSServices::GetOSServices()->GetExecutableDirectory(&ResourceDirectory);
    ResourceDirectory.append(_L("..\\..\\..\\..\\CPUT\\resources\\"));

    // Initialize the system and give it the base CPUT resource directory (location of GUI images/etc)
    // For now, we assume it's a relative directory from the executable directory.  Might make that resource
    // directory location an env variable/hardcoded later
    pSample->CPUTInitialize(ResourceDirectory); 

    // window and device parameters
    CPUTWindowCreationParams params;
    params.deviceParams.refreshRate         = 0;
    params.deviceParams.swapChainBufferCount= 1;
    params.deviceParams.swapChainFormat     = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    params.deviceParams.swapChainUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;

    // parse out the parameter settings or reset them to defaults if not specified
    cString AssetFilename;
    cString CommandLine(lpCmdLine);
    pSample->CPUTParseCommandLine(CommandLine, &params, &AssetFilename);       

    // parse out the filename of the .set file to open (if one was sgiven)
    if(AssetFilename.size())
    {
        // strip off the target .set file, and parse it's full path
        cString PathAndFilename, Drive, Dir, Ext;  
        g_OpenFilePath.clear();

        // resolve full path, and check to see if the file actually exists
        CPUTOSServices::GetOSServices()->ResolveAbsolutePathAndFilename(AssetFilename, &PathAndFilename);
        result = CPUTOSServices::GetOSServices()->DoesFileExist(PathAndFilename);
        if(CPUTFAILED(result))
        {
            cString ErrorMessage = _L("File specified in the command line does not exist: \n")+PathAndFilename;
#ifndef DEBUG
            DEBUGMESSAGEBOX(_L("Error loading command line file"), ErrorMessage);
#else
            ASSERT(false, ErrorMessage);
#endif
        }

        // now parse through the path and removing the trailing \\Asset\\ directory specification
        CPUTOSServices::GetOSServices()->SplitPathAndFilename(PathAndFilename, &Drive, &Dir, &g_OpenFileName, &Ext);
        // strip off any trailing \\ 
        size_t index=Dir.size()-1;
        if(Dir[index]=='\\' || Dir[index]=='/')
        {
            index--;
        }

        // strip off \\Asset
        for(unsigned int  ii=index; ii>=0; ii--)
        {
            if(Dir[ii]=='\\' || Dir[ii]=='/')
            {
                Dir = Dir.substr(0, ii+1); 
                g_OpenFilePath = Drive+Dir; 
                index=ii;
                break;
            }
        }        

        // strip off \\<setname> 
        for(unsigned int ii=index; ii>=0; ii--)
        {
            if(Dir[ii]=='\\' || Dir[ii]=='/')
            {
                Dir = Dir.substr(0, ii+1); 
                g_OpenShaderPath = Drive + Dir + _L("\\Shader\\");
                break;
            }
        }
    }

    // create the window and device context
    result = pSample->CPUTCreateWindowAndContext(_L("CPUTWindow DirectX 11"), params);
    ASSERT( CPUTSUCCESS(result), _L("CPUT Error creating window and context.") );

    bool initialized = pSample->Initialized();
    if(!initialized)
    {
        CPUTOSServices::GetOSServices()->OpenMessageBox(_L("No Extension Support"), _L("Could not initialize extensions. Exiting."));
    }
    else
    {
        // start the main message loop
        returnCode = pSample->CPUTMessageLoop();
    }
    pSample->DeviceShutdown();

	// cleanup resources
    SAFE_DELETE(pSample);

    return returnCode;
}

// Handle OnCreation events
//-----------------------------------------------------------------------------
void MySample::Create()
{
    CPUTRenderParametersDX renderParams(mpContext);

    int width, height;
    CPUTOSServices::GetOSServices()->GetClientDimensions(&width, &height);
    ResizeWindow(width, height);

    CPUTRenderStateBlockDX11 *pBlock = new CPUTRenderStateBlockDX11();
    CPUTRenderStateDX11 *pStates = pBlock->GetState();

    CPUTAssetLibrary *pAssetLibrary = CPUTAssetLibrary::GetAssetLibrary();

    cString ExecutableDirectory;
    CPUTOSServices::GetOSServices()->GetExecutableDirectory(&ExecutableDirectory);

    pAssetLibrary->SetMediaDirectoryName(    _L("..\\..\\..\\Media\\"));

    //Initialize the extensions here
    ID3D11Device *pDevice = CPUT_DX11::GetDevice();
    IGFX::Init(pDevice);

    mHasDRA = IGFX::getAvailableExtensions(pDevice).DirectResourceAccess;

	// If the extionsion are available, create the approriate textures for tests here
    if(mHasDRA)
    {
        // Test texture has an image to ensure that copies work correctly.
        // instant access is implemented with two textures. A normal texture is 
        // created and an additional staging texture is created to allow mapping
        // of the texture. When using the extension, the staging texture does not
        // allocate additional memory, but points the memory allocated by the first
        // texture.
        mpTestTexture = (CPUTTextureDX11*) pAssetLibrary->GetTexture(_L("TestTexture.dds"));

        D3D11_TEXTURE2D_DESC cpudesc, gpudesc, testdesc;
        mpTestTexture->GetDesc(&testdesc);

		//ID3D11Device3* pDevice3 =  NULL;
		//mpD3dDevice->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&pDevice3));
		//D3D11_FEATURE_DATA_D3D11_OPTIONS2 features;
		//HRESULT hr = pDevice3->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &features, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS2));
		//D3D11_TEXTURE2D_DESC1 swDesc;
		//swDesc.ArraySize = 1;
		//swDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		//swDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		//swDesc.Format = testdesc.Format;
		//swDesc.Height = testdesc.Height;
		//swDesc.Width = testdesc.Width;
		//swDesc.MipLevels = testdesc.MipLevels;
		//swDesc.MiscFlags = 0;
		//swDesc.SampleDesc = testdesc.SampleDesc;
		//swDesc.TextureLayout = D3D11_TEXTURE_LAYOUT_64K_STANDARD_SWIZZLE;
		//swDesc.Usage = D3D11_USAGE_DEFAULT;

		//ID3D11Texture2D1* pSWTexture = NULL;
		//pDevice3->CreateTexture2D1(&swDesc, NULL, &pSWTexture);

		//ID3D11ShaderResourceView *pSWTextureSRV;
		//D3D11_SHADER_RESOURCE_VIEW_DESC swSRVdesc; 
		//swSRVdesc.Texture2D.MipLevels = swDesc.MipLevels;
		//swSRVdesc.Format = swDesc.Format;
		//swSRVdesc.Texture2D.MostDetailedMip = 0;
		//swSRVdesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		//pDevice->CreateShaderResourceView(pSWTexture, &swSRVdesc, &pSWTextureSRV);

		ID3D11Texture2D *pDRATextureGPU = NULL;
        cpudesc.ArraySize = gpudesc.ArraySize = 1;
        cpudesc.BindFlags = 0;
        gpudesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cpudesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        gpudesc.CPUAccessFlags = 0;
        cpudesc.Format = gpudesc.Format = testdesc.Format;//DXGI_FORMAT_R8G8B8A8_UNORM;
        cpudesc.Height = gpudesc.Height = testdesc.Height;
        cpudesc.Width = gpudesc.Width = testdesc.Width;
        cpudesc.MipLevels = gpudesc.MipLevels = 1;//testdesc.MipLevels;
        cpudesc.MiscFlags = gpudesc.MiscFlags = 0;
        cpudesc.Usage = D3D11_USAGE_STAGING;
        gpudesc.Usage = D3D11_USAGE_DEFAULT;
        cpudesc.SampleDesc.Count = gpudesc.SampleDesc.Count = testdesc.SampleDesc.Count;
        cpudesc.SampleDesc.Quality = gpudesc.SampleDesc.Quality = testdesc.SampleDesc.Quality;

        testTextureInfo.heightInBlocks = gpudesc.Height;
        testTextureInfo.widthInBlocks = gpudesc.Width;
        testTextureInfo.mips = gpudesc.MipLevels;
        testTextureInfo.bytesPerBlock = 4;
        int bytes=0;
        for (unsigned int i =0; i < testTextureInfo.mips; ++i)
        {
            int mipWidth = (gpudesc.Width >> i)==0 ? 1 : (gpudesc.Width >> i);
            int mipHeight = (gpudesc.Height >> i)==0 ? 1 : (gpudesc.Height>> i);
            bytes +=mipWidth * mipHeight * testTextureInfo.bytesPerBlock;
        }
        testTextureInfo.allocateBytes = bytes;
        testTextureInfo.dxgiFormat = testdesc.Format;
        IGFX::CreateSharedTexture2D(pDevice, &cpudesc, &mpDRATextureCPU, &gpudesc, &pDRATextureGPU, NULL);
        CPUT_DX11::GetContext()->CopyResource(mpDRATextureCPU, pDRATextureGPU);

        ID3D11ShaderResourceView *pDRATextureSRV;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc;
        srvdesc.Texture2D.MipLevels = gpudesc.MipLevels;
        srvdesc.Format = gpudesc.Format;
        srvdesc.Texture2D.MostDetailedMip = 0;
        srvdesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        pDevice->CreateShaderResourceView(pDRATextureGPU, &srvdesc, &pDRATextureSRV);

        CPUTTextureDX11 *pDRATextureGPUCPUT = new CPUTTextureDX11(std::wstring(_L("$DRATextureGPU")), pDRATextureGPU, pDRATextureSRV);
        CPUTTextureDX11 *pDRATextureCPUT = new CPUTTextureDX11(std::wstring(_L("$DRATextureCPU")), mpDRATextureCPU, NULL);
        pAssetLibrary->AddTexture(pDRATextureGPUCPUT->Name(), pDRATextureGPUCPUT);
        pAssetLibrary->AddTexture(pDRATextureCPUT->Name(), pDRATextureCPUT);
        SAFE_RELEASE(pDRATextureGPU);
        SAFE_RELEASE(pDRATextureSRV);
        SAFE_RELEASE(pDRATextureGPUCPUT);
        SAFE_RELEASE(pDRATextureCPUT);

        ID3D11Texture2D* pDXDestTexture;
        pDevice->CreateTexture2D(&cpudesc, NULL, &pDXDestTexture);
        mpDestTexture = new CPUTTextureDX11(std::wstring(_L("DXDestTexture")), pDXDestTexture, NULL);
        SAFE_RELEASE(pDXDestTexture);

        pAssetLibrary->SetMediaDirectoryName(    _L("..\\..\\..\\Media\\"));

        CPUTGuiControllerDX11 *pGUI = CPUTGetGuiController();

        //
        // Create some controls
        //    
        CPUTButton     *pButton = NULL;
        CPUTCheckbox   *pCheckbox = NULL;
        CPUTDropdown   *pDropdown = NULL;
        // create some controls
        pGUI->CreateButton(_L("Fullscreen"), ID_FULLSCREEN_BUTTON, ID_MAIN_PANEL, &pButton);

        pGUI->CreateCheckbox(_L("Write Solid Color"), ID_TEST_SOLID, ID_MAIN_PANEL, &pCheckbox);
        pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
        pGUI->CreateDropdown(_L("Tiled"), ID_TEST_SOLID_DROPDOWN, ID_MAIN_PANEL, &pDropdown);
        pDropdown->AddSelectionItem(_L("Linear Row"), false);
        pDropdown->AddSelectionItem(_L("Linear Column"), false);
        pDropdown->AddSelectionItem(_L("Standard DX"), false);

        pGUI->CreateCheckbox(_L("Copy Texture"), ID_TEST_COPY, ID_MAIN_PANEL, &pCheckbox);
        pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
        pGUI->CreateDropdown(_L("Tiled"), ID_TEST_COPY_DROPDOWN, ID_MAIN_PANEL, &pDropdown);
        pDropdown->AddSelectionItem(_L("Linear Row"), false);
        pDropdown->AddSelectionItem(_L("Linear Column"), false);
        pDropdown->AddSelectionItem(_L("Linear Optimized"), true);
        pDropdown->SetVisibility(false);

        pGUI->CreateCheckbox(_L("READ"), ID_TEST_READ, ID_MAIN_PANEL, &pCheckbox);
        pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);

        mTest = TEST_COPY;
        mMode = MODE_LINEAR_INTRINSICS;

        pGUI->CreateText( _L("Avg test time"), ID_TEST_CONTROL+3, ID_MAIN_PANEL, &mpText);

        // Create Static text
        //
        pGUI->CreateText( _L("F1 for Help"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);
        pGUI->CreateText( _L("[Escape] to quit application"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);
        pGUI->CreateText( _L("A,S,D,F - move camera position"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);
        pGUI->CreateText( _L("Q - camera position up"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);
        pGUI->CreateText( _L("E - camera position down"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);
        pGUI->CreateText( _L("mouse + right click - camera look location"), ID_IGNORE_CONTROL_ID, ID_SECONDARY_PANEL);

        pGUI->SetActivePanel(ID_MAIN_PANEL);
        pGUI->DrawFPS(true);

        pAssetLibrary->SetMediaDirectoryName(  ExecutableDirectory+_L("..\\..\\..\\Media\\"));
        mpDebugSprite = new CPUTSprite();
        mpDebugSprite->CreateSprite( -1.0f, -1.0f, 2.0f, 2.0f, _L("Sprite") );
    }

    // The remainder of this method initializes various CPUT related objects.

    // Add our programatic (and global) material parameters
    CPUTMaterial::mGlobalProperties.AddValue( _L("cbPerFrameValues"), _L("$cbPerFrameValues") );
    CPUTMaterial::mGlobalProperties.AddValue( _L("cbPerModelValues"), _L("$cbPerModelValues") );
    CPUTMaterial::mGlobalProperties.AddValue( _L("_Shadow"), _L("$shadow_depth") );

    // Create default shaders
    CPUTPixelShaderDX11  *pPS       = CPUTPixelShaderDX11::CreatePixelShaderFromMemory(            _L("$DefaultShader"), CPUT_DX11::mpD3dDevice,          _L("PSMain"), _L("ps_4_0"), gpDefaultShaderSource );
    CPUTPixelShaderDX11  *pPSNoTex  = CPUTPixelShaderDX11::CreatePixelShaderFromMemory(   _L("$DefaultShaderNoTexture"), CPUT_DX11::mpD3dDevice, _L("PSMainNoTexture"), _L("ps_4_0"), gpDefaultShaderSource );
    CPUTVertexShaderDX11 *pVS       = CPUTVertexShaderDX11::CreateVertexShaderFromMemory(          _L("$DefaultShader"), CPUT_DX11::mpD3dDevice,          _L("VSMain"), _L("vs_4_0"), gpDefaultShaderSource );
    CPUTVertexShaderDX11 *pVSNoTex  = CPUTVertexShaderDX11::CreateVertexShaderFromMemory( _L("$DefaultShaderNoTexture"), CPUT_DX11::mpD3dDevice, _L("VSMainNoTexture"), _L("vs_4_0"), gpDefaultShaderSource );

    // We just want to create them, which adds them to the library.  We don't need them any more so release them, leaving refCount at 1 (only library owns a ref)
    SAFE_RELEASE(pPS);
    SAFE_RELEASE(pPSNoTex);
    SAFE_RELEASE(pVS);
    SAFE_RELEASE(pVSNoTex);

    gLightDir.normalize();

    // load shadow casting material+sprite object
    pAssetLibrary->SetMediaDirectoryName(  ExecutableDirectory+_L("..\\..\\..\\Media\\"));
    mpShadowRenderTarget = new CPUTRenderTargetDepth();
    mpShadowRenderTarget->CreateRenderTarget( cString(_L("$shadow_depth")), SHADOW_WIDTH_HEIGHT, SHADOW_WIDTH_HEIGHT, DXGI_FORMAT_D32_FLOAT );

    // Override default sampler desc for our default shadowing sampler
    pStates->SamplerDesc[1].Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    pStates->SamplerDesc[1].AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
    pStates->SamplerDesc[1].AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
    pStates->SamplerDesc[1].ComparisonFunc = D3D11_COMPARISON_GREATER;
    pBlock->CreateNativeResources();
    CPUTAssetLibrary::GetAssetLibrary()->AddRenderStateBlock( _L("$DefaultRenderStates"), pBlock );
    pBlock->Release(); // We're done with it.  The library owns it now.
    //
    // Load .set file that was specified on the command line
    // Otherwise, load the default object if no .set was specified
    if(g_OpenFilePath.empty())
    {
        g_OpenFilePath = _L("..\\..\\..\\Media\\Teapot\\");
        g_OpenFileName = _L("Teapot");
    }

    // Use "find" instead of "get" for asset sets as we are OK with them not existing.
    pAssetLibrary->SetMediaDirectoryName( g_OpenFilePath );
    mpAssetSet        = pAssetLibrary->GetAssetSet( g_OpenFileName );

    // If no cameras were created from the model sets then create a default simple camera
    //
    if( mpAssetSet && mpAssetSet->GetCameraCount() )
    {
        mpCamera = mpAssetSet->GetFirstCamera();
        mpCamera->AddRef(); 
    } else
    {
        mpCamera = new CPUTCamera();
        CPUTAssetLibraryDX11::GetAssetLibrary()->AddCamera( _L("SampleStart Camera"), mpCamera );

        mpCamera->SetPosition( 0.0f, 0.0f, 5.0f );
        // Set the projection matrix for all of the cameras to match our window.
        mpCamera->SetAspectRatio(((float)width)/((float)height));
    }
    mpCamera->SetFov(DirectX::XMConvertToRadians(60.0f)); // TODO: Fix converter's FOV bug (Maya generates cameras for which fbx reports garbage for fov)
    mpCamera->SetFarPlaneDistance(100000.0f);
    mpCamera->Update();

    // Set up the shadow camera (a camera that sees what the light sees)
    float3 lookAtPoint(0.0f, 0.0f, 0.0f);
    float3 half(1.0f, 1.0f, 1.0f);
    if( mpAssetSet )
    {
        mpAssetSet->GetBoundingBox( &lookAtPoint, &half );
    }
    float length = half.length();

    mpShadowCamera = new CPUTCamera();
    mpShadowCamera->SetFov(DirectX::XMConvertToRadians(45));
    mpShadowCamera->SetAspectRatio(1.0f);
    float fov = mpShadowCamera->GetFov();
    float tanHalfFov = tanf(fov * 0.5f);
    float cameraDistance = length/tanHalfFov;
    float nearDistance = cameraDistance * 0.1f;
    mpShadowCamera->SetNearPlaneDistance(nearDistance);
    mpShadowCamera->SetFarPlaneDistance(2.0f * cameraDistance);
    CPUTAssetLibraryDX11::GetAssetLibrary()->AddCamera( _L("ShadowCamera"), mpShadowCamera );
    float3 shadowCameraPosition = lookAtPoint - gLightDir * cameraDistance;
    mpShadowCamera->SetPosition( shadowCameraPosition );
    mpShadowCamera->LookAt( lookAtPoint.x, lookAtPoint.y, lookAtPoint.z );
    mpShadowCamera->SetWidth(length*2);
    mpShadowCamera->SetHeight(length*2);
    mpShadowCamera->SetNearPlaneDistance(cameraDistance - length);
    mpShadowCamera->SetFarPlaneDistance(cameraDistance + length);
    mpShadowCamera->SetPerspectiveProjection(false);
    mpShadowCamera->Update();
    mpCameraController = new CPUTCameraControllerFPS();
    mpCameraController->SetCamera(mpCamera);
    mpCameraController->SetLookSpeed(0.004f);
    mpCameraController->SetMoveSpeed(20.0f);
}

//-----------------------------------------------------------------------------
void MySample::Update(double deltaSeconds)
{

}

// Handle keyboard events
//-----------------------------------------------------------------------------
CPUTEventHandledCode MySample::HandleKeyboardEvent(CPUTKey key)
{
    static bool panelToggle = false;
    CPUTEventHandledCode    handled = CPUT_EVENT_UNHANDLED;
    cString fileName;
    CPUTGuiControllerDX11*  pGUI = CPUTGetGuiController();
    switch(key)
    {
    case KEY_F1:
        panelToggle = !panelToggle;
        if(panelToggle)
        {
            pGUI->SetActivePanel(ID_SECONDARY_PANEL);
        }
        else
        {
            pGUI->SetActivePanel(ID_MAIN_PANEL);
        }
        handled = CPUT_EVENT_HANDLED;
        break;
    case KEY_L:
        {
            static int cameraObjectIndex = 0;
            CPUTRenderNode *pCameraList[] = { mpCamera, mpShadowCamera };
            cameraObjectIndex = (++cameraObjectIndex) % (sizeof(pCameraList)/sizeof(*pCameraList));
            CPUTRenderNode *pCamera = pCameraList[cameraObjectIndex];
            mpCameraController->SetCamera( pCamera );
        }
        handled = CPUT_EVENT_HANDLED;
        break;
    case KEY_ESCAPE:
        handled = CPUT_EVENT_HANDLED;
        ShutdownAndDestroy();
        break;
    case KEY_1:
    case KEY_2:
    case KEY_3: 
    case KEY_4:		   
        {
            mMode = key - KEY_1; // key 1 maps to mode 0 (tiled), key 2 to 1 (rows) ...  
            CPUTDropdown *pDropdown = NULL;
            CPUTCheckbox *pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_SOLID);
            if(pCheckbox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED)
            {
                pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_SOLID_DROPDOWN);
            }
            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_COPY);
            if(pCheckbox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED)
            {
                pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_COPY_DROPDOWN);
            }
            if(pDropdown != NULL)
            {
                pDropdown->SetSelectedItem(mMode);
            }
        }
        break;
    case KEY_S:
        {
            UINT controlID = ID_TEST_SOLID;
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(controlID);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            HandleCallbackEvent((CPUTEventID)0, controlID, pCheckbox);
        }
        break;
    case KEY_C:
        {
            UINT controlID = ID_TEST_COPY;
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(controlID);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            HandleCallbackEvent((CPUTEventID)0, controlID, pCheckbox);
        }
        break;
    case KEY_R:
        {
            UINT controlID = ID_TEST_READ;
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(controlID);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            HandleCallbackEvent((CPUTEventID)0, controlID, pCheckbox);
        }
        break;
    }
    // pass it to the camera controller
    if(handled == CPUT_EVENT_UNHANDLED)
    {
        handled = mpCameraController->HandleKeyboardEvent(key);
    }

    return handled;
}


// Handle mouse events
//-----------------------------------------------------------------------------
CPUTEventHandledCode MySample::HandleMouseEvent(int x, int y, int wheel, CPUTMouseState state)
{
    if( mpCameraController )
    {
        return mpCameraController->HandleMouseEvent(x, y, wheel, state);
    }
    return CPUT_EVENT_UNHANDLED;
}

// Handle any control callback events
//-----------------------------------------------------------------------------
void MySample::HandleCallbackEvent( CPUTEventID Event, CPUTControlID ControlID, CPUTControl *pControl )
{
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(pControl);
    cString SelectedItem;

    CPUTGuiControllerDX11 *pGUI = CPUTGetGuiController();
    switch(ControlID)
    {
    case ID_TEST_COPY:
        {	
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_COPY);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            mTest = TEST_COPY;
            CPUTDropdown *pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_COPY_DROPDOWN);
            pDropdown->SetVisibility(true);

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_SOLID);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
            pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_SOLID_DROPDOWN);
            pDropdown->SetVisibility(false);

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_READ);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
        }
        break;
    case ID_TEST_SOLID:
        {	
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_SOLID);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            mTest = TEST_SOLID;
            CPUTDropdown *pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_SOLID_DROPDOWN);
            pDropdown->SetVisibility(true);

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_COPY);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
            pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_COPY_DROPDOWN);
            pDropdown->SetVisibility(false);

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_READ);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
        }
        break;
    case ID_TEST_READ:								 
        {
            CPUTCheckbox* pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_READ);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_CHECKED);
            mTest = TEST_READ;
            CPUTDropdown *pDropdown = NULL;

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_SOLID);				   
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
            pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_SOLID_DROPDOWN);
            pDropdown->SetVisibility(false);

            pCheckbox = (CPUTCheckbox*)pGUI->GetControl(ID_TEST_COPY);
            pCheckbox->SetCheckboxState(CPUT_CHECKBOX_UNCHECKED);
            pDropdown = (CPUTDropdown*)pGUI->GetControl(ID_TEST_COPY_DROPDOWN);
            pDropdown->SetVisibility(false);
            mMode = MODE_TILED;
        }
        break;
    case ID_TEST_COPY_DROPDOWN:
    case ID_TEST_SOLID_DROPDOWN:
        {
            CPUTDropdown* pDropdown = (CPUTDropdown*)pGUI->GetControl(ControlID);
            UINT index;
            pDropdown->GetSelectedItem(index);
            mMode = index;
        }
        break;
    case ID_FULLSCREEN_BUTTON:
        CPUTToggleFullScreenMode();
        break;
    default:
        break;

    }
}

// Handle resize events
//-----------------------------------------------------------------------------
void MySample::ResizeWindow(UINT width, UINT height)
{
    CPUTAssetLibrary *pAssetLibrary = CPUTAssetLibrary::GetAssetLibrary();

    // Before we can resize the swap chain, we must release any references to it.
    // We could have a "AssetLibrary::ReleaseSwapChainResources(), or similar.  But,
    // Generic "release all" works, is simpler to implement/maintain, and is not performance critical.
    pAssetLibrary->ReleaseTexturesAndBuffers();

    // Resize the CPUT-provided render target
    CPUT_DX11::ResizeWindow( width, height );

    // Resize any application-specific render targets here
    if( mpCamera ) mpCamera->SetAspectRatio(((float)width)/((float)height));

    pAssetLibrary->RebindTexturesAndBuffers();
}

static ID3D11UnorderedAccessView *gpNullUAVs[CPUT_MATERIAL_MAX_TEXTURE_SLOTS] = {0};
//-----------------------------------------------------------------------------
void MySample::Render(double deltaSeconds)
{
    CPUTRenderParametersDX renderParams(mpContext);

    // Clear back buffer
    const float clearColor[] = { 0.0993f, 0.0993f, 0.0993f, 1.0f };
    mpContext->ClearRenderTargetView( mpBackBufferRTV,  clearColor );
    mpContext->ClearDepthStencilView( mpDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

    static UINT frame = 0;

    D3D11_MAPPED_SUBRESOURCE mappedResource;

    if(mTest == TEST_COPY)
    {
        mTestData = mpTestTexture->MapTexture(renderParams, CPUT_MAP_READ);
    }
    if(mTest == TEST_READ)
    {
        mTestData = mpDestTexture->MapTexture(renderParams, CPUT_MAP_WRITE);
    }
    mpTimer->StartTimer();
    if(mTest == TEST_SOLID) // write a solid color to the texture
    {
        // dx mode is standard path of mapping the staging buffer and copying the result to the final
        // texture. utility code hides the details here. Note, the CopyResource time is not accounted for
        // here.
        if(mMode == MODE_DX) 
        {
            __declspec(align(16)) UINT color[] = {frame, frame, frame, frame};
            __m128i* src = (__m128i*)color;
            D3D11_MAPPED_SUBRESOURCE subResource = mpDestTexture->MapTexture(renderParams, CPUT_MAP_WRITE);
            __m128i* dest = (__m128i*)subResource.pData;
            for(UINT i = 0; i < testTextureInfo.heightInBlocks * testTextureInfo.widthInBlocks; i+=4)
            {
                _mm_stream_si128(dest++, *src);
            }
            mpDestTexture->UnmapTexture(renderParams);
        }
        else
        {
            //Write a solid color to the dra resource.
            mpContext->Map(mpDRATextureCPU, 0, D3D11_MAP_WRITE, NULL, &mappedResource);
            INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA *pdata = (INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA*)(mappedResource.pData);
            WriteDRA_Solid(mMode, pdata, &testTextureInfo, 0, frame);
            mpContext->Unmap(mpDRATextureCPU, 0);
        }
    }
    else if(mTest == TEST_COPY)
    {
        // Copies the texture data from the source texture to the dra texture. This test illustrates the
        // cost of doing the swizzle.
        mpContext->Map(mpDRATextureCPU, 0, D3D11_MAP_WRITE, NULL, &mappedResource);
        INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA *pdata = (INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA*)(mappedResource.pData);
        WriteDRA_Copy(mMode, pdata, &testTextureInfo, 0, mTestData);
        mpContext->Unmap(mpDRATextureCPU, 0);
    }
    else if(mTest == TEST_READ)
    {
        // Reads from the DRA texture. Note: DRA textures use write combined memory, so reading from this memory is slow.
        mpContext->Map(mpDRATextureCPU, 0, D3D11_MAP_READ, NULL, &mappedResource);
        INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA *pdata = (INTC::RESOURCE_EXTENSION_DIRECT_ACCESS::MAP_DATA*)(mappedResource.pData);
        ReadDRA(mMode, pdata, &testTextureInfo, 0, mTestData);
        mpContext->Unmap(mpDRATextureCPU, 0);
    }

    double time = mpTimer->StopTimer();

    if(mTest == TEST_COPY)
    {
        mpTestTexture->UnmapTexture(renderParams);
    }
    if(mTest == TEST_READ)
    {
        mpDestTexture->UnmapTexture(renderParams);
    }
    double avg = UpdateAverage(time);

    if(frame % 30 == 0)
        mpText->SetText(_L("avg Test time: ") + std::to_wstring((long double)(avg*1000)) + _L(" ms"));
    frame++;
    mpDebugSprite->DrawSprite(renderParams);

    CPUTDrawGUI();
}

double UpdateAverage(double newValue)
{
#define STORE_SIZE 64
    static UINT entries = 0;
    static double values[STORE_SIZE];
    static int head = 0;
    static double sum = 0;
    //The following code keeps a running average of the test time from the pervious STORE_SIZE frames.
    sum = sum - values[head];
    values[head] = newValue;
    sum = sum + newValue;
    head++;
    entries++;
    entries = min(entries, STORE_SIZE);
    head = head % STORE_SIZE;
    return sum/entries;
}
char *gpDefaultShaderSource =  "\n\
// ********************************************************************************************************\n\
struct VS_INPUT\n\
{\n\
float3 Pos      : POSITION; // Projected position\n\
float3 Norm     : NORMAL;\n\
float2 Uv       : TEXCOORD0;\n\
};\n\
struct PS_INPUT\n\
{\n\
float4 Pos      : SV_POSITION;\n\
float3 Norm     : NORMAL;\n\
float2 Uv       : TEXCOORD0;\n\
float4 LightUv  : TEXCOORD1;\n\
float3 Position : TEXCOORD2; // Object space position \n\
};\n\
// ********************************************************************************************************\n\
Texture2D    TEXTURE0 : register( t0 );\n\
SamplerState SAMPLER0 : register( s0 );\n\
Texture2D    _Shadow  : register( t1 );\n\
SamplerComparisonState SAMPLER1 : register( s1 );\n\
// ********************************************************************************************************\n\
cbuffer cbPerModelValues\n\
{\n\
row_major float4x4 World : WORLD;\n\
row_major float4x4 WorldViewProjection : WORLDVIEWPROJECTION;\n\
row_major float4x4 InverseWorld : INVERSEWORLD;\n\
float4   LightDirection;\n\
float4   EyePosition;\n\
row_major float4x4 LightWorldViewProjection;\n\
float4   BoundingBoxCenterWorldSpace;\n\
float4   BoundingBoxHalfWorldSpace;\n\
float4   BoundingBoxCenterObjectSpace;\n\
float4   BoundingBoxHalfObjectSpace;\n\
};\n\
// ********************************************************************************************************\n\
// TODO: Note: nothing sets these values yet\n\
cbuffer cbPerFrameValues\n\
{\n\
row_major float4x4 View;\n\
row_major float4x4 Projection;\n\
float3   AmbientColor;\n\
float3   LightColor;\n\
float3   TotalTimeInSeconds;\n\
row_major float4x4 InverseView;\n\
// row_major float4x4 ViewProjection;\n\
};\n\
// ********************************************************************************************************\n\
PS_INPUT VSMain( VS_INPUT input )\n\
{\n\
PS_INPUT output = (PS_INPUT)0;\n\
output.Pos      = mul( float4( input.Pos, 1.0f), WorldViewProjection );\n\
output.Position = mul( float4( input.Pos, 1.0f), World ).xyz;\n\
// TODO: transform the light into object space instead of the normal into world space\n\
output.Norm = mul( input.Norm, (float3x3)World );\n\
output.Uv   = float2(input.Uv.x, input.Uv.y);\n\
output.LightUv   = mul( float4( input.Pos, 1.0f), LightWorldViewProjection );\n\
return output;\n\
}\n\
// ********************************************************************************************************\n\
float4 PSMain( PS_INPUT input ) : SV_Target\n\
{\n\
float3  lightUv = input.LightUv.xyz / input.LightUv.w;\n\
lightUv.xy = lightUv.xy * 0.5f + 0.5f; // TODO: Move scale and offset to matrix.\n\
lightUv.y  = 1.0f - lightUv.y;\n\
float   shadowAmount = _Shadow.SampleCmp( SAMPLER1, lightUv, lightUv.z );\n\
float3 normal         = normalize(input.Norm);\n\
float  nDotL          = saturate( dot( normal, -LightDirection ) );\n\
float3 eyeDirection   = normalize(input.Position - InverseView._m30_m31_m32);\n\
float3 reflection     = reflect( eyeDirection, normal );\n\
float  rDotL          = saturate(dot( reflection, -LightDirection ));\n\
float3 specular       = pow(rDotL, 16.0f);\n\
specular              = min( shadowAmount, specular );\n\
float4 diffuseTexture = TEXTURE0.Sample( SAMPLER0, input.Uv );\n\
float ambient = 0.05;\n\
float3 result = (min(shadowAmount, nDotL)+ambient) * diffuseTexture + shadowAmount*specular;\n\
return float4( result, 1.0f );\n\
}\n\
\n\
// ********************************************************************************************************\n\
struct VS_INPUT_NO_TEX\n\
{\n\
float3 Pos      : POSITION; // Projected position\n\
float3 Norm     : NORMAL;\n\
};\n\
struct PS_INPUT_NO_TEX\n\
{\n\
float4 Pos      : SV_POSITION;\n\
float3 Norm     : NORMAL;\n\
float4 LightUv  : TEXCOORD1;\n\
float3 Position : TEXCOORD0; // Object space position \n\
};\n\
// ********************************************************************************************************\n\
PS_INPUT_NO_TEX VSMainNoTexture( VS_INPUT_NO_TEX input )\n\
{\n\
PS_INPUT_NO_TEX output = (PS_INPUT_NO_TEX)0;\n\
output.Pos      = mul( float4( input.Pos, 1.0f), WorldViewProjection );\n\
output.Position = mul( float4( input.Pos, 1.0f), World ).xyz;\n\
// TODO: transform the light into object space instead of the normal into world space\n\
output.Norm = mul( input.Norm, (float3x3)World );\n\
output.LightUv   = mul( float4( input.Pos, 1.0f), LightWorldViewProjection );\n\
return output;\n\
}\n\
// ********************************************************************************************************\n\
float4 PSMainNoTexture( PS_INPUT_NO_TEX input ) : SV_Target\n\
{\n\
float3 lightUv = input.LightUv.xyz / input.LightUv.w;\n\
float2 uv = lightUv.xy * 0.5f + 0.5f;\n\
float2 uvInvertY = float2(uv.x, 1.0f-uv.y);\n\
float shadowAmount = _Shadow.SampleCmp( SAMPLER1, uvInvertY, lightUv.z );\n\
float3 eyeDirection = normalize(input.Position - InverseView._m30_m31_m32);\n\
float3 normal       = normalize(input.Norm);\n\
float  nDotL = saturate( dot( normal, -normalize(LightDirection.xyz) ) );\n\
nDotL = shadowAmount * nDotL;\n\
float3 reflection   = reflect( eyeDirection, normal );\n\
float  rDotL        = saturate(dot( reflection, -LightDirection.xyz ));\n\
float  specular     = 0.2f * pow( rDotL, 4.0f );\n\
specular = min( shadowAmount, specular );\n\
return float4( (nDotL + specular).xxx, 1.0f);\n\
}\n\
";


