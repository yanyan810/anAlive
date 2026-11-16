#include "LiveLua.h"

extern "C" {
#include <lua/src/lauxlib.h>
}
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace {
constexpr size_t kMemoryLimit = 256 * 1024;
constexpr int kHookInterval = 100;
constexpr int kCallLimit = 20000;

std::string ErrorMessage(lua_State* lua, int status) {
    if (status == LUA_ERRMEM) return "Lua memory limit exceeded";
    if (lua_type(lua, -1) != LUA_TSTRING) return "Lua evaluation failed";
    size_t length = 0;
    const auto* message = lua_tolstring(lua, -1, &length);
    return std::string(message, std::min(length, size_t{512}));
}
}

struct LiveLua::State {
    size_t bytes = 0;
    lua_State* lua = lua_newstate(Allocate, this);
    std::span<const Field> fields;
    Budget* budget = nullptr;
    int callRemaining = 0;
    bool hookLimitExceeded = false;

    ~State() { if (lua) lua_close(lua); }

    static State& Get(lua_State* lua) {
        void* context = nullptr;
        lua_getallocf(lua, &context);
        return *static_cast<State*>(context);
    }

    static void* Allocate(void* context, void* pointer, size_t oldSize, size_t newSize) {
        auto& used = static_cast<State*>(context)->bytes;
        if (!pointer) oldSize = 0;
        if (!newSize) {
            std::free(pointer);
            used -= oldSize;
            return nullptr;
        }
        if (newSize > kMemoryLimit - (used - oldSize)) return nullptr;
        auto* result = std::realloc(pointer, newSize);
        if (result) used = used - oldSize + newSize;
        return result;
    }

    static void Hook(lua_State* lua, lua_Debug*) {
        auto& self = Get(lua);
        self.callRemaining -= kHookInterval;
        self.budget->instructions -= kHookInterval;
        if (self.callRemaining <= 0 || self.budget->instructions <= 0) {
            self.hookLimitExceeded = true;
            luaL_error(lua, "Lua instruction limit exceeded");
        }
        if (std::chrono::steady_clock::now() >= self.budget->deadline) {
            self.hookLimitExceeded = true;
            luaL_error(lua, "Lua time limit exceeded");
        }
    }

    static int Invoke(lua_State* lua) {
        const auto fields = Get(lua).fields;
        lua_createtable(lua, 0, static_cast<int>(fields.size()));
        for (const auto& field : fields) {
            if (const auto* number = std::get_if<double>(&field.value)) lua_pushnumber(lua, *number);
            else if (const auto* text = std::get_if<std::string_view>(&field.value)) lua_pushlstring(lua, text->data(), text->size());
            else lua_pushboolean(lua, std::get<bool>(field.value));
            lua_setfield(lua, -2, field.name);
        }
        lua_call(lua, 1, 1);
        return 1;
    }
};

LiveLua::LiveLua(std::string_view source) {
    if (source.size() > kSourceLimit) throw Error(ErrorKind::Limit, "Lua source exceeds 16 KiB");
    state_ = std::make_unique<State>();
    auto* lua = state_->lua;
    if (!lua) throw Error(ErrorKind::Limit, "Cannot allocate Lua state");
    const int status = luaL_loadbufferx(lua, source.data(), source.size(), "=enemy policy", "t");
    if (status != LUA_OK)
        throw Error(status == LUA_ERRMEM ? ErrorKind::Limit : ErrorKind::Syntax, ErrorMessage(lua, status));
    // The top-level chunk is retained; its environment is cleared at each call.
}

LiveLua::~LiveLua() = default;

double LiveLua::Evaluate(std::span<const Field> fields, Budget& budget) {
    if (budget.instructions <= 0 || std::chrono::steady_clock::now() >= budget.deadline)
        throw Error(ErrorKind::Limit, "Lua batch budget exhausted");
    auto& self = *state_;
    auto* lua = self.lua;
    self.fields = fields;
    self.budget = &budget;
    self.callRemaining = kCallLimit;
    self.hookLimitExceeded = false;
    lua_pushnil(lua);
    lua_setupvalue(lua, 1, 1);
    lua_pushcfunction(lua, State::Invoke);
    lua_pushvalue(lua, 1);
    lua_sethook(lua, State::Hook, LUA_MASKCOUNT, kHookInterval);
    const int status = lua_pcall(lua, 1, 1, 0);
    lua_sethook(lua, nullptr, 0, 0);
    self.fields = {};
    self.budget = nullptr;

    auto kind = ErrorKind::Result;
    std::string error;
    double result = 0;
    if (status != LUA_OK) {
        kind = status == LUA_ERRMEM || self.hookLimitExceeded ? ErrorKind::Limit : ErrorKind::Runtime;
        error = ErrorMessage(lua, status);
        if (error.empty()) error = "Lua evaluation failed";
    } else if (std::chrono::steady_clock::now() >= budget.deadline) {
        kind = ErrorKind::Limit;
        error = "Lua time limit exceeded";
    } else if (lua_type(lua, -1) != LUA_TNUMBER || !std::isfinite(lua_tonumber(lua, -1))) {
        error = "Lua policy must return a finite number";
    } else {
        result = lua_tonumber(lua, -1);
    }
    lua_settop(lua, 1);
    lua_pushnil(lua);
    lua_setupvalue(lua, 1, 1);
    if (!error.empty()) throw Error(kind, error);
    return result;
}
