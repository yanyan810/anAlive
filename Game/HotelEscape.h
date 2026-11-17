#pragma once
#include "CameraSequence.h"
#include <numbers>
#include <optional>

struct HotelEscape {
    struct Segment {
        Vector3 start, direction;
        float offset, length;
        Vector3 Heading() const {
            return {-std::atan2(direction.y,std::hypot(direction.x,direction.z)),std::atan2(direction.x,direction.z),0};
        }
    };
    struct Actor {
        std::string enemy;
        Vector3 position, rotation, launchDirection{};
        float launchPower=8;
    };
    struct BodyPose {
        float pitch=0, height=0, roll=0;
    };
    struct BodyMotion {
        float ignitionSeconds=.42f, vibrationMeters=.009f, rollDegrees=2.25f;
        float launchSeconds=.84f, pitchDegrees=1.7f, squatMeters=.018f;
        float boostAcceleration=0, reboundDegrees=0, launchRollDegrees=0;
    };
    struct DriftMotion {
        float lateralMeters=0, yawDegrees=22, slideSeconds=.72f, recoverySeconds=.52f;
    };
    struct DriftPose { float offset=0, offsetRate=0, slipWeight=0; };
    struct CarPose {
        Vector3 position, rotation;
        Vector3 Right() const { return {std::cos(rotation.y),0,-std::sin(rotation.y)}; }
        Vector3 Forward() const { return {std::sin(rotation.y),0,std::cos(rotation.y)}; }
    };

    std::string goal;
    std::string launchCamera;
    std::vector<std::string> hiddenMeshes;
    std::array<std::string,2> cameras{"hotel_escape_1","hotel_escape_2"};
    Vector3 start, rotation;
    std::vector<Segment> route;
    std::vector<Actor> actors;
    float departure=1.2f, acceleration=6.5f, maxSpeed=11.5f;
    float cut=3, fade=6, duration=7, shake=.065f, roadWidth=0;
    BodyMotion bodyMotion;
    DriftMotion drift;

    static bool IsHotel(const std::string& id) { return id=="stage03" || id=="stage04"; }
    static HotelEscape Parse(const nlohmann::json& data) {
        const auto vector=[](const nlohmann::json& v) {
            if (!v.is_array() || v.size()!=3) throw std::runtime_error("Escape: expected a 3-vector");
            Vector3 p{v[0].get<float>(),v[1].get<float>(),v[2].get<float>()};
            for (const auto x:{p.x,p.y,p.z})
                if (!std::isfinite(x) || std::abs(x)>1e6f) throw std::runtime_error("Escape: invalid vector");
            return p;
        };
        const auto number=[](const nlohmann::json& object,const char* key,float fallback,float min,float max) {
            const float value=object.value(key,fallback);
            if (!std::isfinite(value) || value<min || value>max)
                throw std::runtime_error(std::string("Escape: invalid ")+key);
            return value;
        };
        HotelEscape result;
        result.goal=data.at("goal").get<std::string>();
        result.launchCamera=data.value("launchCamera",std::string{});
        result.hiddenMeshes=data.value("hiddenMeshes",result.hiddenMeshes);
        result.cameras=data.value("cameras",result.cameras);
        if (result.goal.empty() || result.cameras[0].empty() || result.cameras[1].empty())
            throw std::runtime_error("Escape: a Goal and both camera sequence IDs are required");
        result.start=vector(data.at("position"));
        result.rotation=vector(data.at("rotation"));
        result.departure=number(data,"departure",result.departure,0,120);
        result.acceleration=number(data,"acceleration",result.acceleration,.01f,100);
        result.maxSpeed=number(data,"maxSpeed",result.maxSpeed,.01f,100);
        result.cut=number(data,"cut",result.cut,0,120);
        result.fade=number(data,"fade",result.fade,0,120);
        result.duration=number(data,"duration",result.duration,.01f,120);
        result.shake=number(data,"shake",result.shake,0,1);
        result.roadWidth=number(data,"roadWidth",result.roadWidth,0,100);
        if (data.contains("bodyMotion")) {
            const auto& motion=data.at("bodyMotion");
            auto& settings=result.bodyMotion;
            settings.ignitionSeconds=number(motion,"ignitionSeconds",settings.ignitionSeconds,0,3);
            settings.vibrationMeters=number(motion,"vibrationMeters",settings.vibrationMeters,0,.03f);
            settings.rollDegrees=number(motion,"rollDegrees",settings.rollDegrees,0,12);
            settings.launchSeconds=number(motion,"launchSeconds",settings.launchSeconds,0,3);
            settings.pitchDegrees=number(motion,"pitchDegrees",settings.pitchDegrees,0,5);
            settings.squatMeters=number(motion,"squatMeters",settings.squatMeters,0,.08f);
            settings.boostAcceleration=number(motion,"boostAcceleration",settings.boostAcceleration,0,80);
            settings.reboundDegrees=number(motion,"reboundDegrees",settings.reboundDegrees,0,8);
            settings.launchRollDegrees=number(motion,"launchRollDegrees",settings.launchRollDegrees,0,12);
        }
        if (data.contains("drift")) {
            const auto& motion=data.at("drift");
            auto& settings=result.drift;
            settings.lateralMeters=number(motion,"lateralMeters",settings.lateralMeters,-1.5f,1.5f);
            settings.yawDegrees=number(motion,"yawDegrees",settings.yawDegrees,0,35);
            settings.slideSeconds=number(motion,"slideSeconds",settings.slideSeconds,.1f,3);
            settings.recoverySeconds=number(motion,"recoverySeconds",settings.recoverySeconds,.1f,3);
        }
        if (!(result.departure<result.cut && result.cut<result.fade && result.fade<result.duration))
            throw std::runtime_error("Escape: require departure < cut < fade < duration");

        const auto& path=data.at("path");
        if (!path.is_array() || path.empty() || path.size()>128)
            throw std::runtime_error("Escape: require 1..128 waypoints");
        Vector3 start=result.start;
        float offset=0;
        for (const auto& point:path) {
            const auto end=vector(point), delta=end-start;
            const float length=std::hypot(delta.x,delta.y,delta.z);
            if (std::hypot(delta.x,delta.z)<.01f) throw std::runtime_error("Escape: waypoints must advance horizontally");
            result.route.push_back({start,delta*(1/length),offset,length});
            offset+=length;
            start=end;
        }
        const auto& actors=data.at("enemies");
        if (!actors.is_array() || actors.size()>16) throw std::runtime_error("Escape: maximum 16 enemies");
        for (const auto& row:actors) {
            Actor actor{row.at("enemy").get<std::string>(),vector(row.at("position")),vector(row.at("rotation"))};
            if (row.contains("launchDirection")) actor.launchDirection=vector(row.at("launchDirection"));
            actor.launchPower=number(row,"launchPower",actor.launchPower,0,30);
            result.actors.push_back(std::move(actor));
        }
        return result;
    }

