#pragma once
#include "StageLoader.h"
#include "StageProjectile.h"
#include <stdexcept>

namespace ShutterChecks {
inline void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
inline bool Near(float a,float b) { return std::abs(a-b)<.002f; }
inline void Validate(StageLoader& level) {
    auto& system=level.shutters;
    Check(system.shutters.size()==1,"Hotel requires one shutter");
    const auto def=system.shutters[0].definition;
    Check(def.openDuration==30 && Near(def.openDistance,3.2f) && Near(def.interactionDistance,2),"Shutter defaults");
    const Vector3 atButton=def.buttonPosition-Vector3{0,1.25f,0};
    for (const int fps : {30,60,144}) {
        system.Reset(level.collision);
        Check(system.shutters[0].state==ShutterState::Closed && system.TakeEvents().empty(),"Closed initial/reset state");
        Check(!system.TryInteract(atButton+Vector3{2.1f,0,0}) && !system.TryInteract(atButton+Vector3{0,3,0}),"Out-of-range interaction");
        Check(system.Nearest(atButton+Vector3{2,0,0}).has_value(),"Interaction boundary");
        Check(system.TryInteract(atButton) && !system.TryInteract(atButton),"E starts only once");
        auto events=system.TakeEvents();
        Check(events.size()==1 && events[0].type==ShutterEventType::Started,"One start event");
        size_t progressEvents=0,completed=0;
        const float dt=1.0f/static_cast<float>(fps);
        for (int frame=1;frame<=fps*30;++frame) {
            system.Update(dt,level.collision);
            const auto& shutter=system.shutters[0];
            const auto& collider=level.collision.colliders[def.colliderIndex];
            const float expected=def.closedPosition.y+def.openDistance*(static_cast<float>(frame)/static_cast<float>(fps*30));
            Check(Near(shutter.Position().y,expected),"Constant linear speed / FPS");
            Check(Near(collider.position.y,shutter.Position().y+def.collider.position.y),"Moving collider position");
            const auto origin=StagePoint(shutter.Position(),collider.inverse);
            Check(Near(StagePoint(origin,collider.world).y,shutter.Position().y),"Collider inverse updated");
            for (const auto& event : system.TakeEvents()) {
                if (event.type==ShutterEventType::Progress) ++progressEvents;
                if (event.type==ShutterEventType::Completed) ++completed;
            }
            if (frame<fps*30) Check(shutter.state==ShutterState::Opening,"Opened too early");
        }
        Check(system.shutters[0].state==ShutterState::Open && completed==1 && progressEvents==def.progressEvents.size(),"30-second completion/events");
        const auto open=system.shutters[0].Position();
        system.Update(100,level.collision);
        Check(!system.TryInteract(atButton) && Near(system.shutters[0].Position().y,open.y) && system.TakeEvents().empty(),"Remain open / no repeated events");
    }
    system.Reset(level.collision);
    StageWorld barrier;
    const auto refresh=[&]() { barrier.colliders={level.collision.colliders[def.colliderIndex]}; };
    refresh();
    StageHit hit;
    const Vector3 from{14,0,18},to{10,0,18};
    Check(barrier.Move(from,to,.4f,1.8f,true).x>12.6f,"Closed blocks player");
    Check(barrier.Move(from,to,.55f,2.4f).x>12.7f,"Closed blocks enemies");
    Check(barrier.Raycast(from+Vector3{0,1.4f,0},{-1,0,0},4,hit,.05f),"Closed blocks hitscan and swept bullets");
    EnemyProjectile projectile;
    projectile.active=true; projectile.position={14,1.4f,18}; projectile.velocity={-10,0,0}; projectile.radius=.05f;
    StepStageProjectile(projectile,1,{0,0,0},barrier);
    Check(!projectile.active && projectile.position.x>12,"Closed blocks enemy projectile");
    Check(system.TryInteract(atButton),"Restart allows new operation");
    system.Update(15,level.collision); refresh();
    Check(barrier.Move(from,to,.4f,1.8f,true).x>12.6f,"1.6 m gap blocks standing player");
    Check(!barrier.Raycast({14,1.2f,18},{-1,0,0},4,hit) && barrier.Raycast({14,2,18},{-1,0,0},4,hit),"Partial gap ray occlusion follows geometry");
    system.Update(4,level.collision); refresh();
    Check(Near(barrier.Move(from,to,.4f,1.8f,true).x,10),"2 m gap passes player");
    Check(barrier.Move(from,to,.55f,2.4f).x>12.7f,"Taller enemy still blocked");
    Check(barrier.Raycast({14,2.8f,18},{-1,0,0},4,hit,.05f),"Moving upper panel still blocks bullets");
    system.Update(std::numeric_limits<float>::quiet_NaN(),level.collision);
    system.Update(-1,level.collision);
    Check(Near(system.shutters[0].Progress(),19.0f/30),"Invalid dt ignored");
    system.Update(100,level.collision); refresh();
    Check(Near(barrier.Move(from,to,.55f,2.4f).x,10),"Open passes tall enemy");
    system.Reset(level.collision); refresh();
    Check(barrier.Raycast({14,1.4f,18},{-1,0,0},4,hit),"Reset restores blocking matrices");
    system.TryInteract(atButton); system.Update(30,level.collision); system.TakeEvents(); // Existing route tests traverse the opened exit.
}
}
