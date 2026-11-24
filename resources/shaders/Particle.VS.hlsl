#include "Particle.hlsli"
#include "ParticleVertex.hlsli"

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID) {
    VertexShaderOutput output;
    Particle particle = gParticles[instanceId];
    float32_t4x4 worldMatrix = MakeParticleBillboard(particle);

    output.localEye = 0.0f;
    if ((drawFlags & kParticleFlameGlow) != 0) {
        float3 eye = (cameraSpace != 0 ? float3(0.0f, 0.0f, 0.0f) : gPerView.cameraPosition.xyz) - particle.translate;
        float3 localEye = float3(dot(eye, worldMatrix[0].xyz), dot(eye, worldMatrix[1].xyz),
            dot(eye, worldMatrix[2].xyz));
        output.localEye = localEye / max(particle.scale, 0.00001f);
    }

    output.position = ProjectParticleVertex(input.position, particle, worldMatrix);
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