    BodyPose BodyAt(double elapsed) const {
        const double sinceDeparture=elapsed-departure;
        const double ignition=std::min(departure,bodyMotion.ignitionSeconds);
        const float pitch=-bodyMotion.pitchDegrees*CameraSequence::DegreesToRadians;
        if (sinceDeparture<0) {
            const double time=ignition+sinceDeparture;
            if (time<=0) return {};
            const float phase=static_cast<float>(time/ignition);
            constexpr double turn=2*std::numbers::pi;
            const float envelope=std::sin(std::numbers::pi_v<float>*phase);
            const float squat=bodyMotion.launchSeconds>0 ? Ease((phase-.68f)/.32f) : 0;
            const float vibration=bodyMotion.vibrationMeters*envelope*static_cast<float>(std::sin(turn*13*time));
            const float roll=bodyMotion.rollDegrees*CameraSequence::DegreesToRadians*envelope*
                std::sin(8*std::numbers::pi_v<float>*phase);
            return {pitch*squat,vibration-bodyMotion.squatMeters*squat,roll};
        }
        if (bodyMotion.launchSeconds<=0) return {};
        const float phase=static_cast<float>(sinceDeparture/bodyMotion.launchSeconds);
        const float recovery=ignition>0 ? 1-phase : (phase<.20f ? phase/.20f : (1-phase)/.80f);
        const float amount=Ease(recovery);
        const float kick=static_cast<float>(sinceDeparture/LaunchKickDuration());
        // The body peaks just after the acceleration burst begins, then settles into the slide.
        const float rebound=Ease(kick/.58f)*(1-Ease((kick-.58f)/1.20f));
        const auto slip=DriftAt(sinceDeparture);
        const float leaning=drift.lateralMeters==0 ? 0 :
            -std::copysign(bodyMotion.launchRollDegrees,drift.lateralMeters)*(.55f*rebound+.75f*slip.slipWeight);
        return {pitch*amount-bodyMotion.reboundDegrees*CameraSequence::DegreesToRadians*rebound,
            -bodyMotion.squatMeters*amount+.017f*bodyMotion.reboundDegrees*rebound,
            leaning*CameraSequence::DegreesToRadians};
    }

