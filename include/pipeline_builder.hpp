#pragma once

#include <any>
#include <expected>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    virtual void run(Context &context) = 0;
};

template <class Out, class F, class... Ins>
class StageModel final : public IStage {
  private:
    Key stage;
    std::vector<std::string> upstream_deps;
    F func;

    template <std::size_t... I>
    void run_impl(Context &context, std::index_sequence<I...>) {
        Out result = std::invoke(
            func, std::any_cast<const Ins &>(
                      context.stage_results.at(upstream_deps[I]))...);

        context.stage_results[stage] = std::move(result);
    }

  public:
    StageModel(Key stage, std::vector<Key> upstream_deps, F func)
        : stage(std::move(stage)), upstream_deps(std::move(upstream_deps)),
          func(std::forward<F>(func)) {}

    Key stage_key() const override { return stage; }
    void run(Context &context) override {
        run_impl(context, std::index_sequence_for<Ins...>{});
    }
};

enum class Error {
    StageAlreadyExists,
    UnknownStage,
    TypeMismatch,
    StageCountMismatch
};

std::ostream &operator<<(std::ostream &os, Error e) {
    switch (e) {
    case Error::StageAlreadyExists:
        return os << "StageAlreadyExists";
    case Error::StageCountMismatch:
        return os << "StageCountMismatch";
    case Error::UnknownStage:
        return os << "UnknownStage";
    case Error::TypeMismatch:
        return os << "TypeMismatch";
    }
    return os << "UnknownError";
}

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
    std::unordered_map<Key, int> in_degree;
    Context context;

    Result<std::unordered_set<Key>> get_all_upstream_stages(const Key &key) {
        std::unordered_set<Key> graph;
        std::queue<Key> frontier;
        frontier.push(key);
        graph.insert(key);
        while (!frontier.empty()) {
            Key curr = frontier.front();
            frontier.pop();

            if (!upstream_edges.contains(curr)) {
                return std::unexpected(Error::UnknownStage);
            }

            for (const auto &neighbor : upstream_edges.at(curr)) {
                if (!graph.contains(neighbor)) {
                    graph.insert(neighbor);
                    frontier.push(neighbor);
                }
            }
        }
        return graph;
    }

  public:
    Pipeline() = default;

    template <class Out, class... Ins>
    Result<Port<Out>> add_stage(Key id, auto &&func,
                                Port<Ins>... upstream_ports) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }

        std::vector<Key> upstream_deps = {upstream_ports.id...};
        for (const auto &dep : upstream_deps) {
            if (!stages.contains(dep)) {
                return std::unexpected(Error::UnknownStage);
            }
        }

        std::unique_ptr<IStage> stage_ptr = std::make_unique<
            StageModel<Out, std::decay_t<decltype(func)>, Ins...>>(
            id, upstream_deps, std::forward<decltype(func)>(func));

        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);
        for (const auto &dep : upstream_deps) {
            downstream_edges.at(dep).push_back(id);
            upstream_edges.at(id).push_back(dep);
            in_degree.at(id)++;
        }
        return Port<Out>{id};
    }

    template <class T> Result<T> run(const Port<T> &out) {
        // Start each run with a fresh state.
        // Allowing the user to optionally cache outputs
        // is a possible future extension.
        context.stage_results.clear();
        Result<std::unordered_set<Key>> upstream_stages_result =
            get_all_upstream_stages(out.id);
        if (!upstream_stages_result.has_value()) {
            return std::unexpected(upstream_stages_result.error());
        }

        std::unordered_set<Key> all_stages_to_run =
            upstream_stages_result.value();
        std::queue<Key> ready;
        for (const auto &key : all_stages_to_run) {
            if (in_degree.at(key) == 0) {
                ready.push(key);
            }
        }

        size_t num_stages_run = 0;
        while (!ready.empty()) {
            Key curr = ready.front();
            ready.pop();
            if (!stages.contains(curr)) {
                return std::unexpected(Error::UnknownStage);
            }
            stages.at(curr)->run(context);
            num_stages_run++;

            for (const auto &downstream : downstream_edges.at(curr)) {
                if (all_stages_to_run.contains(downstream)) {
                    if (--in_degree.at(downstream) == 0) {
                        ready.push(downstream);
                    }
                }
            }
        }

        if (num_stages_run != all_stages_to_run.size()) {
            return std::unexpected(Error::StageCountMismatch);
        }

        try {
            return std::any_cast<T>(context.stage_results.at(out.id));
        } catch (const std::bad_any_cast &) {
            return std::unexpected(Error::TypeMismatch);
        } catch (const std::out_of_range &) {
            return std::unexpected(Error::UnknownStage);
        }
    }
};

} // namespace pipeline