#include "ClothSolver.h"
#include <algorithm>
#include <cmath>

namespace {
float Dot(const Vector3& a,const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float Length(const Vector3& a) { return std::sqrt(Dot(a,a)); }
Vector3 Lerp(const Vector3& a,const Vector3& b,float t) { return a+(b-a)*t; }
}

Vector3 PhysicsCollider::ClosestPoint(const Vector3& point) const {
    if (shape == Shape::Sphere) return a;
    const auto axis=b-a;
    const float length2=Dot(axis,axis);
    return a+axis*(length2>1e-10f ? std::clamp(Dot(point-a,axis)/length2,0.0f,1.0f) : 0);
}

bool PhysicsCollider::Project(PhysicsParticle& p) const {
    if (!enabled || p.inverseMass<=0 || radius<=0) return false;
    const auto center=ClosestPoint(p.position);
    auto delta=p.position-center;
    const float length=Length(delta), limit=radius+p.radius;
    if (length>=limit) return false;
    if (length<1e-6f) {
        // An exact center/axis hit still has a deterministic outward direction.
        delta=p.target-ClosestPoint(p.target);
        if (Length(delta)<1e-6f) {
            const auto axis=b-a;
            delta=std::abs(axis.x)<std::abs(axis.y) ? Vector3{0,-axis.z,axis.y} : Vector3{-axis.z,0,axis.x};
            if (Length(delta)<1e-6f) delta={1,0,0};
        }
    }
    const auto normal=delta*(1.0f/Length(delta));
    const auto velocity=p.position-p.previous;
    p.position=center+normal*limit;
    // Retain tangent motion, eliminate velocity into the surface and add mild friction.
    const auto tangent=velocity-normal*std::min(0.0f,Dot(velocity,normal));
    p.previous=p.position-tangent*0.92f;
    return true;
}

void ClothSolver::Reset() {
    for (auto& p:particles) p.position=p.previous=p.frameTarget=p.target;
    accumulator_=0; initialized_=true; wasEnabled_=settings.enabled; contacts=0;
}

void ClothSolver::Update(float dt,const std::vector<PhysicsCollider>& colliders) {
    if (!std::isfinite(dt) || dt<0) return;
    if (!initialized_ || settings.enabled!=wasEnabled_) Reset();
    if (!settings.enabled) { Reset(); return; }
    if (dt==0) {
        for (auto& p:particles) if (p.inverseMass<=0) p.position=p.previous=p.target;
        return;
    }
    // Teleports must not leave cloth at the previous location.
    for (const auto& p:particles) if (p.inverseMass==0 && Length(p.target-p.frameTarget)>2) { Reset(); break; }
    constexpr float step=1.0f/120.0f;
    accumulator_+=std::min(dt,0.1f);
    const int steps=std::min(12,static_cast<int>(accumulator_/step));
    if (steps==0) for (auto& p:particles) if (p.inverseMass<=0) p.position=p.previous=p.target;
    contacts=0;
    const int iterations=std::clamp(settings.iterations,1,32);
    const float stiffness=std::clamp(settings.stiffness,0.0f,1.0f);
    const float damping=std::pow(1-std::clamp(settings.damping,0.0f,1.0f),step*60);
    for (int s=0;s<steps;++s) {
        const float t=static_cast<float>(s+1)/static_cast<float>(steps);
        for (auto& p:particles) {
            const auto target=Lerp(p.frameTarget,p.target,t);
            if (p.inverseMass<=0) { p.position=p.previous=target; continue; }
            const auto velocity=(p.position-p.previous)*damping;
            p.previous=p.position;
            p.position+=velocity+(settings.gravity+(target-p.position)*(stiffness*180))* (step*step);
        }
        for (int i=0;i<iterations;++i) {
            for (const auto& c:constraints) {
                if (c.a>=particles.size() || c.b>=particles.size()) continue;
                auto& a=particles[c.a]; auto& b=particles[c.b];
                const auto delta=b.position-a.position;
                const float length=Length(delta), mass=a.inverseMass+b.inverseMass;
                if (length<1e-7f || mass<=0) continue;
                const float k=1-std::pow(1-std::clamp(c.stiffness,0.0f,1.0f),1.0f/static_cast<float>(iterations));
                const auto correction=delta*((length-c.restLength)/length*k/mass);
                a.position+=correction*a.inverseMass;
                b.position-=correction*b.inverseMass;
            }
            for (const auto& source:colliders) {
                auto collider=source;
                collider.a=Lerp(source.previousA,source.a,t);
                collider.b=Lerp(source.previousB,source.b,t);
                for (auto& p:particles) if (collider.Project(p)) ++contacts;
            }
        }
        accumulator_-=step;
    }
    if (steps>0) for (auto& p:particles) p.frameTarget=p.target;
}
