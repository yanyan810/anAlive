#pragma once
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include <deque>
#include <cstddef>
#include <algorithm>
#include <utility>
// Snapshots own CPU data only. New simulation after rewind discards the future branch.
template<class State> class DebugTimeline {
public:
    static constexpr size_t capacity=300;
    void Clear() { states_.clear(); cursor_=0; }
    void Push(State state) {
        if (!states_.empty()) states_.erase(states_.begin()+static_cast<std::ptrdiff_t>(cursor_+1),states_.end());
        states_.push_back(std::move(state));
        if (states_.size()>capacity) states_.pop_front();
        cursor_=states_.size()-1;
    }
    const State* Back() { if (states_.empty() || cursor_==0) return nullptr; return &states_[--cursor_]; }
    const State* Forward() { if (states_.empty() || cursor_+1>=states_.size()) return nullptr; return &states_[++cursor_]; }
    size_t Size() const { return states_.size(); }
    size_t Cursor() const { return cursor_; }
private:
    std::deque<State> states_;
    size_t cursor_=0;
};
#endif