    float WheelAngleAt(double elapsed,float radius,bool driven) const {
        const double ignition=std::min(departure,bodyMotion.ignitionSeconds);
        double angle=DistanceAt(elapsed)/radius;
        if (driven && ignition>0) {
            constexpr double spinRate=26; // radians per second at the end of the stationary rev
            constexpr double gripTime=.34;
            const double sinceDeparture=elapsed-departure;
            const double ramp=std::clamp(1+sinceDeparture/ignition,0.0,1.0);
            const double grip=std::clamp(sinceDeparture/gripTime,0.0,1.0);
            // Integrate the same speed at both sides of departure, then blend into rolling.
            angle+=spinRate*(ignition*ramp*ramp*ramp*(1-.5*ramp)+gripTime*(grip-grip*grip*grip*(1-.5*grip)));
        }
        return static_cast<float>(std::fmod(angle,2*std::numbers::pi));
    }

    float DistanceAt(double elapsed) const { return TravelAt(elapsed).distance; }

    const Segment& SegmentAt(float distance) const {
        const auto it=std::upper_bound(route.begin(),route.end(),distance,
            [](float value,const Segment& segment) { return value<segment.offset; });
        return *std::prev(it);
    }

    CarPose CarAt(double elapsed) const {
        const auto travel=TravelAt(elapsed);
        const auto& segment=SegmentAt(travel.distance);
        CarPose pose{segment.start+segment.direction*(travel.distance-segment.offset),
            travel.distance>0 ? segment.Heading() : rotation};
        const auto slip=DriftAt(elapsed-departure);
        pose.position=pose.position+pose.Right()*(slip.offset*drift.lateralMeters);
        // Face the tangent of the actual trajectory, with a brief rear-slip angle on top.
        const float forwardSpeed=travel.speed*std::hypot(segment.direction.x,segment.direction.z);
        const float lateralSpeed=drift.lateralMeters*slip.offsetRate;
        const float yawOffset=-std::copysign(drift.yawDegrees,drift.lateralMeters)*CameraSequence::DegreesToRadians*slip.slipWeight;
        pose.rotation.y+=std::atan2(lateralSpeed,forwardSpeed)+yawOffset;
        return pose;
    }

private:
    struct TravelSample { float distance, speed; };
    TravelSample TravelAt(double elapsed) const {
        const double t=std::max(0.0,elapsed-departure);
        const double launchSpeed=LaunchSpeed();
        const double duration=LaunchKickDuration();
        const double burstTime=std::min(t,duration);
        const double phase=duration>0 ? burstTime/duration : 0;
        // Integrate a short, decaying acceleration burst before the usual sustained pull.
        const double boost=bodyMotion.boostAcceleration;
        const double burstSpeed=burstTime*(acceleration+boost*(1-phase+phase*phase/3));
        const double burstDistance=burstTime*burstTime*(.5*acceleration+boost*(.5-phase/3+phase*phase/12));
        const double remaining=t-burstTime;
        const double launchTime=std::min(remaining,std::max(0.0,launchSpeed-burstSpeed)/acceleration);
        const double recoveryAcceleration=acceleration*.28;
        const double recoveryTime=std::min(remaining-launchTime,(maxSpeed-launchSpeed)/recoveryAcceleration);
        const double distance=burstDistance+(burstSpeed+.5*acceleration*launchTime)*launchTime+
            (launchSpeed+.5*recoveryAcceleration*recoveryTime)*recoveryTime+
            maxSpeed*(remaining-launchTime-recoveryTime);
        const auto& end=route.back();
        const float routeLength=end.offset+end.length;
        if (distance>=routeLength) return {routeLength,0};
        const double speed=burstSpeed+acceleration*launchTime+recoveryAcceleration*recoveryTime;
        return {static_cast<float>(distance),static_cast<float>(speed)};
    }

    static float Ease(float t) {
        t=std::clamp(t,0.f,1.f);
        return t*t*(3-2*t);
    }
    double LaunchSpeed() const { return maxSpeed*(drift.lateralMeters!=0 ? .58 : 1.0); }
    double LaunchKickDuration() const {
        return std::min({.18,bodyMotion.launchSeconds*.25,LaunchSpeed()/(acceleration+bodyMotion.boostAcceleration/3.0)});
    }
    DriftPose DriftAt(double time) const {
        if (drift.lateralMeters==0 || time<=0 || time>=drift.slideSeconds+drift.recoverySeconds) return {};
        if (time<drift.slideSeconds) {
            const float phase=static_cast<float>(time/drift.slideSeconds);
            const float shift=Ease(phase);
            const float shiftRate=6*phase*(1-phase)/drift.slideSeconds;
            constexpr float begin=.10f, peak=.35f, end=.87f;
            const float slip=Ease((phase-begin)/(peak-begin))*(1-Ease((phase-peak)/(end-peak)));
            return {shift*shift,2*shift*shiftRate,slip};
        }
        const float phase=static_cast<float>((time-drift.slideSeconds)/drift.recoverySeconds);
        return {1-Ease(phase),-6*phase*(1-phase)/drift.recoverySeconds,0};
    }
};
