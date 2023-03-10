//--------------------------------------------------------------------------------------
// Water surface vertex shader
//--------------------------------------------------------------------------------------
// Vertex shader that distorts the water surface - which is a fine grid (tessellation could have been used, but more complex)
// Also sends data for pixel lighting as the water uses specular lighting

#include "Common.hlsli"


//--------------------------------------------------------------------------------------
// Texture maps
//--------------------------------------------------------------------------------------

// Note that the texture register numbers are important - slot 0 is not used here, but is used for the diffuse texture in
// other shaders, so the first texture goes to slot 1. Similarly there is a water height map used for other shaders in slot 2
// We make sure each map gets a unique slot across all the shaders in use at any given point
Texture2D NormalHeightMap : register(t1); // Normal/height map for the water waves

SamplerState StandardFilter : register(s0); // Filtering used on most textures (trilinear or anisotropic - chosen on the C++ side)


//--------------------------------------------------------------------------------------
// Shader code
//--------------------------------------------------------------------------------------

WorldPositionPixelShaderInput main( BasicVertex input )
{
	WorldPositionPixelShaderInput output;

	// Add 4th element to position
	float4 modelPosition = float4(input.position, 1.0f);

	// Sample the height at this point on the water's surface. Sample at four different sizes and combine to give complex waves
	// All UVs are moving, different speeds for each size
	// TODO - STAGE 6: Add vertex displacement to get bumpy water
	//                 You need to sample the alpha channel of NormalHeightMap four times and add them all together. Look at how the
	//                 normals are sampled in the WaterSurface_ps pixel shader file. Use the same process here except:
	//                 - Use SampleLevel instead of Sample, and add an extra final parameter of 0 (this is needed to access textures in a vertex shader)
	//                 - Grab the alpha channel, not the rgb (normals and heights stored in same texture - same as parallax mapping)
	//                 - Don't do the * 2 - 1 part, that converts rgb to xyz for normals, here the 0->1 range of alpha is already OK
	//                 - Add all the alphas together, the line below already does the averaging and scaling to the final height
    float2 waterUV = input.uv;
    float1 normal1 = NormalHeightMap.SampleLevel(StandardFilter, WaterSize1 * (waterUV + gWaterMovement * WaterSpeed1), 0).a;
    float1 normal2 = NormalHeightMap.SampleLevel(StandardFilter, WaterSize2 * (waterUV + gWaterMovement * WaterSpeed2), 0).a;
    float1 normal3 = NormalHeightMap.SampleLevel(StandardFilter, WaterSize3 * (waterUV + gWaterMovement * WaterSpeed3), 0).a;
    float1 normal4 = NormalHeightMap.SampleLevel(StandardFilter, WaterSize4 * (waterUV + gWaterMovement * WaterSpeed4), 0).a;
	
    float height = normal1 + normal2 + normal3 + normal4; // Not 2, read comment above
	
  
	// Average heights and add to water y-coordinate
	modelPosition.y += (0.25f * height - 0.5f) * MaxWaveHeight * gWaveScale; // -0.5 makes wave movement an equal amount up or down from basic water height

	// Transform new water vertex position to world space and send to pixel shader
	float4 worldPosition = mul(gWorldMatrix, modelPosition);
	output.worldPosition = worldPosition.xyz;

	// Use camera matrices to further transform the vertex from world space into view space and finally into 2D "projection" space
	float4 viewPosition  = mul(gViewMatrix, worldPosition);
	output.projectedPosition  = mul(gProjectionMatrix, viewPosition);

	// Pass texture coordinates (UVs) on to the pixel shader
	output.uv = input.uv;

	return output;
}
