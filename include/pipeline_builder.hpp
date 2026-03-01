#pragma once

#include <any>
#include <atomic>
#include <expected>
#include <fstream>
#include <ios>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pipeline {

using Key = std::string;
using Value = std::any;
struct Context {
    std::mutex mut;
    std::unordered_map<Key, Value> stage_results;
};

class IStage {
  public:
    virtual ~IStage() = default;
    virtual Key stage_key() const = 0;
    virtual void run(Context &context) = 0;
};

template <class Out, class F> class Stage0 final : public IStage {
  private:
    Key stage;
    F func;

  public:
    Stage0(Key stage, F func)
        : stage(std::move(stage)), func(std::forward<F>(func)) {}

    Key stage_key() const override { return stage; }
    void run(Context &context) override {
        Out result = std::invoke(func);
        context.stage_results[stage] = std::move(result);
    }
};

template <class Out, class In, class F> class Stage1 final : public IStage {
  private:
    Key stage;
    F func;
    Key dep;

  public:
    Stage1(Key stage, Key input, F func)
        : stage(std::move(stage)), dep(std::move(input)),
          func(std::forward<F>(func)) {}

    Key stage_key() const override { return stage; }
    void run(Context &context) override {
        const In &input =
            std::any_cast<const In &>(context.stage_results.at(dep));
        Out result = std::invoke(func, input);
        context.stage_results[stage] = std::move(result);
    }
};

template <class In1, class In2> class JoinStage final : public IStage {
  private:
    Key stage;
    Key in1, in2;

  public:
    JoinStage(Key stage, Key in1, Key in2)
        : stage(std::move(stage)), in1(std::move(in1)), in2(std::move(in2)) {}

    Key stage_key() const override { return stage; }

    void run(Context &context) override {
        const In1 &input_1 =
            std::any_cast<const In1 &>(context.stage_results.at(in1));
        const In2 &input_2 =
            std::any_cast<const In2 &>(context.stage_results.at(in2));
        context.stage_results[stage] = std::pair<In1, In2>{input_1, input_2};
    }
};

enum class Error {
    StageAlreadyExists,
    UnknownStage,
    TypeMismatch,
    StageCountMismatch,
    IoError,
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
    case Error::IoError:
        return os << "IoError";
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
    std::unordered_map<Key, std::atomic<int>> in_degree;
    Context context;

    std::vector<std::thread> workers;

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

    template <class Out> Result<Port<Out>> add_stage(Key id, auto &&func) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }

        std::unique_ptr<IStage> stage_ptr =
            std::make_unique<Stage0<Out, std::decay_t<decltype(func)>>>(id,
                                                                        func);
        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);
        return Port<Out>{id};
    }

    template <class Out, class In>
    Result<Port<Out>> add_stage(Key id, auto &&func, Port<In> upstream) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }
        if (!stages.contains(upstream.id)) {
            return std::unexpected(Error::UnknownStage);
        }
        std::unique_ptr<IStage> stage_ptr =
            std::make_unique<Stage1<Out, In, std::decay_t<decltype(func)>>>(
                id, upstream.id, func);

        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);

        downstream_edges.at(upstream.id).push_back(id);
        upstream_edges.at(id).push_back(upstream.id);
        in_degree.at(id)++;

        return Port<Out>{id};
    }

    template <class In1, class In2>
    Result<Port<std::pair<In1, In2>>> join(Key id, Port<In1> in1,
                                           Port<In2> in2) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }
        if (!stages.contains(in1.id)) {
            return std::unexpected(Error::UnknownStage);
        }
        if (!stages.contains(in2.id)) {
            return std::unexpected(Error::UnknownStage);
        }

        std::unique_ptr<IStage> stage_ptr =
            std::make_unique<JoinStage<In1, In2>>(id, in1.id, in2.id);

        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);

        downstream_edges.at(in1.id).push_back(id);
        downstream_edges.at(in2.id).push_back(id);
        in_degree.at(id) += 2;

        upstream_edges.at(id).push_back(in1.id);
        upstream_edges.at(id).push_back(in2.id);

        return Port<std::pair<In1, In2>>(id);
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
            try {
                stages.at(curr)->run(context);
            } catch (const std::exception &e) {
                std::cout << "Exception " << e.what() << "\n";
                return std::unexpected(Error::IoError);
            }
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