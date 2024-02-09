#include "BindingBridge.hlsli"

struct PushConstants
{
    float mipBias;
};

NRI_PUSH_CONSTANTS( PushConstants, g_PushConstants, 0 );

NRI_RESOURCE( Texture2D, g_TiledTexture, t, 0, 0 );
NRI_RESOURCE( SamplerState, g_Sampler, s, 0, 0 );

struct outputVS
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

float4 main( in outputVS input ) : SV_Target
{
    return g_TiledTexture.SampleBias( g_Sampler, input.texCoord, g_PushConstants.mipBias );
}
