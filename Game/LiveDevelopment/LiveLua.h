#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

// Text chunk in, scalar fields in, number out. This layer knows no game classes.
class LiveLua {
public:
    enum class ErrorKind { Syntax, Runtime, Result, Limit, Contract };
    struct Error : std::runtime_error {
        Error(ErrorKind kind, const std::string& message) : std::runtime_error(message), kind(kind) {}
        const ErrorKind kind;
    };
    struct Field {
        const char* name;
        std::variant<double, std::string_view, bool> value;
    };
    struct Budget {
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        int instructions = 50000;
    };
    static constexpr size_t kSourceLimit = 16 * 1024;

    explicit LiveLua(std::string_view source);
    ~LiveLua();
    double Evaluate(std::span<const Field> fields, Budget& budget);

private:
    struct State;
    std::unique_ptr<State> state_;
};
