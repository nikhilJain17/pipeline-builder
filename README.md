# pipeline-builder
A pipeline builder library in C++.

## Design Overview
The pipeline is represented as a directed acyclic graph of type-erased stages. Each stage encapsulates a typed callable and produces a typed output in a shared execution `Context`. Users interact with the library using `Port<T>` handles to preserve compile-time type checking across stages, while allowing heterogenous input types within the same pipeline. Input and output types are deduced from the callable, and input types are type-matched against `Port<T>`. File I/O is modeled as a regular stage with a callable to read or write to a file.  

## Example usage

```
Pipeline p;

auto src = p.add_stage("src", [] {
    return 5;
}).value();

auto incr = p.add_stage("incr",
    [](int x) { return x + 1; },
    src
).value();

auto triple = p.add_stage("triple",
    [](int x) { return x * 3; },
    incr
).value();

auto result = p.run(triple).value();
// result == (5 + 1) * 3
```

## API

#### Create stage with no input
```
template <class F>
    requires std::invocable<F> 
auto add_stage(Key id, F &&func) -> Result<Port<std::invoke_result_t<F>>>
```
- `id`: name of the stage
- `func`: callable to execute per stage

Returns a `Result` which either contains a `Port<std::invoke_result_t<F>>` or `pipeline::Error`.

On success, creates a stage and returns a `Port<std::invoke_result_t<F>>` handle to be used to fetch results or wire dependencies to other stages. The output `Port` handle ensures compile time type checking between stages.   

On failure, returns a `pipeline::Error`.

#### Create stage with one input
```
template <class In, class F>
    requires std::invocable<F, const In &>
auto add_stage(Key id, F &&func, Port<In> upstream)
    -> Result<Port<std::invoke_result_t<F, const In &>>>
```
- `id`: name of the stage
- `func`: callable to execute per stage
- `upstream`: handle to input stage

Returns a `Result` which either contains a `Port<std::invoke_result_t<F, const In&>>` or `pipeline::Error`.

On success, creates a stage that reads input from `upstream` and returns a `Port<std::invoke_result_t<F, const In&>>` handle to be used to fetch results or wire dependencies to other stages. The input `Port` parameter, output `Port` handle, and callable `F` ensure compile time type checking between stages.   

On failure, returns a `pipeline::Error`.

#### Join two stage outputs
```
template <class In1, class In2>
Result<Port<std::pair<In1, In2>>> join(Key id, Port<In1> in1,
                                        Port<In2> in2)
```
- `id`: name of the stage
- `in1`: first handle to input stage
- `in2`: second handle to input stage
- `In1`: input type of the stage
- `In2`: input type of the stage

Returns a `Result` which either contains a `Port<std::pair<In1, In2>>` or `pipeline::Error`.

On success, creates a stage that reads from upstream stages `in1` and `in2` and merges them into a single input, returning a `Port<std::pair<In1, In2>>`. To be used to pack multiple stage outputs to fan into a subsequent stage's input.

On failure, returns a `pipeline::Error`.

#### File Write

```
Result<Port<std::monostate>>
write_bytes_to_file(Key id, const std::string &path,
                    Port<std::vector<std::uint8_t>> bytes_input) {
```
Syntactic sugar to add a stage which writes to a file and returns a `std::monostate`.  

Equivalent to calling  
```
add_stage(id, [path](const std::vector<std::uint8_t>& data) {
    // write to file...
})
```

#### File Read

```
Result<Port<std::vector<std::uint8_t>>> read_bytes_from_file(
    Key id, const std::string &path,
    std::optional<Port<std::monostate>> after = std::nullopt)
```
Syntactic sugar to add a stage which reads a file and returns `std::vector<uint8_t>`, optionally executing after a void return-value stage (such as a stage which writes to a filepath).

Equivalent to calling  
```
add_stage(
    id, 
    [path](std::monostate) -> std::vector<std::uint8_t> {
        // read from file...            
    },
    after);
```

#### Run

```
template <class T>
Result<T> run(const Port<T>& stage, size_t num_threads=1);
```  
Executes the minimal upstream subgraph required to compute the requested stage. Parallel execution is enabled when `num_threads > 1`, and stages execute when all upstream dependencies are completed. Returns a `Result<T>` which either contains a `T` on success or `pipeline::Error` on failure.

## Features
- DAGs are acyclic by construction, since stages can only depend on previously created stages, disallowing forward references and cycles.  
- Multiple inputs per stage allowed via `join`
- Compile-time type checking of pipeline dependencies via `Port`
- Topological parallel execution of stages
- File I/O supported as normal stages with read/write callables

## Possible Extensions  
- Option to cache results on reruns (currently each new run discards all previously cached results)
- Soft dependencies, allowing tasks to be skipped on failure
- Retry policy to automatically rerun failed tasks  
