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
#include <concepts>   
#include <functional> 
#include <iterator>   
#include <optional>   
#include <condition_variable>
#include <variant>    

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
        {
            std::lock_guard<std::mutex> lg(context.mut);
            context.stage_results[stage] = std::move(result);
        }   
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
        In input;
        {
            std::lock_guard<std::mutex> lg(context.mut);
            input = std::any_cast<const In &>(context.stage_results.at(dep));
        }
        Out result = std::invoke(func, input);
        {
            std::lock_guard<std::mutex> lg(context.mut);
            context.stage_results[stage] = std::move(result);
        }
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
        In1 input1;
        In2 input2;
        {
            std::lock_guard<std::mutex> lg(context.mut);
            input1 = std::any_cast<const In1 &>(context.stage_results.at(in1));
            input2 = std::any_cast<const In2 &>(context.stage_results.at(in2));
        }
        std::pair<In1, In2> out{input1, input2};
        {
            std::lock_guard<std::mutex> lg(context.mut);
            context.stage_results[stage] = out;
        }
    }
};

enum class Error {
    StageAlreadyExists,
    UnknownStage,
    TypeMismatch,
    StageCountMismatch,
    IoError,
    RuntimeError,
    InvalidThreadCount,
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
    case Error::RuntimeError:
        return os << "RuntimeError";
    case Error::InvalidThreadCount:
        return os << "InvalidThreadCount";
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

    template <class Out, class F>
        requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, Out>
    Result<Port<Out>> add_stage(Key id, F &&func) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }
        std::unique_ptr<IStage> stage_ptr =
            std::make_unique<Stage0<Out, std::decay_t<F>>>(id, std::forward<F>(func));
        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);
        return Port<Out>{id};
    }

    template <class Out, class In, class F>
        requires std::invocable<F, const In &> &&
                 std::same_as<std::invoke_result_t<F, const In &>, Out>
    Result<Port<Out>> add_stage(Key id, F &&func, Port<In> upstream) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }
        if (!stages.contains(upstream.id)) {
            return std::unexpected(Error::UnknownStage);
        }
        std::unique_ptr<IStage> stage_ptr =
            std::make_unique<Stage1<Out, In, std::decay_t<F>>>(id, upstream.id,
                                                               std::forward<F>(func));

        stages.emplace(id, std::move(stage_ptr));
        downstream_edges.try_emplace(id);
        upstream_edges.try_emplace(id);
        in_degree.try_emplace(id, 0);

        downstream_edges.at(upstream.id).push_back(id);
        upstream_edges.at(id).push_back(upstream.id);
        in_degree.at(id)++;

        return Port<Out>{id};
    }

    Result<Port<std::monostate>>
    write_bytes_to_file(Key id, const std::string &path,
                        Port<std::vector<std::uint8_t>> bytes_input) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }

        return add_stage<std::monostate>(
            std::move(id),
            [path](const std::vector<std::uint8_t> &data) {
                std::ofstream f(path, std::ios::binary);
                if (!f || !f.write(reinterpret_cast<const char *>(data.data()),
                                   static_cast<std::streamsize>(data.size()))) {
                    throw std::runtime_error("file write failed: " + path);
                }
                return std::monostate{};
            },
            bytes_input);
    }

    Result<Port<std::vector<std::uint8_t>>> read_bytes_from_file(
        Key id, const std::string &path,
        std::optional<Port<std::monostate>> after = std::nullopt) {
        if (stages.contains(id)) {
            return std::unexpected(Error::StageAlreadyExists);
        }

        if (after) {
            return add_stage<std::vector<std::uint8_t>>(
                std::move(id),
                [path](std::monostate) -> std::vector<std::uint8_t> {
                    std::ifstream f(path, std::ios::binary);
                    if (!f)
                        throw std::runtime_error("open failed: " + path);
                    return std::vector<std::uint8_t>{
                        std::istreambuf_iterator<char>(f),
                        std::istreambuf_iterator<char>()};
                },
                after.value());
        }

        return add_stage<std::vector<std::uint8_t>>(
            std::move(id), [path]() -> std::vector<std::uint8_t> {
                std::ifstream f(path, std::ios::binary);
                if (!f)
                    throw std::runtime_error("open failed: " + path);
                return std::vector<std::uint8_t>{
                    std::istreambuf_iterator<char>(f),
                    std::istreambuf_iterator<char>()};
            });
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

    template <class T>
    Result<T> run(const Port<T> &stage, size_t num_threads=1) {
        auto hc = std::thread::hardware_concurrency();
        if (num_threads == 0 || (hc != 0 && num_threads > hc)) {
            return std::unexpected(Error::InvalidThreadCount);
        }
        context.stage_results.clear();
        Result<std::unordered_set<Key>> upstream_stages_result =
            get_all_upstream_stages(stage.id);
        if (!upstream_stages_result.has_value()) {
            return std::unexpected(upstream_stages_result.error());
        }

        std::unordered_set<Key> all_stages_to_run =
            upstream_stages_result.value();
        std::queue<Key> ready;
        std::unordered_map<Key, int> indeg_for_run;
        for (const auto &key : all_stages_to_run) {
            indeg_for_run.emplace(key, in_degree.at(key));
            if (indeg_for_run.at(key) == 0) {
                ready.push(key);
            }
        }

        std::mutex mut;
        std::condition_variable waiting_workers;
        Error err = Error::RuntimeError;
        std::atomic<size_t> remaining_jobs = all_stages_to_run.size();
        std::atomic<bool> failed = false;
        
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; i++) {
            threads.push_back(std::thread([&]() {
                while (true) {
                    Key curr;
                    // Try to grab a stage from the queue
                    {
                        std::unique_lock<std::mutex> uniq(mut);
                        waiting_workers.wait(uniq, [&] {
                            return failed.load() || !ready.empty() || remaining_jobs.load() == 0;
                        });

                        if (failed.load() || remaining_jobs.load() == 0) {
                            return;
                        }
                        curr = ready.front();
                        ready.pop();
                    }

                    // Run the stage
                    try {
                        stages.at(curr)->run(context);
                    } catch (...) {
                        failed = true;
                        waiting_workers.notify_all();
                        return;
                    }

                    // Make downstream ready to run
                    {
                        std::lock_guard<std::mutex> lg(mut);
                        for (const Key& downstream : downstream_edges.at(curr)) {
                            if (all_stages_to_run.contains(downstream)) {
                                indeg_for_run.at(downstream)--;
                                if (indeg_for_run.at(downstream) == 0) {
                                    ready.push(downstream);
                                }
                            }
                        }
                    }

                    remaining_jobs.fetch_sub(1);
                    waiting_workers.notify_all();

                }
            }));
        }

        waiting_workers.notify_all();
        for (auto& worker : threads) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        if (failed.load()) {
            return std::unexpected(err);
        }

        try {
            return std::any_cast<T>(context.stage_results.at(stage.id));
        } catch (const std::bad_any_cast&) {
            return std::unexpected(Error::TypeMismatch);
        } catch (...) {
            return std::unexpected(Error::UnknownStage);
        }

    }

};

} // namespace pipeline