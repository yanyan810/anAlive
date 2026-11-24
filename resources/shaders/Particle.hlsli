static const uint kParticleUseTexture = 1u;
static const uint kParticleFlameFacets = 2u;
static const uint kParticleEmissiveBlend = 4u;
static const uint kParticleFlameGlow = 8u;
static const uint kParticleSparkStreak = 16u;
static const uint kParticleSoftDisc = 32u;

struct VertexShaderOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint drawFlags : TEXCOORD1;
    float3 localPosition : TEXCOORD2;
    nointerpolation float3 localEye : TEXCOORD3;
    nointerpolation float age : TEXCOORD4;
};
