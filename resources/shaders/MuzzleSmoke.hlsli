// SmokeShape uses the third particle vertex root constant.
static const uint kSmokeShapeVertical = 1u;

struct SmokeVertexOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float age : TEXCOORD1;
    nointerpolation float2 seed : TEXCOORD2;
    nointerpolation uint shape : TEXCOORD3;
    nointerpolation float noiseAmount : TEXCOORD4;
};
