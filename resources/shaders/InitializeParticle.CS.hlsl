struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float rotation;
    float angularVelocity;
    float3 emissionDirection;
    float4 color;
};

static const uint kMaxParticles = 1024;
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles) {
        // Particle構造体の全要素を0で埋める
        gParticles[particleIndex] = (Particle)0;
        gFreeList[particleIndex] = particleIndex;
    }
    
    if (particleIndex == 0) {
        gFreeListIndex[0] = kMaxParticles - 1;
    }
}
