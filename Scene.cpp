//--------------------------------------------------------------------------------------
// Scene geometry and layout preparation
// Scene rendering & update
//--------------------------------------------------------------------------------------

#include "Scene.h"
#include "Mesh.h"
#include "Model.h"
#include "Camera.h"
#include "State.h"
#include "Shader.h"
#include "Input.h"
#include "Common.h"

#include "CVector2.h" 
#include "CVector3.h" 
#include "CMatrix4x4.h"
#include "MathHelpers.h"     // Helper functions for maths
#include "GraphicsHelpers.h" // Helper functions to unclutter the code here
#include "ColourRGBA.h" 

#include <array>
#include <sstream>
#include <memory>


//--------------------------------------------------------------------------------------
// Scene Data
//--------------------------------------------------------------------------------------

// Constants controlling speed of movement/rotation (measured in units per second because we're using frame time)
const float ROTATION_SPEED = 1.5f;  // Radians per second for rotation
const float MOVEMENT_SPEED = 50.0f; // Units per second for movement (what a unit of length is depends on 3D model - i.e. an artist decision usually)

// Lock FPS to monitor refresh rate, which will typically set it to 60fps. Press 'p' to toggle to full fps
bool lockFPS = true;
bool wireframe = true;

// Meshes, models and cameras, same meaning as TL-Engine. Meshes prepared in InitGeometry function, Models & camera in InitScene
Mesh* gSkyMesh;
Mesh* gGroundMesh;
Mesh* gTrollMesh;
Mesh* gCrateMesh;
Mesh* gLightMesh;
Mesh* gWaterMesh;

Model* gSky;
Model* gGround;
Model* gTroll;
Model* gCrate;
Model* gWater;

Camera* gCamera;


// Store lights in an array in this exercise
const int NUM_LIGHTS = 2;
struct Light
{
	Model*   model;
	CVector3 colour;
	float    strength;
};
Light gLights[NUM_LIGHTS];


// Additional light information
CVector3 gAmbientColour = { 0.5f, 0.5f, 0.5f }; // Background level of light (slightly bluish to match the far background, which is dark blue)
float    gSpecularPower = 256; // Specular power controls shininess - same for all models in this app

ColourRGBA gBackgroundColor = { 0.5f, 0.5f, 0.5f, 1.0f };

// Variables controlling light1's orbiting
const float gLightOrbitRadius = 20.0f;
const float gLightOrbitSpeed = 0.7f;



//--------------------------------------------------------------------------------------
// Constant Buffers
//--------------------------------------------------------------------------------------
// Variables sent over to the GPU each frame
// The structures are now in Common.h
// IMPORTANT: Any new data you add in C++ code (CPU-side) is not automatically available to the GPU
//            Anything the shaders need (per-frame or per-model) needs to be sent via a constant buffer

PerFrameConstants gPerFrameConstants;      // The constants (settings) that need to be sent to the GPU each frame (see common.h for structure)
ID3D11Buffer*     gPerFrameConstantBuffer; // The GPU buffer that will recieve the constants above

PerModelConstants gPerModelConstants;      // As above, but constants (settings) that change per-model (e.g. world matrix)
ID3D11Buffer*     gPerModelConstantBuffer; // --"--



//--------------------------------------------------------------------------------------
// Textures
//--------------------------------------------------------------------------------------

// DirectX objects controlling textures used in this lab
ID3D11Resource*           gSkyDiffuseSpecularMap = nullptr;
ID3D11ShaderResourceView* gSkyDiffuseSpecularMapSRV = nullptr;
ID3D11Resource*           gGroundDiffuseSpecularMap = nullptr;
ID3D11ShaderResourceView* gGroundDiffuseSpecularMapSRV = nullptr;
ID3D11Resource*           gCrateDiffuseSpecularMap = nullptr;
ID3D11ShaderResourceView* gCrateDiffuseSpecularMapSRV = nullptr;
ID3D11Resource*           gTrollDiffuseSpecularMap = nullptr;
ID3D11ShaderResourceView* gTrollDiffuseSpecularMapSRV = nullptr;

ID3D11Resource*           gLightDiffuseMap = nullptr;
ID3D11ShaderResourceView* gLightDiffuseMapSRV = nullptr;


