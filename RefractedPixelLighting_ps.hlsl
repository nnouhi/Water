//--------------------------------------------------------------------------------------
// Pixel shader for lit geometry in refraction (for geometry below the water)
//--------------------------------------------------------------------------------------
// Per-pixel lighting for refracted objects - only renders below water
// The equivalent of the PixelLighting_ps file but with additional work for refraction

#include "Common.hlsli"

// Actually include the pixel lighting shader so the code can be shared. Only need the additional work for refraction here. Unusual but works fine. 
#include "PixelLighting_ps.hlsl" 


//--------------------------------------------------------------------------------------
// Texture maps
//--------------------------------------------------------------------------------------

// Note that the texture register numbers are important - slots 0-5 are not used here, but are used in other shaders
// We make sure each map gets a unique slot across all the shaders in use at any given point

// Water height map - each pixel contains the y coordinate of the surface of the water at that point - rendered in an earlier pass
Texture2D WaterHeightMap : register(t2);

SamplerState BilinearMirror : register(s1); // We use mirror mode for the reflection and refraction because pixels off screen might come
                                            // into view due to the water wiggling. Mirror mode will put some vaguely sensible colours there
                                            // although it is a bit of a cheat. An alternative solution is to render the reflection / refraction
                                            // maps larger than they need to be but that adds complexity for little gain


//--------------------------------------------------------------------------------------
// Shader code
//--------------------------------------------------------------------------------------

float4 main(LightingPixelShaderInput input) : SV_Target
{
    // Sample the height map of the water to find if this pixel is underwater
    float2 screenUV = input.projectedPosition.xy / float2(gViewportWidth, gViewportHeight);
    float waterHeight = WaterHeightMap.Sample(BilinearMirror, screenUV).x;
    float objectDepth = waterHeight - input.worldPosition.y;

    clip(objectDepth); // Remove pixels with negative depth - i.e. above the water

    // Get the basic colour for this pixel by calling the standard pixel-lighting shader (included at the top)
    float3 sceneColour = PixelLighting(input).rgb;

    // Darken the colour based on the depth underwater
    // TODO - STAGE 1: Darken deep water
    //                 Use the < and > keys to adjust the water height, we would like objects going deep underwater to darken and
    //                 become blue as happens will real water. The second line below (refractionColour = ...) does the colour tint, 
    //                 but first you need to calculate a factor of how much to darken. There is a constant "WaterExtinction" in the
    //                 common.hlsi file. It sets how many metres red, blue and green light can travel in water. We also have the
    //                 depth underwater of the current pixel in the variable "objectDepth":
    //                 - Formula needed in line below is simple, object depth divided by water extinction level
    //                 - However, ensure the result does not exceed 1. There is a HLSL function to do this better than an if, but
    //                   use an "if" if you don't remember it.
    //                 When finished ensure objects underwater darken to blue
    float3 depthDarken = saturate(objectDepth / WaterExtinction); // Not 0, read comment above
    float3 refractionColour = lerp(sceneColour, normalize(WaterExtinction) * WaterDiffuseLevel, depthDarken);

    // Store the darkened colour in rgb and a value representing how deep the pixel is in alpha (used for refraction distortion)
    // This is written to a 8-bit RGBA texture, so this alpha depth value has limited accuracy
    return float4(refractionColour, saturate(objectDepth / MaxDistortionDistance)); // Alpha ranges 0->1 for depths of 0 to MaxDistortionDistance
}
