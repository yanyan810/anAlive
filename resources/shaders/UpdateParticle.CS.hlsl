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

struct EmitterData {
    float3 translate; // 位置
    float radius; // 射出半径
    
    uint count; // 射出数
    float frequency; // 射出間隔
    float frequencyTime; // 射出間隔調整用時間
    uint emit; // 射出許可

    float3 velocityBase; // 基本速度
    float velocityVariance; // 速度分散

    float lifeTimeMin; // 最小寿命
    float lifeTimeMax; // 最大寿命
    uint shapeType;
    float shapeAngle;

    float3 shapeSize;
    uint randomSeed;

    float3 acceleration; // 加速度
    float padding2;

    float4 startColor; // 発生時の色
    float4 endColor; // 消滅時の色
    float3 scaleStart;
    float speedMin;

    float3 scaleEnd;
    float speedMax;

    float angleRandomDeg;
    float jitterDeg;
    float rotationStartDeg;
    float rotationRandomDeg;

    float angularVelocityMinDeg;
    float angularVelocityMaxDeg;
    float timeScale;
    float initialAge;
};

ConstantBuffer<EmitterData> gEmitter : register(b0);

static const uint kMaxParticles = 1024;
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);

struct PerFrame {
    float time;
    float deltaTime;
};
ConstantBuffer<PerFrame> gPerFrame : register(b1);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles) {
        // Keep transparent particles alive until their slot can be returned to the free list.
        if (gParticles[particleIndex].lifeTime > 0.0f) {
            float dt = gPerFrame.deltaTime * gEmitter.timeScale;
            // 加速度を速度に適用
            gParticles[particleIndex].velocity += gEmitter.acceleration * dt;
            
            // Existing particle JSONs author velocity as per-60fps-frame movement.
            gParticles[particleIndex].translate += gParticles[particleIndex].velocity * (dt * 60.0f);
            gParticles[particleIndex].rotation += gParticles[particleIndex].angularVelocity * dt;
            gParticles[particleIndex].currentTime += dt;
            float lifeRate = saturate(gParticles[particleIndex].currentTime / max(gParticles[particleIndex].lifeTime, 0.001f));
            gParticles[particleIndex].color = lerp(gEmitter.startColor, gEmitter.endColor, lifeRate);
            gParticles[particleIndex].scale = lerp(gEmitter.scaleStart, gEmitter.scaleEnd, lifeRate);

            if (gParticles[particleIndex].currentTime >= gParticles[particleIndex].lifeTime) {
                gParticles[particleIndex].lifeTime = 0.0f;
                gParticles[particleIndex].color.a = 0.0f;
                gParticles[particleIndex].scale = float3(0.0f, 0.0f, 0.0f);
                int freeListIndex;
                InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
                if ((freeListIndex + 1) < kMaxParticles) {
                    gFreeList[freeListIndex + 1] = particleIndex;
                } else {
                    InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
                }
            }
        }
    }
}
