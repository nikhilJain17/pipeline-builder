#pragma once

#include <any>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace pipeline {

using Key = std::string;
using Value = std::any;
struct Context {
    std::unordered_map<Key, Value> stage_results;
};

class IStage {
  public:
    virtual ~IStage() = default;
    virtual Key stage_key() const = 0;
    virtual void run(Context &) = 0;
};

template <class Out, class F, class... Ins>
class StageModel final : public IStage {
  private:
    Key stage;
    std::vector<std::string> upstream_deps;
    F func;

  public:
    StageModel(Key stage, std::vector<Key> upstream_deps, F &&func)
        : stage(std::move(stage)), upstream_deps(std::move(upstream_deps)),
          func(std::forward<F>(func)) {}

    Key stage_key() const override { return stage; }
    void run(Context &context) override {
        // TODO
    }
};

enum class Error {
    StageAlreadyExists,
    CycleDetected,
    UnknownDependency,
    TypeMismatch,
};

template <class T> using Result = std::expected<T, Error>;
using Status = Result<std::monostate>;

template <class T> struct Port {
    Key id;
};

class Pipeline {
  private:
    std::unordered_map<Key, std::unique_ptr<IStage>> stages;
    std::unordered_map<Key, std::vector<Key>> downstream_edges;
    std::unordered_map<Key, std::vector<Key>> upstream_edges;
    Context context;

  public:
    Pipeline() = default;

    template <class Out, class... Ins>
    Result<Port<Out>> add_stage(Key id, auto&& func, Port<Ins>... upstream_ports) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }
        
        std::vector<Key> upstream_deps = { upstream_ports.id... };
        for (const auto& src : upstream_deps) {
            if (!stages.contains(src)) {
                return std::unexpected(Error::UnknownDependency);
            }
        }

        std::unique_ptr<IStage> stage_ptr = std::make_unique<
            StageModel<Out, std::decay_t<decltype(func)>, Ins...>>(
                id, upstream_deps, std::forward<decltype(func)>(func)
            );

        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        for (const auto& dep : upstream_deps) {
            downstream_edges[dep].push_back(id);
            upstream_edges[id].push_back(dep);
        }

        return Port<Out>{id};
    }
};

} // namespace pipeline