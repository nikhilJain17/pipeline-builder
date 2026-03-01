# pipeline-builder
A pipeline builder library in C++

## Example usage

```
Pipeline p;

auto src = p.add_stage<int>("src", [] {
    return 5;
}).value();

auto incr = p.add_stage<int>("incr",
    [](int x) { return x + 1; },
    src
).value();

auto triple = p.add_stage<int>("triple",
    [](int x) { return x * 3; },
    incr
).value();

auto result = p.run<int>(triple).value();
// result == (5 + 1) * 3
```

## API
```
template <class Out, class... Ins>
Result<Port<Out>> add_stage(
    Key id,
    auto&& func,
    Port<Ins>... dependencies
);
```
- `Out`: output type of the stage  
- `Ins...`: input types, inferred from upstream dependency Ports
- `id`: name of the stage
- `func`: callable to execute per stage
- `dependencies`: upstream dependencies 

Returns a `Result` which either contains a `Port<Out>` or `pipeline::Error`.

On success, returns a `Port<Out>` object to be used to fetch results or wire dependencies to other stages. The typed `Port` object ensures compile time type checking between stages.   

On failure, returns a `pipeline::Error`.


```
Result<Port<std::vector<std::uint8_t>>>
add_file_source_bytes(Key id, const std::string &path)
```
Syntactic sugar to add a stage which reads a file and returns a `std::vector<uint8_t>`.

```
Result<Port<std::vector<std::uint8_t>>>
add_file_source_bytes(Key id, 
                    const std::string &path, 
                    Port<std::monostate> after) 
```
Syntactic sugar to add a stage which reads a file and returns `std::vector<uint8_t>`, executing after a void return-value stage (e.g. a stage which writes to a filepath)

```
Result<Port<std::monostate>>
add_file_sink_bytes(Key id,
                    Port<std::vector<std::uint8_t>> data_port,
                    const std::string& path) 
```
Syntactic sugar to add a stage which inputs a `std::vector<uint8_t>` from a `data_port` and writes to a file.

```
template <class T>
Result<T> run(const Port<T>& output_port);
```  
Executes the minimal upstream subgraph required to compute the requested output

## Features
- DAGs are acyclic by construction, since users can only add dependencies to previously created stages, and cannot create dependencies based on to-be-created stages.  

- Multiple inputs per stage allowed

- Compile-time type checking of pipeline dependencies
  
- Topological execution of stages

## Future Work  
- Option to cache results on reruns (currently each new run discards all previously cached results)
- Soft dependencies, allowing tasks to be skipped on failure
- Retry policy to automatically rerun failed tasks  
