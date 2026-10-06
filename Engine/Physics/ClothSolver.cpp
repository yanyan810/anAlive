#include "ClothSolver.h"
#include <algorithm>
#include <cmath>

namespace {
float Dot(const Vector3& a,const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a,const Vector3& b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float Length(const Vector3& a) { return std::sqrt(Dot(a,a)); }
Vector3 Lerp(const Vector3& a,const Vector3& b,float t) { return a+(b-a)*t; }
bool OutsideBounds(const PhysicsCollider& c,Vector3 point,float limit) {
    return point.x<std::min(c.a.x,c.b.x)-limit || point.x>std::max(c.a.x,c.b.x)+limit ||
           point.y<std::min(c.a.y,c.b.y)-limit || point.y>std::max(c.a.y,c.b.y)+limit ||
           point.z<std::min(c.a.z,c.b.z)-limit || point.z>std::max(c.a.z,c.b.z)+limit;
}
}

Vector3 PhysicsCollider::ClosestPoint(const Vector3& point) const {
    if (shape == Shape::Sphere) return a;
    const auto axis=b-a;
    const float length2=Dot(axis,axis);
    return a+axis*(length2>1e-10f ? std::clamp(Dot(point-a,axis)/length2,0.0f,1.0f) : 0);
}

bool PhysicsCollider::Project(PhysicsParticle& p) const {
    if (!enabled || p.inverseMass<=0 || radius<=0) return false;
    const float limit=radius+p.radius;
    // Endpoints enclose the complete capsule: reject only definite misses.
    if(OutsideBounds(*this,p.position,limit)) return false;
    const auto center=ClosestPoint(p.position);
    auto delta=p.position-center;
    const float length2=Dot(delta,delta);
    if (length2>=limit*limit) return false;
    const float length=std::sqrt(length2);
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

bool PhysicsCollisionSample::Project(const PhysicsCollider& collider,std::vector<PhysicsParticle>& particles,float radius) {
    if(!collider.enabled || collider.radius<=0) return false;
    if (a>=particles.size() || b>=particles.size() || a==b) return false;
    auto& pa=particles[a]; auto& pb=particles[b];
    const float u=1-t;
    const float massA=std::max(0.0f,pa.inverseMass), massB=std::max(0.0f,pb.inverseMass);
    // Effective inverse mass of the interpolated point. Normalization makes its
    // displacement exactly the contact correction even with one pinned endpoint.
    const float mass=massA*u*u+massB*t*t;
    if (mass<=1e-8f) return false;
    PhysicsParticle sample;
    sample.position=Lerp(pa.position,pb.position,t);
    sample.radius=std::max(0.0f,radius);
    // Defer velocity/target interpolation until a hit; most samples miss most
    // body colliders every iteration.
    const float limit=collider.radius+sample.radius;
    if(OutsideBounds(collider,sample.position,limit)) return false;
    const auto deltaToAxis=sample.position-collider.ClosestPoint(sample.position);
    if(Dot(deltaToAxis,deltaToAxis)>=limit*limit) return false;
    sample.previous=Lerp(pa.previous,pb.previous,t);
    sample.target=Lerp(pa.target,pb.target,t);
    const auto before=sample.position, beforePrevious=sample.previous;
    if (!collider.Project(sample)) return false;
    const auto delta=sample.position-before, previousDelta=sample.previous-beforePrevious;
    const float weightA=massA*u/mass, weightB=massB*t/mass;
    pa.position+=delta*weightA; pb.position+=delta*weightB;
    // Carry the contact velocity/friction correction back too; positional repair
    // alone would inject a large artificial Verlet velocity on the next step.
    pa.previous+=previousDelta*weightA; pb.previous+=previousDelta*weightB;
    position=sample.position; contacted=true;
    return true;
}

void ClothSolver::RebuildCollisionSamples() {
    settings.collisionSamplesPerSegment=std::clamp(settings.collisionSamplesPerSegment,0,8);
    sampleCount_=settings.collisionSamplesPerSegment;
    horizontalSamples_=settings.enableHorizontalCollisionSamples;
    segmentCount_=collisionSegments.size();
    collisionSamples.clear(); sampleContacts=0;
    for (const auto& segment:collisionSegments) {
        if (segment.a>=particles.size() || segment.b>=particles.size() || segment.a==segment.b ||
            (segment.horizontal && !horizontalSamples_)) continue;
        for (int i=0;i<sampleCount_;++i) {
            PhysicsCollisionSample sample;
            sample.a=segment.a; sample.b=segment.b; sample.horizontal=segment.horizontal;
            sample.t=static_cast<float>(i+1)/static_cast<float>(sampleCount_+1);
            collisionSamples.push_back(sample);
        }
    }
    RefreshCollisionSamples();
}

void ClothSolver::RefreshCollisionSamples() {
    for (auto& sample:collisionSamples) sample.position=Lerp(particles[sample.a].position,particles[sample.b].position,sample.t);
}

void ClothSolver::Reset() {
    for (auto& p:particles) p.position=p.previous=p.frameTarget=p.target;
    accumulator_=0; initialized_=true; wasEnabled_=settings.enabled; contacts=sampleContacts=0;
    for (auto& sample:collisionSamples) sample.contacted=false;
    RefreshCollisionSamples();
}

void ClothSolver::Update(float dt,const std::vector<PhysicsCollider>& colliders) {
    if (!std::isfinite(dt) || dt<0) return;
    if (sampleCount_!=std::clamp(settings.collisionSamplesPerSegment,0,8) ||
        horizontalSamples_!=settings.enableHorizontalCollisionSamples || segmentCount_!=collisionSegments.size())
        RebuildCollisionSamples();
    if (!initialized_ || settings.enabled!=wasEnabled_) Reset();
    if (!settings.enabled) { Reset(); return; }
    if (dt==0) {
        for (auto& p:particles) if (p.inverseMass<=0) p.position=p.previous=p.target;
        RefreshCollisionSamples();
        return;
    }
    // Teleports must not leave cloth at the previous location.
    for (const auto& p:particles) if (p.inverseMass==0 && Length(p.target-p.frameTarget)>2) { Reset(); break; }
    constexpr float step=1.0f/120.0f;
    accumulator_+=std::min(dt,0.1f);
    const int steps=std::min(12,static_cast<int>(accumulator_/step));
    if (steps==0) for (auto& p:particles) if (p.inverseMass<=0) p.position=p.previous=p.target;
    contacts=0;
    sampleContacts=0;
    for (auto& sample:collisionSamples) sample.contacted=false;
    const int iterations=std::clamp(settings.iterations,1,32);
    const float stiffness=std::clamp(settings.stiffness,0.0f,1.0f);
    const float damping=std::pow(1-std::clamp(settings.damping,0.0f,1.0f),step*60);
    const float swingAngle=std::clamp(settings.maxSwingAngleDegrees,0.0f,180.0f)*3.14159265f/180;
    const bool limitSwing=settings.maxSwingAngleDegrees<180;
    const float swingCos=std::cos(swingAngle),swingSin=std::sin(swingAngle);
    // Stiffness is constant across substeps/iterations of this update.
    constraintFactors_.resize(constraints.size());
    float previousStiffness=-1,previousFactor=0;
    for(size_t i=0;i<constraints.size();++i) {
        const float value=std::clamp(constraints[i].stiffness,0.0f,1.0f);
        if(value!=previousStiffness) {
            previousStiffness=value;
            previousFactor=1-std::pow(1-value,1.0f/static_cast<float>(iterations));
        }
        constraintFactors_[i]=previousFactor;
    }
    for (int s=0;s<steps;++s) {
        const float t=static_cast<float>(s+1)/static_cast<float>(steps);
        if(limitSwing) {
            stepSwingAxes_.assign(collisionSegments.size(),{});
            for(size_t index=0;index<collisionSegments.size();++index) {
                const auto& segment=collisionSegments[index];
                if(segment.horizontal || segment.a>=particles.size() || segment.b>=particles.size()) continue;
                const auto& a=particles[segment.a]; const auto& b=particles[segment.b];
                const auto axis=Lerp(b.frameTarget,b.target,t)-Lerp(a.frameTarget,a.target,t);
                const float length=Length(axis);
                if(length>1e-7f) stepSwingAxes_[index]=axis*(1/length);
            }
        }
        stepColliders_.clear();
        for(const auto& source:colliders) if(source.enabled && source.radius>0) {
            auto collider=source;
            collider.a=Lerp(source.previousA,source.a,t);
            collider.b=Lerp(source.previousB,source.b,t);
            stepColliders_.push_back(collider);
        }
        for (auto& p:particles) {
            const auto target=Lerp(p.frameTarget,p.target,t);
            if (p.inverseMass<=0) { p.position=p.previous=target; continue; }
            const auto referenceVelocity=settings.dampingRelativeToAnimation ?
                (p.target-p.frameTarget)*(1.0f/static_cast<float>(steps)) : Vector3{};
            const auto velocity=(p.position-p.previous-referenceVelocity)*damping+referenceVelocity;
            p.previous=p.position;
            p.position+=velocity+(settings.gravity+(target-p.position)*(stiffness*180))* (step*step);
        }
        for (int i=0;i<iterations;++i) {
            for (size_t index=0;index<constraints.size();++index) {
                const auto& c=constraints[index];
                if (c.a>=particles.size() || c.b>=particles.size()) continue;
                auto& a=particles[c.a]; auto& b=particles[c.b];
                const auto delta=b.position-a.position;
                const float length=Length(delta), mass=a.inverseMass+b.inverseMass;
                if (length<1e-7f || mass<=0) continue;
                const float k=constraintFactors_[index];
                const auto correction=delta*((length-c.restLength)/length*k/mass);
                a.position+=correction*a.inverseMass;
                b.position-=correction*b.inverseMass;
            }
            if(limitSwing) for(size_t index=0;index<collisionSegments.size();++index) {
                const auto axis=stepSwingAxes_[index];
                if(Dot(axis,axis)<.5f) continue;
                const auto& segment=collisionSegments[index];
                auto& a=particles[segment.a]; auto& b=particles[segment.b];
                const auto delta=b.position-a.position;
                const float length=Length(delta),mass=a.inverseMass+b.inverseMass;
                if(length<1e-7f || mass<=0) continue;
                const auto direction=delta*(1/length);
                const float cosine=std::clamp(Dot(direction,axis),-1.0f,1.0f);
                if(cosine>=swingCos) continue;
                auto tangent=direction-axis*cosine;
                float tangentLength=Length(tangent);
                if(tangentLength<1e-6f) {
                    tangent=Cross(axis,std::abs(axis.x)<.8f ? Vector3{1,0,0} : Vector3{0,1,0});
                    tangentLength=Length(tangent);
                }
                const auto allowed=(axis*swingCos+tangent*(swingSin/tangentLength))*length;
                const auto correction=(delta-allowed)*(1/mass);
                a.position+=correction*a.inverseMass; b.position-=correction*b.inverseMass;
                // Do not turn a positional angular repair into an impulse.
                a.previous+=correction*a.inverseMass; b.previous-=correction*b.inverseMass;
            }
            for (const auto& collider:stepColliders_) {
                for (auto& p:particles) if (collider.Project(p)) ++contacts;
                for (auto& sample:collisionSamples)
                    if (sample.Project(collider,particles,settings.collisionSampleRadius)) ++sampleContacts;
            }
        }
        accumulator_-=step;
    }
    if (steps>0) for (auto& p:particles) p.frameTarget=p.target;
    RefreshCollisionSamples();
}
