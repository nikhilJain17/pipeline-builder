#pragma once

#include <any>
#include <unordered_map>
#include <string>
#include <vector>

using Key = std::string;
using Value = std::any;
struct Context {
    std::unordered_map<Key, Value> stage_results;
};

class IStage {
public:
    virtual ~IStage() = default;
    virtual Key stage_key() const = 0;
    virtual void run(Context&) = 0;
};

template <class Out, class F, class... Ins>
class StageModel final : public IStage {
private:
    Key stage;
    std::vector<std::string> upstream_deps;
    F func;
public:
    StageModel(Key stage, std::vector<Key> upstream_deps, F&& func)
        : stage(std::move(stage)), upstream_deps(std::move(upstream_deps)),
          func(std::forward<F>(func)) {}

    Key stage_key() const override { return stage; }
    void run(Context& context) override {
        // TODO
    }
};