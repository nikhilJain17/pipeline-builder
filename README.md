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
- Separation between control dependencies (must wait for prior task to execute, but do not read in its data) and data dependencies (must wait for prior task to execute and also read in its data)  
- Option to cache results on reruns (currently each new run discards all previously cached results)
- Soft dependencies, allowing tasks to be skipped on failure
- Retry policy to automatically rerun failed tasks  