//// Water textures / render targets
ID3D11Resource*           gWaterNormalMap = nullptr;          // The normal/height map used for the waves on the surface of the water
ID3D11ShaderResourceView* gWaterNormalMapSRV = nullptr;       // --"--
ID3D11Texture2D*          gWaterHeight = nullptr;            // The height of the water above the floor at each pixel - a data texture rendered each frame
ID3D11ShaderResourceView* gWaterHeightSRV = nullptr;          // --"--  Used to detect the boundary between above water and underwater
ID3D11RenderTargetView*   gWaterHeightRenderTarget = nullptr; // --"--
ID3D11Texture2D*          gReflection = nullptr;             // The reflected scene is rendered into this texture 
ID3D11ShaderResourceView* gReflectionSRV = nullptr;           // --"-- For reading the texture in shaders
ID3D11RenderTargetView*   gReflectionRenderTarget = nullptr;  // --"-- For writing to the texture as a render target
ID3D11Texture2D*          gRefraction = nullptr;             // The refracted scene is rendered into this texture
ID3D11ShaderResourceView* gRefractionSRV = nullptr;           // --"-- For reading the texture in shaders
ID3D11RenderTargetView*   gRefractionRenderTarget = nullptr;  // --"-- For writing to the texture as a render target


//--------------------------------------------------------------------------------------
// Initialise scene geometry, constant buffers and states
//--------------------------------------------------------------------------------------

