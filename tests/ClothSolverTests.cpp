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
    std::cout<<"PASS: pinning, return, distance, sphere/axis/degenerate capsule collision, moving leg, enable reset, teleport, invalid dt, fixed timestep\n";
}
