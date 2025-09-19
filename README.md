# @isopodlabs/napi
[![npm version](https://img.shields.io/npm/v/@isopodlabs/napi.svg)](https://www.npmjs.com/package/@isopodlabs/napi)
[![GitHub stars](https://img.shields.io/github/stars/adrianstephens/napi.svg?style=social)](https://github.com/adrianstephens/napi)
[![License](https://img.shields.io/npm/l/@isopodlabs/napi.svg)](LICENSE.txt)

C++ helpers for Node.js Native API (N-API) development, providing a modern C++ wrapper around the Node.js C API.

## ☕ Support My Work
If you use this package, consider [buying me a cup of tea](https://coff.ee/adrianstephens) to support future updates!

## Features

- **Modern C++ wrapper** - Clean, type-safe C++ interface over raw N-API
- **Automatic type conversion** - Seamless conversion between C++ and JavaScript types
- **Template-based callbacks** - Automatic function binding with type deduction
- **Memory management** - RAII-based resource management for handles and references
- **Error handling** - Integrated error checking and reporting
- **Cross-platform** - Works on Windows, macOS, and Linux

## Installation

```sh
npm install @isopodlabs/napi
```

## Usage

### Basic Setup

```cpp
#include "node.h"

auto HelloWorld() {
    return "Hello, World!";
}

napi_value Init(napi_env env, napi_value exports) {
    Node::global_env = env;
    
    Node::object(exports).defineProperties({
        {"HelloWorld", Node::function::make<HelloWorld>()},
    });
    
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
```

### Type Conversions

The library provides automatic conversion between C++ and JavaScript types:

```cpp
// Numbers
Node::number jsNum(42);
int cppNum = jsNum;

// Strings  
Node::string jsStr("Hello");
auto cppStr = jsStr.get_utf8(buffer, size);

// Booleans
Node::boolean jsBool(true);
bool cppBool = jsBool;

// Arrays
Node::array jsArray;
jsArray.push(Node::number(1));
jsArray.push(Node::string("test"));
```

### Function Binding

Automatically bind C++ functions to JavaScript:

```cpp
// Simple function
int add(int a, int b) {
    return a + b;
}

// Method binding
class Calculator {
public:
    int multiply(int a, int b) { return a * b; }
};

// In Init function:
Node::object(exports).defineProperties({
    {"add", Node::function::make<add>()},
    {"Calculator", Node::Class<Calculator>::constructor()},
});
```

### Class Wrapping

Expose C++ classes to JavaScript:

```cpp
class MyClass {
public:
    MyClass(int value) : value_(value) {}
    
    int getValue() const { return value_; }
    void setValue(int value) { value_ = value; }
    
private:
    int value_;
};

// Define class properties
template<> Node::Constructor Node::define<MyClass>() {
    return Node::ClassDefinition<MyClass, int>("MyClass", {
        Node::field<&MyClass::getValue>("getValue"),
        Node::field<&MyClass::setValue>("setValue"),
    });
}
```

### Memory Management

```cpp
// Automatic scope management
{
    Node::scope scope;
    // All handles created here are automatically cleaned up
    auto obj = Node::object();
    auto arr = Node::array(10);
} // Scope automatically closed

// References for long-term storage
Node::ref persistent_ref(some_value);
// Use *persistent_ref to access the value
```

### Async Operations

```cpp
// Async work with automatic cleanup
auto work = Node::async_work("background_task",
    []() {
        // Background thread work
        return expensive_computation();
    },
    [](napi_status status, auto result) {
        // Main thread completion
        if (status == napi_ok) {
            // Handle result
        }
    }
);
```

## API Reference

### Core Classes

- **`Node::value`** - Base class for all JavaScript values
- **`Node::object`** - JavaScript objects with property access
- **`Node::array`** - JavaScript arrays with indexed access  
- **`Node::function`** - JavaScript functions with call support
- **`Node::string`** - JavaScript strings with encoding support
- **`Node::number`** - JavaScript numbers with type conversion

### Type System

- **`Node::boolean`** - JavaScript boolean values
- **`Node::symbol`** - JavaScript symbols
- **`Node::Promise`** - JavaScript promises with resolve/reject
- **`Node::ArrayBuffer`** - Binary data buffers
- **`Node::TypedArray<T>`** - Typed array views

### Memory Management

- **`Node::ref`** - Persistent references to JavaScript values
- **`Node::scope`** - Handle scope management
- **`Node::escapable_scope`** - Escapable handle scopes

### Utilities

- **`Node::environment`** - N-API environment wrapper
- **`Node::callback`** - Function callback helpers
- **`Node::property`** - Property descriptor builder

## Advanced Features

### Custom Type Conversion

```cpp
// Define conversion for custom types
template<> struct Node::node_type<MyCustomType> {
    static napi_value to_value(const MyCustomType& x) {
        // Convert C++ type to JavaScript
    }
    static MyCustomType from_value(napi_value x) {
        // Convert JavaScript to C++ type
    }
};
```

### Error Handling

```cpp
// Automatic error checking
if (!Node::global_env.check(status)) {
    // Error occurred, details logged automatically
    return nullptr;
}

// Custom errors
throw Node::error("MyError", "Something went wrong");
```

## Building

The library requires:
- Node.js development headers (a download script is provided)
- C++17 compatible compiler
- node-gyp and CMake are not needed, use whatever build system you prefer

### Headers

A script is supplied for downloading the node headers and libraries. Use one of the following forms:

```
node get-headers.js <path to vscode.exe> <output path>
node get-headers.js electron:<Version> <output path>
node get-headers.js node:<Version> <output path>
node get-headers.js <output path>
```
### VS Code Extension Development

For building VS Code extensions, provide the path to the vscode executable for the script to determine the node version (see below to add it as a vscode task).

```sh
node get-headers.js "C:\Program Files\Microsoft VS Code\Code.exe" ./electron
```

in .vscode/tasks.json
```json
{
	"version": "2.0.0",
	"tasks": [
		{
			"label": "Get Native Dev Headers for VS Code",
			"type": "shell",
			"command": "node",
			"args": ["get-headers.js", "${execPath}", "electron"],
			"problemMatcher": []
		},
		{
			"type": "shell",
			"label": "Build NAPI",
			"group": "build",
			"command": "clang-cl",
			"args": [
				"-I", "include",
				"-I", "electron\\include\\node",
				"/LD", "-g", "-std:c++17", "/TP",
				"example\\example.cpp",
				"-DWIN32_LEAN_AND_MEAN", "-D_CRT_SECURE_NO_WARNINGS", "-DNOMINMAX",
				"-o", "out\\example.node",
				"-link", "/DELAYLOAD:node.exe", "electron\\node.lib"
			],

			"osx": {
				"command": "clang++",
				"args": [
					"-I", "include",
					"-I", "electron/include/node",
					"-shared", "-fPIC", "-std=c++17",
					"example/example.cpp",
					"-o", "out/example.node",
					"-undefined", "dynamic_lookup"
				]
			},
			"linux": {
				"command": "g++",
				"args": [
					"-I", "include",
					"-I", "electron/include/node",
					"-shared", "-fPIC", "-std=c++17",
					"example/example.cpp",
					"-o", "out/example.node"
				]
			}
		}
	]
}
```


## Platform Support

- **Windows** - Any C++17 compatible compiler (clang-cl tested)
- **macOS** - Any C++17 compatible compiler  
- **Linux** - Any C++17 compatible compiler

## License

This project is licensed under the MIT License.