// Prepare the geometry required for the scene
// Returns true on success
bool InitGeometry()
{
	////--------------- Load meshes ---------------////

	// Load mesh geometry data, just like TL-Engine this doesn't create anything in the scene. Create a Model for that.
	try
	{
		gSkyMesh    = new Mesh("Skybox.x");
		gGroundMesh = new Mesh("Hills.x");
		gTrollMesh  = new Mesh("Troll.x");
		gCrateMesh  = new Mesh("CargoContainer.x");
		gLightMesh  = new Mesh("Light.x");
		gWaterMesh  = new Mesh(CVector3(-200,0,-200), CVector3(200,0,200), 400, 400, true); // Using special constructor that creates a grid - see Mesh.cpp
	}
	catch (std::runtime_error e)  // Constructors cannot return error messages so use exceptions to catch mesh errors (fairly standard approach this)
	{
		gLastError = e.what(); // This picks up the error message put in the exception (see Mesh.cpp)
		return false;
	}


	////--------------- Load / prepare textures & GPU states ---------------////

	// Load textures and create DirectX objects for them
	// The LoadTexture function requires you to pass a ID3D11Resource* (e.g. &gTrollDiffuseMap), which manages the GPU memory for the
	// texture and also a ID3D11ShaderResourceView* (e.g. &gTrollDiffuseMapSRV), which allows us to use the texture in shaders
	// The function will fill in these pointers with usable data. The variables used here are globals found near the top of the file.
	if (!LoadTexture("CubeMapB.jpg",             &gSkyDiffuseSpecularMap,    &gSkyDiffuseSpecularMapSRV) ||
		!LoadTexture("GrassDiffuseSpecular.dds", &gGroundDiffuseSpecularMap, &gGroundDiffuseSpecularMapSRV) ||
		!LoadTexture("TrollDiffuseSpecular.dds", &gTrollDiffuseSpecularMap,  &gTrollDiffuseSpecularMapSRV) ||
		!LoadTexture("CargoA.dds",               &gCrateDiffuseSpecularMap,  &gCrateDiffuseSpecularMapSRV) ||
		!LoadTexture("Flare.jpg",                &gLightDiffuseMap,          &gLightDiffuseMapSRV) ||
		!LoadTexture("WaterNormalHeight.png",    &gWaterNormalMap,           &gWaterNormalMapSRV))
	{
		gLastError = "Error loading textures";
		return false;
	}


	// Create all filtering modes, blending modes etc. used by the app (see State.cpp/.h)
	if (!CreateStates())
	{
		gLastError = "Error creating states";
		return false;
	}



	////--------------- Create textures needed for water rendering ---------------////
	
	// Using a helper function to load textures from files above. Here we create the scene texture manually
	// as we are creating a special kind of texture (one that we can render to). Many settings to prepare:
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width  = gViewportWidth;  // Reflection / refraction / water surface textures are full screen size - could experiment with making them smaller
	textureDesc.Height = gViewportHeight;
	textureDesc.MipLevels = 1; // No mip-maps when rendering to textures (or we would have to render every level)
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA texture (8-bits each)
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; // IMPORTANT: Indicate we will use texture as render target, and pass it to shaders
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;
	if (FAILED(gD3DDevice->CreateTexture2D(&textureDesc, NULL, &gReflection)))
	{
		gLastError = "Error creating reflection texture";
		return false;
	}

	// We created the relection texture above, now we get a "view" of it as a render target, i.e. get a special pointer to the texture that we use when rendering to it
	if (FAILED(gD3DDevice->CreateRenderTargetView(gReflection, NULL, &gReflectionRenderTarget)))
	{
		gLastError = "Error creating reflection render target view";
		return false;
	}

	// We also need to send this texture (resource) to the shaders. To do that we must create a shader-resource "view"
	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc = {};
	srDesc.Format = textureDesc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;
	if (FAILED(gD3DDevice->CreateShaderResourceView(gReflection, &srDesc, &gReflectionSRV)))
	{
		gLastError = "Error creating reflection shader resource view";
		return false;
	}



	// Same again for refraction texture. Structures already set up so can save a few lines
	if (FAILED(gD3DDevice->CreateTexture2D(&textureDesc, NULL, &gRefraction)))
	{
		gLastError = "Error creating refraction texture";
		return false;
	}
	if (FAILED(gD3DDevice->CreateRenderTargetView(gRefraction, NULL, &gRefractionRenderTarget)))
	{
		gLastError = "Error creating refraction render target view";
		return false;
	}
	if (FAILED(gD3DDevice->CreateShaderResourceView(gRefraction, &srDesc, &gRefractionSRV)))
	{
		gLastError = "Error creating refraction shader resource view";
		return false;
	}


	// Same again for the water height texture.
	textureDesc.Format = srDesc.Format = DXGI_FORMAT_R32_FLOAT; // Water surface height is just one value per pixel - so texture only needs red channel using a 32-bit float
	if (FAILED(gD3DDevice->CreateTexture2D(&textureDesc, NULL, &gWaterHeight)))
	{
		gLastError = "Error creating water height texture";
		return false;
	}
	if (FAILED(gD3DDevice->CreateRenderTargetView(gWaterHeight, NULL, &gWaterHeightRenderTarget)))
	{
		gLastError = "Error creating water height render target view";
		return false;
	}
	if (FAILED(gD3DDevice->CreateShaderResourceView(gWaterHeight, &srDesc, &gWaterHeightSRV)))
	{
		gLastError = "Error creating water height shader resource view";
		return false;
	}


	////--------------- Prepare shaders and constant buffers to communicate with them ---------------////

	// Load the shaders required for the geometry we will use (see Shader.cpp / .h)
	if (!LoadShaders())
	{
		gLastError = "Error loading shaders";
		return false;
	}

	// Create GPU-side constant buffers to receive the gPerFrameConstants and gPerModelConstants structures above
	// These allow us to pass data from CPU to shaders such as lighting information or matrices
	// See the comments above where these variable are declared and also the UpdateScene function
	gPerFrameConstantBuffer       = CreateConstantBuffer(sizeof(gPerFrameConstants));
	gPerModelConstantBuffer       = CreateConstantBuffer(sizeof(gPerModelConstants));
	if (gPerFrameConstantBuffer == nullptr || gPerModelConstantBuffer == nullptr)
	{
		gLastError = "Error creating constant buffers";
		return false;
	}

	return true;
}


