# Node.js Examples - Taguchi Array Tool

Examples for integrating with the Taguchi Array Tool from Node.js using ffi-napi.

## Available Examples

### `basic_examples.js`
- Direct FFI interface to C library functions
- Demonstrates basic operations: list arrays, parse definitions, generate runs
- Shows proper error handling and resource management
- Minimal abstraction, direct API calls

### `advanced_examples.js`
- Higher-level JavaScript-friendly wrapper class around C library
- Promise-based APIs for async operations
- Error handling with JavaScript conventions
- More intuitive interface with familiar patterns

## Requirements

- Node.js (version 14 or later)
- npm packages: `ffi-napi`, `ref-napi`, `ref-array-napi`
- C library built (libtaguchi.so, libtaguchi.dylib, or taguchi.dll)
- Python and build tools for native dependency compilation

## Installing Dependencies

```bash
# Navigate to nodejs examples directory
cd examples/nodejs

# Install dependencies
npm install ffi-napi ref-napi ref-array-napi
```

## Running Examples

```bash
# Run basic examples
node basic_examples.js

# Run advanced examples (when available)
node advanced_examples.js
```

## Node.js Integration Patterns

### Direct FFI Approach
```javascript
const ffi = require('ffi-napi');
const ref = require('ref-napi');

// Define library interface
const taguchiLib = ffi.Library('./libtaguchi.so', {
  'taguchi_list_arrays': ['pointer', []],
  'taguchi_parse_definition': ['pointer', ['string', 'pointer']]
});

// Call functions directly
const arraysPtr = taguchiLib.taguchi_list_arrays();
```

### Wrapper Class Approach
```javascript
const { TaguchiAPI } = require('./taguchi-wrapper');

// Clean, JavaScript-friendly interface
const api = new TaguchiAPI();
const experiment = await api.loadDefinition('experiment.tgu');
const runs = await experiment.generateRuns();
```

## Best Practices

1. **Type Definitions**: Define all function signatures to prevent crashes
2. **Resource Management**: Properly free memory allocated by C library
3. **Error Handling**: Convert C error codes to JavaScript errors/promises
4. **Buffer Management**: Careful handling of C-string buffers and pointers
5. **Async Operations**: Proper async handling for potentially long-running C operations

## Development Notes

- The FFI approach provides access to all C library functionality
- Consider using Node.js native addons for better performance
- Buffer management is critical to prevent memory leaks
- Promise/async patterns make the API more JavaScript-friendly
- Error handling should follow JavaScript conventions