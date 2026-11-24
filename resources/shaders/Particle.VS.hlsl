#include "Particle.hlsli"

struct Particle {
    float32_t3 translate;
    float32_t3 scale;
    float32_t lifeTime;
    float32_t3 velocity;
    float32_t currentTime;
    float32_t rotation;
    float32_t angularVelocity;
    float32_t3 emissionDirection;
    float32_t4 color;
};

struct PerView {
    float32_t4x4 viewProjection;
    float32_t4x4 billboardMatrix;
    float32_t4x4 projection;
    float32_t4 cameraPosition;
};

StructuredBuffer<Particle> gParticles : register(t0);
ConstantBuffer<PerView> gPerView : register(b0);

cbuffer BillboardMode : register(b1) {
    uint billboardMode;
    uint cameraSpace;
    uint drawFlags;
};

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID) {
    VertexShaderOutput output;
    Particle particle = gParticles[instanceId];

    float32_t4x4 worldMatrix = gPerView.billboardMatrix; 
    
    if (billboardMode == 1 || billboardMode == 3 || billboardMode == 4) {
        float3 direction = billboardMode == 4 ? particle.emissionDirection : particle.velocity;
        float axisLength = length(direction);
        float3 up = axisLength > 0.001f ? direction / axisLength : float3(0.0f, 1.0f, 0.0f);
        
        // upベクトルから直交基底を作る
        float3 reference = abs(up.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        if (billboardMode == 3) {
            // Keep the long axis on the velocity and turn the polygon toward the eye.
            float3 sightline = cameraSpace != 0 ? particle.translate : particle.translate - gPerView.cameraPosition.xyz;
            float3 cameraRight = cross(up, sightline);
            if (dot(cameraRight, cameraRight) > 0.000001f) {
                reference = cameraRight;
            }
        }
        float3 forward = normalize(cross(reference, up));
        float3 right = normalize(cross(up, forward));

        worldMatrix[0] = float32_t4(right, 0.0f);
        worldMatrix[1] = float32_t4(up, 0.0f);
        worldMatrix[2] = float32_t4(forward, 0.0f);
        worldMatrix[3] = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (billboardMode == 2 || cameraSpace != 0) {
        // None (回転なし・固定)
        worldMatrix[0] = float32_t4(1.0f, 0.0f, 0.0f, 0.0f);
        worldMatrix[1] = float32_t4(0.0f, 1.0f, 0.0f, 0.0f);
        worldMatrix[2] = float32_t4(0.0f, 0.0f, 1.0f, 0.0f);
        worldMatrix[3] = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    // billboardMode == 0 の場合はそのまま gPerView.billboardMatrix を使用

    float s = sin(particle.rotation);
    float c = cos(particle.rotation);
    float3 rightAxis = worldMatrix[0].xyz;
    if (billboardMode == 4) {
        float3 forwardAxis = worldMatrix[2].xyz;
        worldMatrix[0].xyz = rightAxis * c + forwardAxis * s;
        worldMatrix[2].xyz = -rightAxis * s + forwardAxis * c;
    } else {
        float3 upAxis = worldMatrix[1].xyz;
        worldMatrix[0].xyz = rightAxis * c + upAxis * s;
        worldMatrix[1].xyz = -rightAxis * s + upAxis * c;
    }

    output.localEye = 0.0f;
    if ((drawFlags & kParticleFlameGlow) != 0) {
        float3 eye = (cameraSpace != 0 ? float3(0.0f, 0.0f, 0.0f) : gPerView.cameraPosition.xyz) - particle.translate;
        float3 localEye = float3(dot(eye, worldMatrix[0].xyz), dot(eye, worldMatrix[1].xyz),
            dot(eye, worldMatrix[2].xyz));
        output.localEye = localEye / max(particle.scale, 0.00001f);
    }

    worldMatrix[0] *= particle.scale.x;
    worldMatrix[1] *= particle.scale.y;
    worldMatrix[2] *= particle.scale.z;
    worldMatrix[3].xyz = particle.translate;

    float32_t4x4 viewProjection = cameraSpace != 0 ? gPerView.projection : gPerView.viewProjection;
    output.position = mul(input.position, mul(worldMatrix, viewProjection));
    output.texcoord = input.texcoord;
    output.color = particle.color;
    output.drawFlags = drawFlags;
    output.localPosition = input.position.xyz;
    output.age = particle.currentTime;
    if ((drawFlags & kParticleFlameFacets) != 0) {
        // Per-face emissive variation makes the volume readable without scene lighting.
        float angle = saturate(dot(input.normal, normalize(float3(-0.65f, -0.26f, -0.7f))));
        output.color.rgb *= lerp(0.69f, 1.0f, angle);
    }
    return output;
}
