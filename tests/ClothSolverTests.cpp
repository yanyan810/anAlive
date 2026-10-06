#include "../Engine/Physics/ClothSolver.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

float Length(Vector3 p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); }
ClothSolver Chain() {
    ClothSolver s;
    PhysicsParticle a; a.inverseMass=0;
    PhysicsParticle b; b.target={0,-1,0};
    s.particles={a,b}; s.constraints={{0,1,1,1}}; s.Reset(); return s;
}
int main() {
    PhysicsCollider sphere; sphere.shape=PhysicsCollider::Shape::Sphere; sphere.radius=1;
    PhysicsParticle p; p.position={0,0,0}; p.previous=p.position;
    assert(sphere.Project(p) && Length(p.position)>=1.014f);
    p.inverseMass=0; p.position={0,0,0}; assert(!sphere.Project(p));
    PhysicsCollider capsule; capsule.a={0,-1,0}; capsule.b={0,1,0}; capsule.radius=.5f;
    p.inverseMass=1; p.position={0,0,0}; p.previous=p.position;
    assert(capsule.Project(p)); assert(Length(p.position-capsule.ClosestPoint(p.position))>=.514f);
    capsule.b=capsule.a; p.position=capsule.a; assert(capsule.Project(p));
    auto s=Chain(); s.particles[1].position={.4f,-.9f,0}; s.particles[1].previous=s.particles[1].position;
    for(int i=0;i<600;++i) s.Update(1.0f/60,{});
    assert(Length(s.particles[0].position)==0);
    assert(std::abs(Length(s.particles[1].position)-1)<.002f);
    assert(std::abs(s.particles[1].position.x)<.01f);
    s.settings.enabled=false; s.particles[0].target={1,0,0}; s.Update(1.0f/60,{});
    assert(Length(s.particles[0].position-Vector3{1,0,0})==0);
    s.settings.enabled=true; s.particles[0].target={100,0,0}; s.particles[1].target={100,-1,0}; s.Update(1.0f/60,{});
    assert(Length(s.particles[1].position-Vector3{100,-1,0})<.01f);
    const auto before=s.particles[1].position;
    s.Update(std::numeric_limits<float>::quiet_NaN(),{}); s.Update(-1,{});
    assert(Length(s.particles[1].position-before)==0);
    auto a=Chain(),b=Chain();
    for(int i=0;i<120;++i) a.Update(1.0f/30,{});
    for(int i=0;i<240;++i) b.Update(1.0f/60,{});
    assert(Length(a.particles[1].position-b.particles[1].position)<.001f);
    auto highFps=Chain();
    highFps.particles[0].target={.01f,0,0}; highFps.Update(1.0f/240,{});
    assert(Length(highFps.particles[0].position-highFps.particles[0].target)==0);
    // A moving leg pushes a free particle outward without affecting its fixed waist.
    auto contact=Chain(); capsule.a={-.2f,-1,0}; capsule.b={-.2f,-2,0}; capsule.radius=.3f;
    capsule.previousA=capsule.a; capsule.previousB=capsule.b;
    contact.Update(1.0f/60,{capsule});
    assert(contact.contacts>0 && contact.particles[1].position.x>.05f);
    assert(Length(contact.particles[1].position-capsule.ClosestPoint(contact.particles[1].position))>=.314f);
    // Endpoints are outside the collider, but the interpolated surface is inside.
    sphere.radius=.5f;
    PhysicsParticle left,right;
    left.position=left.previous=left.target={-1,.1f,0};
    right.position=right.previous=right.target={1,.1f,0};
    std::vector<PhysicsParticle> ends{left,right};
    PhysicsCollisionSample sample; sample.a=0; sample.b=1; sample.t=.5f;
    assert(!sphere.Project(ends[0]) && !sphere.Project(ends[1]));
    assert(sample.Project(sphere,ends,.02f));
    const auto center=(ends[0].position+ends[1].position)*.5f;
    assert(Length(center)>=.5199f && sample.contacted);
    assert(Length((ends[0].position-left.position)-(ends[1].position-right.position))<.0001f);
    assert(Length(ends[0].position-ends[0].previous)<.0001f); // No correction-induced velocity.
    // Off-center sample: endpoints receive the barycentric 3:1 ratio.
    left.position=left.previous=left.target={-.5f,.1f,0};
    right.position=right.previous=right.target={1.5f,.1f,0};
    ends={left,right}; sample.t=.25f;
    assert(sample.Project(sphere,ends,0));
    assert(std::abs(Length(ends[0].position-left.position)/Length(ends[1].position-right.position)-3)<.0001f);
    assert(Length(ends[0].position*.75f+ends[1].position*.25f)>=.4999f);
    // Pinning renormalizes the movable endpoint rather than dropping half the correction.
    left.inverseMass=0; ends={left,right};
    assert(sample.Project(sphere,ends,0));
    assert(Length(ends[0].position-left.position)==0 && Length(ends[0].previous-left.previous)==0);
    assert(Length(ends[0].position*.75f+ends[1].position*.25f)>=.4999f);
    ends[1].inverseMass=0; ends[1].position=right.position;
    assert(!sample.Project(sphere,ends,0));
    // The same segment mechanism applies to capsules, including an axis hit.
    ends={PhysicsParticle{},PhysicsParticle{}};
    ends[0].position=ends[0].previous=ends[0].target={-1,0,0};
    ends[1].position=ends[1].previous=ends[1].target={1,0,0};
    capsule.a={0,-1,0}; capsule.b={0,1,0}; capsule.radius=.4f; sample.t=.5f;
    assert(sample.Project(capsule,ends,.01f));
    const auto midpoint=(ends[0].position+ends[1].position)*.5f;
    assert(Length(midpoint-capsule.ClosestPoint(midpoint))>=.4099f);
    ClothSolver bridge; bridge.particles.resize(2);
    bridge.particles[0].target={-1,0,0}; bridge.particles[1].target={1,0,0};
    bridge.settings.gravity={0,0,0}; bridge.settings.stiffness=0;
    bridge.settings.collisionSamplesPerSegment=1; bridge.settings.collisionSampleRadius=.01f;
    bridge.collisionSegments={{0,1,false}};
    capsule.previousA=capsule.a; capsule.previousB=capsule.b;
    bridge.Update(1.0f/120,{capsule});
    assert(bridge.sampleContacts>0 && bridge.contacts==0);
    const auto bridgeCenter=(bridge.particles[0].position+bridge.particles[1].position)*.5f;
    assert(Length(bridgeCenter-capsule.ClosestPoint(bridgeCenter))>=.4099f);
    // Generation includes only selected segments, never the bending diagonals.
    auto sampled=Chain(); sampled.collisionSegments={{0,1,false},{0,1,true}};
    sampled.settings.collisionSamplesPerSegment=2; sampled.RebuildCollisionSamples();
    assert(sampled.collisionSamples.size()==2);
    assert(std::abs(sampled.collisionSamples[0].t-1.0f/3)<.0001f);
    sampled.settings.enableHorizontalCollisionSamples=true; sampled.Update(0,{});
    assert(sampled.collisionSamples.size()==4 && sampled.collisionSamples[2].horizontal);
    sampled.settings.collisionSamplesPerSegment=3; sampled.Update(0,{});
    assert(sampled.collisionSamples.size()==6);
    sampled.settings.enabled=false; sampled.Update(1.0f/60,{capsule});
    assert(sampled.sampleContacts==0);
    // Zero samples is numerically identical to the previous solver, even with segment topology present.
    auto legacy=Chain(),compatible=Chain(); compatible.collisionSegments={{0,1,false}};
    for(int i=0;i<120;++i) { legacy.Update(1.0f/60,{capsule}); compatible.Update(1.0f/60,{capsule}); }
    assert(Length(legacy.particles[1].position-compatible.particles[1].position)==0);
    // Uniform reference-frame translation must not create artificial drag.
    ClothSolver translated; translated.particles.resize(1);
    translated.settings.gravity={0,0,0}; translated.settings.stiffness=0;
    translated.settings.damping=.5f; translated.settings.dampingRelativeToAnimation=true;
    translated.Reset(); translated.particles[0].previous={-.01f,0,0};
    for(int frame=1;frame<=120;++frame) {
        translated.particles[0].target={frame*.01f,0,0};
        translated.Update(1.0f/120,{});
        assert(Length(translated.particles[0].position-translated.particles[0].target)<.00001f);
    }
    // Accelerating the reference still leaves inertia; it does not teleport the particle.
    translated.particles[0].target.x+=.03f;
    translated.Update(1.0f/120,{});
    assert(translated.particles[0].position.x<translated.particles[0].target.x-.005f);
    auto limited=Chain(); limited.settings.gravity={0,0,0}; limited.settings.stiffness=0;
    limited.settings.maxSwingAngleDegrees=20; limited.collisionSegments={{0,1,false}};
    limited.particles[1].position=limited.particles[1].previous={1,0,0};
    limited.Update(1.0f/120,{});
    assert(Length(limited.particles[0].position)==0 && std::abs(Length(limited.particles[1].position)-1)<.0001f);
    assert(limited.particles[1].position.y<-.939f && std::abs(limited.particles[1].position.x)<.343f);
    assert(Length(limited.particles[1].position-limited.particles[1].previous)<.0001f);
    // The cone follows the animated direction rather than a world-space axis.
    limited.particles[1].target={-1,0,0}; limited.Update(1.0f/120,{});
    assert(limited.particles[1].position.x<-.939f && std::abs(limited.particles[1].position.y)<.343f);
    limited.particles[1].target={0,-1,0}; limited.particles[1].position=limited.particles[1].previous={0,1,0};
    limited.Update(1.0f/120,{});
    assert(std::isfinite(limited.particles[1].position.x) && limited.particles[1].position.y<-.939f);
    std::cout<<"PASS: legacy cloth, segment contacts, fixed endpoints, velocity history, configurable samples, compatibility, moving-frame damping, acceleration inertia, animated swing limits and antiparallel axes\n";
}
