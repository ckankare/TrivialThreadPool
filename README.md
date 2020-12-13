# Description
A simple, straightforward and modern C++ thread pool implementation.

Requires C++20, but can easily be changed to only require C++17.


# Usage
The most simple use case is to launch an asynchronous task, and later retrieves its value.
And if the task has not yet started then run the task on the calling thread.
This is done by the `async` function.
The `async` function supports all kinds of callabels; free functions, lambdas, etc.
```c++
ttp::ThreadPool pool(5);

// Passing a lambda
auto future = pool.async([]() { ... });
...
auto value = future.get();

// The arguments are given after the function to call
auto future = pool.async([](int foo, int bar) { ... }, 10, 20);
...
auto value = future.get();

// Passing a free function
auto future = pool.async(sum, 4, 5);
...
auto value = future.get();

// If no return type, just call 'get' as a synchronization point
auto future = pool.async([]() { ... });
...
future.get();
```


It is also possible to launch a member function, to do this the function and instance should be passed to `async`.
```c++
struct S {
    int foo(int arg) { ... }
};
S s;
ttp::ThreadPool pool(5);
auto future = pool.async(&S::foo, s, 45)
...
auto value = future.get();
```


Because the `Future<T>::get` supports execution of the task if it hasn't already started, it is possible to use the
thread pool from inside a task - nesting async tasks - without causing a deadlock.
```c++
ttp::ThreadPool pool(5);
auto future = pool.async([&]() {
    ...
    auto inner_future = pool.async([] { ... });
    ...
    auto inner_value = inner_future.get();
    ...
    });
...
auto value = future.get();
```


To wait for all tasks to have finished use `ThreadPool::wait`.
```c++
ttp::ThreadPool pool(15);
for (std::size_t i = 0; i <1000; i++) {
    // Launch tasks
    pool.async([&]() { ... });
}
...
// Here we force all the tasks to finish
pool.wait();
// It is equivalent to calling 'get' on all futures returned.
```

Note that the task is owned by both the `Future<T>` and the `ThreadPool`. So a `Future<T>` can outlive the thread pool (however, that is not
considered good practice).

# Installation
Add `TrivialThreadPool` to your project structure, as you see fit. Easiest is just to download the header and add it to one of your
project's include folders (or in its own separate folder, of course). This should work with whatever build system in use.

However, the preferred way would be to use CMake and then include the library as a (third-party) library. Download
the full library with git
```bash
# Assuming third-party libraries are stored under a "third_party"-folder
cd your_project/third_party
git clone https://github.com/ckankare/TrivialThreadPool.git
```
or just download and unpack it.

Then just include it to your project with `add_subdirectory` and add a dependecy to it with `target_link_libraries`.
```cmake
# This in top-level CMakeLists.txt
add_subdirectory(third_party/TrivialThreadPool) # The path to the library

# And this in the executable(s) (or library(s)) CMakeLists.txt that is using the library
target_link_libraries(project_executable TrivialThreadPool::TrivialThreadPool)
```

Note that the library uses threads, so you probably want to make sure that threads are enabled (under Windows that
always are, but under Linux you need to link against `pthread`).
In CMake this is done by something like
```cmake
# Often in the top-level CMakeLists.txt
find_package(Threads)

# In the targets using threads
target_link_libraries(project_executable TrivialThreadPool::TrivialThreadPool Threads::Threads)
```

In a git project you might want to include the project as a `git submodule`.
```bash
cd third_party # Assuming third-party libraries under this folder
git submodule add https://github.com/ckankare/TrivialThreadPool.git
```
which will clone the project to a subdirectory under the current folder.
You might want to fix the submodule to a specific commit, branch or tag; so after the submodule has been cloned
checkout the commit/branch/tag and then commit the change.
```bash
cd TrivialThreadPool
git checkout ... # Whatever should be checked out
cd ..
git add TrivialThreadPool
git commit -m "Set TrivialThreadPool to ..."
```


# Build
Tests are not built by default. To enable them compile with `BUILD_TESTING` set to `ON`
```bash
mkdir build
cd build
cmake -DBUILD_TESTING=ON ..
cmake --build .
./test/unit_tests
# .\test\Debug\unit_tests.exe under Windows
```

Tests are written with [Catch2](https://github.com/catchorg/Catch2), and the full test library is compiled for the tests.


# TODO
* Documentation.
* Add flag for waiting in the destructor.
* Implement `TaskGroup` for easily waiting on a group of tasks/futures at a time.
* Implement `Task` from `std::function` and `std::promise`, instead of wrapping `std::packaged_task`.
* Refactor `Task` to be reusable.
* Maybe lower requirement to C++17 (remove uses of `requires`).
* Add proper support for exceptions.
* Always more tests.
* Some more (at least for debug build) runtime checks.