// Prepare the scene
// Returns true on success
bool InitScene()
{
	////--------------- Set up scene ---------------////

	gSky    = new Model(gSkyMesh);
	gGround = new Model(gGroundMesh);
	gTroll  = new Model(gTrollMesh);
	gCrate  = new Model(gCrateMesh);
	gWater  = new Model(gWaterMesh);

	// Initial positions
	gTroll->SetPosition({ 45, 0, 45 });
	gTroll->SetScale(10.0f);
	gCrate->SetPosition({ 65, 0, -170 });
	gCrate->SetRotation({ 0.0f, ToRadians(40.0f), 0.0f });
	gCrate->SetScale(12.0f);
	gSky->SetRotation({0, ToRadians(90.0f), 0});
	gSky->SetScale(10);
	gWater->SetPosition({ 0, 10, 0 });
	

	// Light set-up
	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		gLights[i].model = new Model(gLightMesh);
	}

	gLights[0].colour = { 0.8f, 0.8f, 1.0f };
	gLights[0].strength = 20;
	gLights[0].model->SetPosition({ 40, 20, -40 });
	gLights[0].model->SetScale(pow(gLights[0].strength, 0.5f)); // Convert light strength into a nice value for the scale of the light - equation is ad-hoc.

	gLights[1].colour = { 1.0f, 0.9f, 0.8f };
	gLights[1].strength = 1000;
	gLights[1].model->SetPosition({ 25, 800, -950 });
	gLights[1].model->SetScale(pow(gLights[1].strength, 0.5f));


	////--------------- Set up camera ---------------////

	gCamera = new Camera();
	gCamera->Position() = { -80, 50, 200 };
	gCamera->SetRotation({ ToRadians(16.0f), ToRadians(145.0f), 0.0f });
	gCamera->SetNearClip(5);
	gCamera->SetFarClip(100000);

	return true;
}


// Release the geometry and scene resources created above
void ReleaseResources()
{
	ReleaseStates();

	if (gRefractionRenderTarget)   gRefractionRenderTarget->Release();
	if (gRefractionSRV)            gRefractionSRV->Release();
	if (gRefraction)               gRefraction->Release();
	if (gReflectionRenderTarget)   gReflectionRenderTarget->Release();
	if (gReflectionSRV)            gReflectionSRV->Release();
	if (gReflection)               gReflection->Release();
	if (gWaterHeightRenderTarget)  gWaterHeightRenderTarget->Release();
	if (gWaterHeightSRV)           gWaterHeightSRV->Release();
	if (gWaterHeight)              gWaterHeight->Release();
	if (gWaterNormalMapSRV)        gWaterNormalMapSRV->Release();
	if (gWaterNormalMap)           gWaterNormalMap->Release();


	if (gLightDiffuseMapSRV)           gLightDiffuseMapSRV->Release();
	if (gLightDiffuseMap)              gLightDiffuseMap->Release();
	if (gCrateDiffuseSpecularMapSRV)   gCrateDiffuseSpecularMapSRV->Release();
	if (gCrateDiffuseSpecularMap)      gCrateDiffuseSpecularMap->Release();
	if (gTrollDiffuseSpecularMapSRV)   gTrollDiffuseSpecularMapSRV->Release();
	if (gTrollDiffuseSpecularMap)      gTrollDiffuseSpecularMap->Release();
	if (gGroundDiffuseSpecularMapSRV)  gGroundDiffuseSpecularMapSRV->Release();
	if (gGroundDiffuseSpecularMap)     gGroundDiffuseSpecularMap->Release();
	if (gSkyDiffuseSpecularMapSRV)     gSkyDiffuseSpecularMapSRV->Release();
	if (gSkyDiffuseSpecularMap)        gSkyDiffuseSpecularMap->Release();

	if (gPerModelConstantBuffer)        gPerModelConstantBuffer->Release();
	if (gPerFrameConstantBuffer)        gPerFrameConstantBuffer->Release();

	ReleaseShaders();

	// See note in InitGeometry about why we're not using unique_ptr and having to manually delete
	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		delete gLights[i].model;  gLights[i].model = nullptr;
	}
	delete gCamera;  gCamera = nullptr;
	delete gWater;   gWater = nullptr;
	delete gCrate;   gCrate = nullptr;
	delete gTroll;   gTroll = nullptr;
	delete gGround;  gGround = nullptr;
	delete gSky;     gSky = nullptr;

	delete gWaterMesh;   gWaterMesh = nullptr;
	delete gLightMesh;   gLightMesh = nullptr;
	delete gCrateMesh;   gCrateMesh = nullptr;
	delete gTrollMesh;   gTrollMesh = nullptr;
	delete gGroundMesh;  gGroundMesh = nullptr;
	delete gSkyMesh;     gSkyMesh = nullptr;
}



//--------------------------------------------------------------------------------------
// Scene Rendering
//--------------------------------------------------------------------------------------


//**************************
// Split the rendering of models into lit models and non-lit models. They need different
// shaders when rendering the normal scene, reflected and refracted scenes and this
// breaking up of the code makes dealing with the different shaders simpler - see RenderSceneFromCamer

