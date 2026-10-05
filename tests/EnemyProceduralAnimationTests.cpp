#include "EnemyProceduralAnimation.h"
#include <cassert>
#include <iostream>

static bool Near(float a, float b) { return std::abs(a-b) < 2e-4f; }
static void SameMatrix(const Matrix4x4& a, const Matrix4x4& b) {
    for (int row=0;row<4;++row) for (int col=0;col<4;++col) assert(Near(a.m[row][col],b.m[row][col]));
}
static Matrix4x4 Render(const EnemyProceduralAnimation& animation,
    const EnemyProceduralAnimation::Pose& pose, const EnemyPart* part,
    const Vector3& scale, const Vector3& rotation, const Vector3& position) {
    const auto transform=animation.TransformFor(pose,part,scale,rotation,position);
    return Matrix4x4::MakeAffineMatrix(scale,transform.rotate,transform.translate);
}
int main() {
    auto parts=MakeEnemyParts({{-.3f,0,-1},{.3f,2.5f,1}});
    const auto original=parts;
    EnemyProceduralAnimation animation;
    using Mode=EnemyLocomotionPose;
    assert(animation.Classify(parts)==Mode::Normal);
    // Both wrists drop below their shoulders even with a stationary phase.
    const auto idle=animation.Sample(parts,{1,1,1},true);
    for (size_t i : {size_t{2},size_t{3}}) {
        const auto& arm=parts[i];
        const auto center=(arm.bounds.min+arm.bounds.max)*.5f;
        const auto lowered=Render(animation,idle,&arm,{1,1,1},{},{});
        const auto point=EnemyPartTransformPoint(center,lowered);
        assert(point.y<center.y-.1f);
        const auto leftOrRight=i==2 ? 1.0f : -1.0f;
        assert(std::abs(point.z-leftOrRight*.38f)<std::abs(center.z-leftOrRight*.38f));
        animation.settings.enabled=false;
        SameMatrix(Render(animation,animation.Sample(parts,{1,1,1},true),&arm,{1,1,1},{},{}),Matrix4x4::MakeIdentity4x4());
        animation.settings.enabled=true;
    }
    const Vector3 scale{.6f,2,1.4f}, position{3,0,9};
    animation.Advance(.125f,true,parts);
    const auto walkingArms=animation.Sample(parts,{1,1,1},true);
    const auto leftCenter=(parts[2].bounds.min+parts[2].bounds.max)*.5f;
    const auto rightCenter=(parts[3].bounds.min+parts[3].bounds.max)*.5f;
    const auto leftPoint=EnemyPartTransformPoint(leftCenter,Render(animation,walkingArms,&parts[2],{1,1,1},{},{}));
    const auto rightPoint=EnemyPartTransformPoint(rightCenter,Render(animation,walkingArms,&parts[3],{1,1,1},{},{}));
    assert(leftPoint.x<leftCenter.x && rightPoint.x>rightCenter.x); // Opposing forward/back swings.
    animation.Reset();
    // Arbitrary facing, pitch/roll, nonuniform scale, both Euler singularities.
    for (const Vector3 rotation : {Vector3{0,0,0}, Vector3{.3f,-.8f,.2f},
        Vector3{.4f,1.57079632679f,.2f}, Vector3{-.3f,-1.57079632679f,.8f}}) {
        const auto base=Matrix4x4::MakeAffineMatrix(scale,rotation,position);
        auto pose=animation.Sample(parts,scale,true);
        SameMatrix(Render(animation,pose,nullptr,scale,rotation,position),base);
        animation.Advance(.125f,true,parts);
        pose=animation.Sample(parts,scale,true);
        const auto animated=Render(animation,pose,nullptr,scale,rotation,position);
        SameMatrix(animated,Matrix4x4::Multiply(Matrix4x4::Multiply(Matrix4x4::Scale(scale),pose.root),
            Matrix4x4::MakeAffineMatrix({1,1,1},rotation,position)));
        // Shoulder/hip pivots remain attached to the same root as the torso.
        for (size_t i=2;i<parts.size();++i) {
            const auto& part=parts[i];
            auto pivot=(part.bounds.min+part.bounds.max)*.5f;
            if (part.role==EnemyPartRole::Leg) pivot.y=part.bounds.max.y;
            else pivot.z=part.type==EnemyPartType::LeftArm ? part.bounds.min.z : part.bounds.max.z;
            const auto rootPoint=EnemyPartTransformPoint(pivot,animated);
            const auto limbPoint=EnemyPartTransformPoint(pivot,Render(animation,pose,&part,scale,rotation,position));
            assert(Near(rootPoint.x,limbPoint.x) && Near(rootPoint.y,limbPoint.y) && Near(rootPoint.z,limbPoint.z));
        }
        // Re-evaluation and dt=0 cannot accumulate offsets or advance phase.
        for (int frame=0;frame<1000;++frame) {
            animation.Advance(0,false,parts);
            SameMatrix(Render(animation,animation.Sample(parts,scale,true),nullptr,scale,rotation,position),animated);
        }
        animation.Advance(.1f,false,parts);
        SameMatrix(Render(animation,animation.Sample(parts,scale,true),nullptr,scale,rotation,position),base);
        animation.Reset();
    }
    DamageEnemyPart(parts,EnemyPartType::LeftArm,1000);
    assert(animation.Classify(parts)==Mode::Normal);
    DamageEnemyPart(parts,EnemyPartType::LeftLeg,1000);
    assert(animation.Classify(parts)==Mode::MissingLeftLeg);
    const auto leftPose=animation.Sample(parts,{1,1,1},true);
    assert(leftPose.root.m[1][2]>0); // Up leans toward the missing +Z side.
    auto rightMissing=original;
    DamageEnemyPart(rightMissing,EnemyPartType::RightLeg,1000);
    assert(animation.Classify(rightMissing)==Mode::MissingRightLeg);
    assert(animation.Sample(rightMissing,{1,1,1},true).root.m[1][2]<0);
    DamageEnemyPart(parts,EnemyPartType::RightLeg,1000);
    assert(animation.Classify(parts)==Mode::Crawl);
    auto crawl=animation.Sample(parts,{1,1,1},true);
    assert(crawl.root.m[1][0]<-.95f && crawl.root.m[1][1]<.15f); // Torso nearly horizontal, front down.
    auto crawlParts=original;
    DamageEnemyPart(crawlParts,EnemyPartType::LeftLeg,1000);
    DamageEnemyPart(crawlParts,EnemyPartType::RightLeg,1000);
    const Vector3 crawlScale{.6f,2,1.4f};
    // A full cycle with two, one, and zero surviving arms. The visual pose
    // remains above the base plane and every shoulder stays attached.
    for (int armCount=2;armCount>=0;--armCount) {
        for (int frame=0;frame<72;++frame) {
            animation.Advance(1.0f/60,true,crawlParts);
            const auto pose=animation.Sample(crawlParts,crawlScale,true);
            const auto root=Render(animation,pose,nullptr,crawlScale,{},{});
            float lowest=std::numeric_limits<float>::max();
            for (const auto& part:crawlParts) {
                if (part.Destroyed()) continue;
                const auto matrix=Render(animation,pose,&part,crawlScale,{},{});
                for (int corner=0;corner<8;++corner) {
                    const Vector3 point{(corner&1)?part.bounds.max.x:part.bounds.min.x,
                        (corner&2)?part.bounds.max.y:part.bounds.min.y,(corner&4)?part.bounds.max.z:part.bounds.min.z};
                    lowest=std::min(lowest,EnemyPartTransformPoint(point,matrix).y);
                }
                if (part.role==EnemyPartRole::Arm) {
                    auto shoulder=(part.bounds.min+part.bounds.max)*.5f;
                    shoulder.z=part.type==EnemyPartType::LeftArm?part.bounds.min.z:part.bounds.max.z;
                    const auto attached=EnemyPartTransformPoint(shoulder,root);
                    const auto animated=EnemyPartTransformPoint(shoulder,matrix);
                    assert(Near(attached.x,animated.x) && Near(attached.y,animated.y) && Near(attached.z,animated.z));
                }
            }
            assert(lowest>=.015f*5-.001f && lowest<.12f);
        }
        if (armCount==2) DamageEnemyPart(crawlParts,EnemyPartType::LeftArm,1000);
        if (armCount==1) DamageEnemyPart(crawlParts,EnemyPartType::RightArm,1000);
    }
    // Alternating crawl arms differ from their stopped support pose.
    crawlParts=original;
    DamageEnemyPart(crawlParts,EnemyPartType::LeftLeg,1000);
    DamageEnemyPart(crawlParts,EnemyPartType::RightLeg,1000);
    animation.Reset();
    const auto stopped=animation.Sample(crawlParts,{1,1,1},true);
    animation.Advance(.125f,true,crawlParts);
    const auto stroke=animation.Sample(crawlParts,{1,1,1},true);
    for (size_t i : {size_t{2},size_t{3}}) {
        const auto center=(crawlParts[i].bounds.min+crawlParts[i].bounds.max)*.5f;
        const auto resting=EnemyPartTransformPoint(center,Render(animation,stopped,&crawlParts[i],{1,1,1},{},{}));
        const auto reaching=EnemyPartTransformPoint(center,Render(animation,stroke,&crawlParts[i],{1,1,1},{},{}));
        assert((i==2) ? reaching.x<resting.x : reaching.x>resting.x);
    }
    crawl=animation.Sample(parts,scale,true);
    // Destroyed arms are never given a new resting rotation.
    SameMatrix(Render(animation,crawl,&parts[2],scale,{},position),Render(animation,crawl,nullptr,scale,{},position));
    animation.settings.enabled=false;
    SameMatrix(Render(animation,animation.Sample(parts,scale,true),nullptr,scale,{},position),
        Matrix4x4::MakeAffineMatrix(scale,{},position));
    animation.settings.enabled=true;
    SameMatrix(Render(animation,animation.Sample(parts,scale,false),nullptr,scale,{},position),
        Matrix4x4::MakeAffineMatrix(scale,{},position));
    animation.Reset();
    assert(animation.Sample(original,scale,true).swing==0);
    animation.Advance(std::numeric_limits<float>::infinity(),true,original);
    animation.Advance(-1,true,original);
    assert(animation.Sample(original,scale,true).swing==0);
    // Legacy generic Leg tags must not impersonate a canonical left/right pair.
    auto generic=original;
    generic[4].name="LegArmorA"; generic[5].name="LegArmorB";
    generic[4].type=generic[5].type=EnemyPartType::LeftLeg;
    assert(animation.Classify(generic)==Mode::Normal);
    assert(animation.Classify({})==Mode::Normal);
    // Animation never writes HP, geometry bounds or collision transforms.
    for (size_t i=0;i<original.size();++i) assert(original[i].hp==original[i].maxHp);
    std::cout << "PASS: lowered stationary arms, prone crawl and alternating arms, base-plane clearance with 2/1/0 arms, locomotion modes, side/facing, pivots, scale, zero-time refresh, no drift, stop/reset, dead/disabled, generic parts\n";
}
