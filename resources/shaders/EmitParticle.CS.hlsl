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
    uint shapeType; // 形状
    float shapeAngle; // 角度

    float3 shapeSize; // Boxのサイズ
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
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);

static const uint kMaxParticles = 1024;

// ====================
// 乱数生成
// ====================
float rand3dTo1d(float3 value, float3 dotDir = float3(12.9898, 78.233, 37.719)) {
    float3 smallValue = sin(value);
    float random = dot(smallValue, dotDir);
    random = frac(sin(random) * 143758.5453);
    return random;
}

float3 rand3dTo3d(float3 value) {
    return float3(
        rand3dTo1d(value, float3(12.989, 78.233, 37.719)),
        rand3dTo1d(value, float3(39.346, 11.135, 83.155)),
        rand3dTo1d(value, float3(73.156, 52.235, 09.151))
    );
}

class RandomGenerator {
    float3 seed;
    float3 Generate3d() {
        seed = rand3dTo3d(seed);
        return seed;
    }
    float Generate1d() {
        float result = rand3dTo1d(seed);
        seed.x = result;
        return result;
    }
};

struct PerFrame {
    float time;
    float deltaTime;
};
ConstantBuffer<PerFrame> gPerFrame : register(b1);
// ====================

// 今回スレッド数は1。複数のEmitterを扱い、同時に処理したいような場合は適宜スレッド数を増やすと良い
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (gEmitter.emit != 0) { // 射出許可が出たので射出
        
        RandomGenerator generator;
        float3 seedOffset = float3(gEmitter.randomSeed & 0xffffu, gEmitter.randomSeed >> 16u, 0.0f);
        generator.seed = gEmitter.translate + float3(1.0f, 2.0f, 3.0f) + seedOffset * 0.03125f;
        bool spreadAroundAxis = gEmitter.randomSeed != 0 && gEmitter.count > 1;
        float phase = 0.0f;
        if (spreadAroundAxis) {
            phase = generator.Generate1d();
        }
        float axisLength = length(gEmitter.velocityBase);
        float3 emissionDirection = axisLength > 0.001f ? gEmitter.velocityBase / axisLength : float3(0.0f, 1.0f, 0.0f);

        for (uint countIndex = 0; countIndex < gEmitter.count; ++countIndex) {
            int freeListIndex;
            InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);

            if (0 <= freeListIndex && freeListIndex < kMaxParticles) {
                uint particleIndex = gFreeList[freeListIndex];
                
                // カウント分Particleを創出する
                gParticles[particleIndex].scale = gEmitter.scaleStart;
                gParticles[particleIndex].emissionDirection = emissionDirection;
                
                float3 randomDir = (generator.Generate3d() - 0.5f) * 2.0f; // -1~1のキューブ内
                float3 randomDirNorm = normalize(randomDir);
                float speedRange = max(0.0f, gEmitter.speedMax - gEmitter.speedMin);
                float randomSpeed = gEmitter.speedMin + generator.Generate1d() * speedRange;

                // --- 形状による位置と速度の決定 ---
                if (gEmitter.shapeType == 0) {
                    // Sphere (球)
                    gParticles[particleIndex].translate = gEmitter.translate + (randomDirNorm * gEmitter.radius * generator.Generate1d());
                    gParticles[particleIndex].velocity = gEmitter.velocityBase + randomDirNorm * randomSpeed;

                } else if (gEmitter.shapeType == 1) {
                    // Cone (円錐)
                    // 位置は少し散らす
                    gParticles[particleIndex].translate = gEmitter.translate + (randomDirNorm * gEmitter.radius * generator.Generate1d());
                    
                    // 方向の計算
                    float3 baseDir = emissionDirection;

                    float3 up = abs(baseDir.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
                    float3 right = normalize(cross(up, baseDir));
                    up = cross(baseDir, right);

                    float r1 = generator.Generate1d();
                    float r2 = generator.Generate1d();
                    if (spreadAroundAxis) {
                        r2 = frac(phase + (countIndex + r2 * 0.65f) / gEmitter.count);
                    }

                    float cosTheta = lerp(cos(gEmitter.shapeAngle), 1.0f, r1);
                    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
                    float phi = 2.0f * 3.14159265f * r2;

                    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
                    float3 worldDir = localDir.x * right + localDir.y * up + localDir.z * baseDir;

                    // 速度 = (基本スピード + 分散) * Cone方向
                    gParticles[particleIndex].velocity = worldDir * randomSpeed;

                } else if (gEmitter.shapeType == 2) {
                    // Box (箱)
                    // -0.5 ~ 0.5 の乱数にサイズを掛ける
                    float3 boxRandom = generator.Generate3d() - 0.5f;
                    gParticles[particleIndex].translate = gEmitter.translate + (boxRandom * gEmitter.shapeSize);
                    gParticles[particleIndex].velocity = gEmitter.velocityBase + randomDirNorm * randomSpeed;
                } else {
                    // Default
                    gParticles[particleIndex].translate = gEmitter.translate;
                    gParticles[particleIndex].velocity = gEmitter.velocityBase + randomDirNorm * randomSpeed;
                }

                // カラー設定
                gParticles[particleIndex].color = gEmitter.startColor;
                float rotation = generator.Generate1d();
                if (spreadAroundAxis) {
                    rotation = frac(phase + (countIndex + rotation * 0.65f) / gEmitter.count);
                }
                gParticles[particleIndex].rotation = radians(gEmitter.rotationStartDeg + rotation * gEmitter.rotationRandomDeg);
                float angularVelocityRange = gEmitter.angularVelocityMaxDeg - gEmitter.angularVelocityMinDeg;
                gParticles[particleIndex].angularVelocity = radians(gEmitter.angularVelocityMinDeg + generator.Generate1d() * angularVelocityRange);
                
                float initialAge = max(0.0f, gEmitter.initialAge * gEmitter.timeScale);
                
                // 寿命: Min ~ Max の間
                float lifeTimeRange = gEmitter.lifeTimeMax - gEmitter.lifeTimeMin;
                gParticles[particleIndex].lifeTime = max(0.001f, gEmitter.lifeTimeMin + generator.Generate1d() * lifeTimeRange);
                gParticles[particleIndex].currentTime = min(initialAge, gParticles[particleIndex].lifeTime);

                const float authoredFrameRate = 60.0f;
                float authoredFrames = initialAge * authoredFrameRate;
                gParticles[particleIndex].translate += gParticles[particleIndex].velocity * authoredFrames;
                gParticles[particleIndex].translate += gEmitter.acceleration * (0.5f * authoredFrameRate * initialAge * initialAge);
                gParticles[particleIndex].velocity += gEmitter.acceleration * initialAge;
                gParticles[particleIndex].rotation += gParticles[particleIndex].angularVelocity * initialAge;

                float lifeRate = saturate(gParticles[particleIndex].currentTime / max(gParticles[particleIndex].lifeTime, 0.001f));
                gParticles[particleIndex].color = lerp(gEmitter.startColor, gEmitter.endColor, lifeRate);
                gParticles[particleIndex].scale = lerp(gEmitter.scaleStart, gEmitter.scaleEnd, lifeRate);
            } else {
                InterlockedAdd(gFreeListIndex[0], 1);
                break;
            }
        }
    }
}