// Render lit models. Assumes most GPU setup has been done (e.g. shader selection, camera matrix setup), but will do
// per-model setup (model textures, model-specific states etc.)
void RenderLitModels()
{
	gD3DContext->PSSetShaderResources(0, 1, &gGroundDiffuseSpecularMapSRV); // First parameter must match texture slot number in the shader
	gGround->Render();

	gD3DContext->PSSetShaderResources(0, 1, &gTrollDiffuseSpecularMapSRV);
	gTroll->Render();

	gD3DContext->PSSetShaderResources(0, 1, &gCrateDiffuseSpecularMapSRV); 
	gCrate->Render();
}


// Render models that don't use lighting. Assumes most GPU setup has been done (e.g. shader selection, camera matrix setup), but will do
// per-model setup (model textures, model-specific states etc.)
void RenderOtherModels()
{
	////--------------- Render sky ---------------////

	// Using a pixel shader that tints the texture - don't need a tint on the sky so set it to white
	gPerModelConstants.objectColour = { 1, 1, 1 };

	// Sky points inwards
	gD3DContext->RSSetState(gCullNoneState);

	// Render sky
	gD3DContext->PSSetShaderResources(0, 1, &gSkyDiffuseSpecularMapSRV);
	gSky->Render();



	////--------------- Render lights ---------------////

	// Select the texture and sampler to use in the pixel shader
	gD3DContext->PSSetShaderResources(0, 1, &gLightDiffuseMapSRV); // First parameter must match texture slot number in the shaer

	// States - additive blending, read-only depth buffer and no culling (standard set-up for blending)
	gD3DContext->OMSetBlendState(gAdditiveBlendingState, nullptr, 0xffffff);
	gD3DContext->OMSetDepthStencilState(gDepthReadOnlyState, 0);
	gD3DContext->RSSetState(gCullNoneState);

	// Render all the lights in the array
	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		gPerModelConstants.objectColour = gLights[i].colour; // Set any per-model constants apart from the world matrix just before calling render (light colour here)
		gLights[i].model->Render();
	}

	// Restore standard states
	gD3DContext->OMSetBlendState(gNoBlendingState, nullptr, 0xffffff);
	gD3DContext->OMSetDepthStencilState(gUseDepthBufferState, 0);
	gD3DContext->RSSetState(gCullBackState);

}


void SelectCamera(Camera* camera)
{
	// Set camera matrices in the constant buffer and send over to GPU
	gPerFrameConstants.cameraMatrix = camera->WorldMatrix();
	gPerFrameConstants.viewMatrix = camera->ViewMatrix();
	gPerFrameConstants.projectionMatrix = camera->ProjectionMatrix();
	gPerFrameConstants.viewProjectionMatrix = camera->ViewProjectionMatrix();
	UpdateConstantBuffer(gPerFrameConstantBuffer, gPerFrameConstants);

	// Indicate that the constant buffer we just updated is for use in the vertex shader (VS) and pixel shader (PS)
	gD3DContext->VSSetConstantBuffers(0, 1, &gPerFrameConstantBuffer); // First parameter must match constant buffer number in the shader 
	gD3DContext->PSSetConstantBuffers(0, 1, &gPerFrameConstantBuffer);
}


