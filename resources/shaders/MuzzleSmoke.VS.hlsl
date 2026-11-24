#include "ParticleVertex.hlsli"
#include "MuzzleSmoke.hlsli"

SmokeVertexOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID) {
    SmokeVertexOutput output;
    Particle particle = gParticles[instanceId];
    output.shape = drawFlags;
    output.noiseAmount = smokeNoise;
    output.seed = frac(float(instanceId) * float2(0.75488f, 0.56984f)
        + particle.lifeTime * float2(17.3f, 29.1f));
    if (particle.lifeTime > 0.0f) {
        float2 lateral = float2(particle.emissionDirection.z, -particle.emissionDirection.x);
        lateral *= rsqrt(max(dot(lateral, lateral), 0.01f));
        float sway = sin(particle.currentTime * lerp(12.0f, 19.0f, output.seed.y)
            + 6.28319f * output.seed.x);
        particle.translate.xz += lateral * sway * (smokeSway * smoothstep(0.0f, 0.16f, particle.currentTime));
        // Ease in as the light falls away, even when authored with a shorter lifetime.
        float fadeIn = min(0.060f, 0.25f * particle.lifeTime);
        particle.color.a *= smoothstep(0.20f * fadeIn, fadeIn, particle.currentTime);
    }

    if (output.shape == kSmokeShapeVertical) {
        particle.scale.xy *= float2(0.76f, 1.6f);
        particle.rotation = sin(particle.rotation) * 0.15f;
        input.position.y += 0.18f;
    }

    output.position = ProjectParticleVertex(input.position, particle, MakeParticleBillboard(particle));
    output.texcoord = input.texcoord;
    output.color = particle.color;
    output.age = particle.currentTime;
    return output;
}
