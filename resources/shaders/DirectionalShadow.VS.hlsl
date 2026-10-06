cbuffer ShadowTransform : register(b0)
{
    float4x4 gLightWVP;
};
float4 main(float4 position : POSITION0) : SV_Position
{
    return mul(position, gLightWVP);
}