// Render everything in the scene from the given camera
void RenderSceneFromCamera(Camera* camera)
{
	SelectCamera(camera);


	////--------------- Prepare common states / textures / samplers ---------------///
	// The water normal / height map is used in many stages of the following code, so it is permanently left in slot 1
	gD3DContext->PSSetShaderResources(1, 1, &gWaterNormalMapSRV); // First parameter must match texture slot number in the shader
	gD3DContext->VSSetShaderResources(1, 1, &gWaterNormalMapSRV); // We also need the water height map in the water vertex shader to displace the water surface (quite rare to use a texture in the vertex shader)

	gD3DContext->PSSetSamplers(0, 1, &gAnisotropic4xSampler);  // Standard sampler for most textures goes in slot 0 (first parameter - must match value in shaders)
	gD3DContext->VSSetSamplers(0, 1, &gAnisotropic4xSampler);  // Use in vertex shader as well
	gD3DContext->PSSetSamplers(1, 1, &gBilinearMirrorSampler); // Mirroring sampler used when distorting reflection and refraction - when wiggling UVs we sometimes get 
	                                                           // pixels outside the bounds of the texture. Using mirror mode ensures theses are a reasonable local colour
	                                                           // This sampler also disables mip-maps - we won't have them for a scene we render ourselves

	// Standard states - no blending, ordinary depth buffer and back-face culling
	gD3DContext->OMSetBlendState(gNoBlendingState, nullptr, 0xffffff);
	gD3DContext->OMSetDepthStencilState(gUseDepthBufferState, 0);
	gD3DContext->RSSetState(gCullBackState);


	//***************************
	// Render water height
	//***************************

	// Target the water height texture for rendering
	gD3DContext->OMSetRenderTargets(1, &gWaterHeightRenderTarget, gDepthStencil);

	// Clear the water depth texture and depth buffer
	// Note we reuse the same depth buffer for all the rendering passes, clearing it each time
	float Zero[4] = {0,0,0,0};
	gD3DContext->ClearRenderTargetView(gWaterHeightRenderTarget, Zero);
	gD3DContext->ClearDepthStencilView(gDepthStencil, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// Select shaders
	gD3DContext->VSSetShader(gWaterSurfaceVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gWaterHeightPixelShader, nullptr, 0);
	gD3DContext->GSSetShader(nullptr, nullptr, 0);  // Switch off geometry shader when not using it (pass nullptr for first parameter)

	// Render heights of water surface
	gWater->Render();


	//***************************
	// Render refracted scene
	//***************************

	// Target the refraction texture for rendering and clear depth buffer
	gD3DContext->OMSetRenderTargets(1, &gRefractionRenderTarget, gDepthStencil);
	gD3DContext->ClearRenderTargetView(gRefractionRenderTarget, &gBackgroundColor.r);
	gD3DContext->ClearDepthStencilView(gDepthStencil, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// Select the water height map (rendered in the last step) as a texture, so the refraction shader can tell what is underwater
	gD3DContext->PSSetShaderResources(2, 1, &gWaterHeightSRV); // First parameter must match texture slot number in the shader

	////// Render lit models

	// Select shaders for refraction rendering of lit models
	gD3DContext->VSSetShader(gPixelLightingVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gRefractedPixelLightingPixelShader, nullptr, 0);
	
	RenderLitModels();

	////// Render sky and lights

	// Select shaders for refraction rendering of non-lit models
	gD3DContext->VSSetShader(gBasicTransformWorldPosVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gRefractedTintedTexturePixelShader, nullptr, 0);
	
	RenderOtherModels();


	//***************************
	// Render reflected scene
	//***************************

	// Reflect the camera's matrix in the water plane - to show what is seen in the reflection.
	// Will assume the water is horizontal in the xz plane, which makes the reflection simple:
	// - Negate the y component of the x,y and z axes of the reflected camera matrix
	// - Put the reflected camera y position on the opposite side of the water y position
	CMatrix4x4 originalMatrix = camera->WorldMatrix();
	camera->XAxis().y *= -1; // Negate y component of each axis of the matrix
	camera->YAxis().y *= -1;
	camera->ZAxis().y *= -1;

	// Camera distance above water = Camera.y - Water.y
	// Reflected camera is same distance below water = Water.y - (Camera.y - Water.y) = 2*Water.y - Camera.y
	// (Position is on bottom row (row 3) of matrix so Camera.y is matrix element e31)
	camera->Position().y = gWater->Position().y * 2 - camera->Position().y;
	
	// Use camera with reflected matrix for rendering
	SelectCamera(camera);

	// IMPORTANT: when rendering in a mirror must switch from back face culling to front face culling (because clockwise / anti-clockwise order of points will be reversed)
	gD3DContext->RSSetState(gCullFrontState);

	// Target the reflection texture for rendering and clear depth buffer
	gD3DContext->OMSetRenderTargets(1, &gReflectionRenderTarget, gDepthStencil);
	gD3DContext->ClearRenderTargetView(gReflectionRenderTarget, &gBackgroundColor.r);
	gD3DContext->ClearDepthStencilView(gDepthStencil, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// Note that water height map is still selected as a texture (from the previous step) and will be used here to tell what is above the water

	////// Render lit models

	// Select shaders for reflection rendering of lit models
	gD3DContext->VSSetShader(gPixelLightingVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gReflectedPixelLightingPixelShader, nullptr, 0);
	
	RenderLitModels();

	////// Render sky and lights

	// Select shaders for reflection rendering of non-lit models
	gD3DContext->VSSetShader(gBasicTransformWorldPosVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gReflectedTintedTexturePixelShader, nullptr, 0);
	
	RenderOtherModels();

	// Restore original camera and culling state
	camera->WorldMatrix() = originalMatrix;
	SelectCamera(camera);
	gD3DContext->RSSetState(gCullBackState);

	// Detach the water height map from being a source texture so it can be used as a render target again next frame (if you don't do this DX emits lots of warnings)
	ID3D11ShaderResourceView* gNullSRV = nullptr;
	gD3DContext->PSSetShaderResources(2, 1, &gNullSRV);


	//***************************
	// Render main scene
	//***************************
	
	// Finally target the back buffer for rendering, clear depth buffer
	gD3DContext->OMSetRenderTargets(1, &gBackBufferRenderTarget, gDepthStencil);
	gD3DContext->ClearDepthStencilView(gDepthStencil, D3D11_CLEAR_DEPTH, 1.0f, 0);

	////// Render lit models

	// Select shaders for ordinary rendering of lit models
	gD3DContext->VSSetShader(gPixelLightingVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gPixelLightingPixelShader, nullptr, 0);
	RenderLitModels();


	////// Render water surface - combining reflection and refraction
	// Render water before transparent objects or it will draw over them

	// Select the reflection and refraction textures (rendered in the previous steps)
	gD3DContext->PSSetShaderResources(3, 1, &gRefractionSRV); // First parameter must match texture slot number in the shader
	gD3DContext->PSSetShaderResources(4, 1, &gReflectionSRV);

	gD3DContext->VSSetShader(gWaterSurfaceVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gWaterSurfacePixelShader, nullptr, 0);
	gWater->Render();

	// Detach the reflection/refraction maps from being source textures so they can be used as a render target again next frame (if you don't do this DX emits lots of warnings)
	gD3DContext->PSSetShaderResources(3, 1, &gNullSRV);
	gD3DContext->PSSetShaderResources(4, 1, &gNullSRV);


	////// Render sky and lights

	// Select shaders for ordinary rendering of non-lit models
	gD3DContext->VSSetShader(gBasicTransformVertexShader, nullptr, 0);
	gD3DContext->PSSetShader(gTintedTexturePixelShader, nullptr, 0);
	RenderOtherModels();
}


// Rendering the scene
void RenderScene()
{
	//// Common settings ////

	// Set up the light information in the constant buffer
	// Don't send to the GPU yet, the function RenderSceneFromCamera will do that
	gPerFrameConstants.light1Colour   = gLights[0].colour * gLights[0].strength;
	gPerFrameConstants.light1Position = gLights[0].model->Position();
	gPerFrameConstants.light2Colour   = gLights[1].colour * gLights[1].strength;
	gPerFrameConstants.light2Position = gLights[1].model->Position();

	gPerFrameConstants.ambientColour  = gAmbientColour;
	gPerFrameConstants.specularPower  = gSpecularPower;
	gPerFrameConstants.cameraPosition = gCamera->Position();

	gPerFrameConstants.viewportWidth  = static_cast<float>(gViewportWidth);
	gPerFrameConstants.viewportHeight = static_cast<float>(gViewportHeight);


	////--------------- Main scene rendering ---------------////

	// Setup the viewport to the size of the main window
	D3D11_VIEWPORT vp;
	vp.Width = static_cast<FLOAT>(gViewportWidth);
	vp.Height = static_cast<FLOAT>(gViewportHeight);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	gD3DContext->RSSetViewports(1, &vp);

	// Render the scene from the main camera
	RenderSceneFromCamera(gCamera);


	////--------------- Scene completion ---------------////

	// TODO = STAGE 0: Look at the reflection and refraction textures
	//                 To begin with three textures are rendered:
	//                 1. The height of the water surface at each pixel on the screen (mainly needed when the water surface becomes bumpy)
	//                 2. Everything below the water - for refractions
	//                 3. Everything above the water, reflected vertically - for reflections
	//                 You can view the last two textures now:
	//                 - Comment in the first of the lines below to see the refraction texture. Press the < and > keys to adjust the
	//                   water height. Note how it clips exactly at the water line - don't want to see above the water items in the refractions
	//                 - Then comment that line out again, and comment in the second line to see the reflection, notice that it is the view from
	//                   below the water, but is correcly positioned on the screen to apply to the water surface
	//                 The starting point for the program mixes 75% refraction + 25% reflection + ordinary scene. The C++ rendering code above is
	//                 not especially complex, only the camera reflection is of any particular interest. All the real work is in the shaders.
	//                 - Comment both lines out and see how those textures are combined to give the basic water effect
	//                 - Continue to the stages in the fx file
	//                 
	//gD3DContext->CopyResource( gBackBufferTexture, gRefraction );
	//gD3DContext->CopyResource( gBackBufferTexture, gReflection );


	// When drawing to the off-screen back buffer is complete, we "present" the image to the front buffer (the screen)
	// Set first parameter to 1 to lock to vsync
	gSwapChain->Present(lockFPS ? 1 : 0, 0);
}



//--------------------------------------------------------------------------------------
// Scene Update
//--------------------------------------------------------------------------------------

// Update models and camera. frameTime is the time passed since the last frame
void UpdateScene(float frameTime)
{
	// Orbit one light - a bit of a cheat with the static variable [ask the tutor if you want to know what this is]
	static float lightRotate = 0.0f;
	static bool go = true;
	gLights[0].model->SetPosition( {40 + cos(lightRotate) * gLightOrbitRadius, 20, -40 + sin(lightRotate) * gLightOrbitRadius} );
	if (go)  lightRotate -= gLightOrbitSpeed * frameTime;
	if (KeyHit(Key_0))  go = !go;

	// Control of camera & troll
	gCamera->Control(frameTime, Key_Up, Key_Down, Key_Left, Key_Right, Key_W, Key_S, Key_A, Key_D);
	gTroll->Control(0, frameTime, Key_None, Key_None, Key_J, Key_L, Key_None, Key_None, Key_I, Key_K);


	 // Control water height
	gPerFrameConstants.waterPlaneY = gWater->Position().y;
	if (KeyHeld(Key_Period))  gPerFrameConstants.waterPlaneY += 5.0f * frameTime;
	if (KeyHeld(Key_Comma ))  gPerFrameConstants.waterPlaneY -= 5.0f * frameTime;
	gWater->SetPosition({gWater->Position().x, gPerFrameConstants.waterPlaneY, gWater->Position().z});

    // Control wave height
	static float waveScale = 0.6f;
	if (KeyHeld(Key_Plus ))  waveScale += 0.5f * frameTime;
	if (KeyHeld(Key_Minus))  waveScale -= 0.5f * frameTime;
	if (waveScale < 0)  waveScale = 0;
	gPerFrameConstants.waveScale = waveScale;

	// Move water
	static CVector2 waterPos = {0, 0};
	const float waterSpeed = 1.0f;
	waterPos += frameTime * waterSpeed * CVector2(0.01f, 0.015f);
	gPerFrameConstants.waterMovement = waterPos;
	
	// Toggle FPS limiting
	if (KeyHit(Key_P))  lockFPS = !lockFPS;

	// Show frame time / FPS in the window title //
	const float fpsUpdateTime = 0.5f; // How long between updates (in seconds)
	static float totalFrameTime = 0;
	static int frameCount = 0;
	totalFrameTime += frameTime;
	++frameCount;
	if (totalFrameTime > fpsUpdateTime)
	{
		// Displays FPS rounded to nearest int, and frame time (more useful for developers) in milliseconds to 2 decimal places
		float avgFrameTime = totalFrameTime / frameCount;
		std::ostringstream frameTimeMs;
		frameTimeMs.precision(2);
		frameTimeMs << std::fixed << avgFrameTime * 1000;
		std::string windowTitle = "CO3303 Week 16: Water Rendering - Frame Time: " + frameTimeMs.str() +
			"ms, FPS: " + std::to_string(static_cast<int>(1 / avgFrameTime + 0.5f));
		SetWindowTextA(gHWnd, windowTitle.c_str());
		totalFrameTime = 0;
		frameCount = 0;
	}
}
