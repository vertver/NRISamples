#include "BindingBridge.hlsli"

struct outputVS
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

outputVS main
(
    float2 inPos : POSITION0,
    float2 inTexCoord : TEXCOORD0
)
{
    outputVS output;

    output.position.xy = inPos;
    output.position.zw = float2( 0.0, 1.0 );

    output.texCoord = inTexCoord;

    return output;
}
