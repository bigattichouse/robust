/**
 * Node.js binding for Taguchi Array Tool
 * Using node-ffi-napi to interface with the C library
 */

const ffi = require('ffi-napi');
const ref = require('ref-napi');
const path = require('path');

// Determine library path
let libPath;
if (process.platform === 'darwin') {
  libPath = path.join(__dirname, '..', '..', 'libtaguchi.dylib');
} else if (process.platform === 'win32') {
  libPath = path.join(__dirname, '..', '..', 'taguchi.dll');
} else {
  libPath = path.join(__dirname, '..', '..', 'libtaguchi.so');
}

// Define types for the Taguchi library
const TAGUCHI_ERROR_SIZE = 256;

// Define the library interface
const taguchiLib = ffi.Library(libPath, {
  // Array functions
  'taguchi_list_arrays': ['pointer', []],
  
  // Definition functions
  'taguchi_parse_definition': ['pointer', ['string', 'pointer']],
  'taguchi_create_definition': ['pointer', ['string']],
  'taguchi_add_factor': ['int', ['pointer', 'string', 'pointer', 'size_t', 'pointer']],
  'taguchi_validate_definition': ['bool', ['pointer', 'pointer']],
  'taguchi_free_definition': ['void', ['pointer']],
  
  // Generation functions
  'taguchi_generate_runs': ['int', ['pointer', 'pointer', 'pointer', 'pointer']],
  'taguchi_run_get_value': ['string', ['pointer', 'string']],
  'taguchi_run_get_id': ['size_t', ['pointer']],
  'taguchi_free_runs': ['void', ['pointer', 'size_t']],
  
  // Results and Analysis functions
  'taguchi_create_result_set': ['pointer', ['pointer', 'string']],
  'taguchi_add_result': ['int', ['pointer', 'size_t', 'double', 'pointer']],
  'taguchi_free_result_set': ['void', ['pointer']],
  'taguchi_calculate_main_effects': ['int', ['pointer', 'pointer', 'pointer', 'pointer']]
});

class Taguchi {
  /**
   * Get available array names
   * @returns {string[]} Array of available orthogonal arrays (e.g., ['L4', 'L8', 'L9'])
   */
  static listArrays() {
    const ptr = taguchiLib.taguchi_list_arrays();
    const arrays = [];
    let i = 0;
    
    while (true) {
      const strPtr = ref.readPointer(ptr, i * ref.sizeof.pointer);
      if (strPtr.isNull()) break;
      arrays.push(ref.readCString(strPtr, 0));
      i++;
    }
    
    return arrays;
  }

  /**
   * Parse a .tgu file content string into an experiment definition
   * @param {string} content - Content of .tgu file as string
   * @returns {Object|null} Experiment definition handle or null on error
   */
  static parseDefinition(content) {
    const errorBuf = Buffer.alloc(TAGUCHI_ERROR_SIZE);
    const defHandle = taguchiLib.taguchi_parse_definition(content, errorBuf);

    if (defHandle.isNull()) {
      throw new Error(`Parse error: ${ref.readCString(errorBuf)}`);
    }

    return defHandle;
  }

  /**
   * Add a factor to an experiment definition
   * @param {Object} defHandle - Definition handle from parseDefinition
   * @param {string} name - Factor name
   * @param {string[]} levels - Array of level values
   * @returns {boolean} true on success, false on error
   */
  static addFactor(defHandle, name, levels) {
    const errorBuf = Buffer.alloc(TAGUCHI_ERROR_SIZE);
    
    // Convert levels array to a pointer array
    const levelCount = levels.length;
    const levelsPtr = ref.alloc('pointer');
    
    // Create array of strings
    const ptrArray = [];
    for (const level of levels) {
      ptrArray.push(Buffer.from(level + '\x00'));
    }
    
    // Create array of pointers to the strings
    const levelPtrs = Buffer.alloc(ref.sizeof.pointer * levelCount);
    for (let i = 0; i < levelCount; i++) {
      ref.writePointer(levelPtrs, i * ref.sizeof.pointer, ptrArray[i]);
    }
    
    const result = taguchiLib.taguchi_add_factor(defHandle, name, levelPtrs, levelCount, errorBuf);
    
    if (result !== 0) {
      throw new Error(`Add factor error: ${ref.readCString(errorBuf)}`);
    }
    
    return true;
  }

  /**
   * Validate an experiment definition
   * @param {Object} defHandle - Definition handle
   * @returns {boolean} true if valid, false if not
   */
  static validateDefinition(defHandle) {
    const errorBuf = Buffer.alloc(TAGUCHI_ERROR_SIZE);
    return taguchiLib.taguchi_validate_definition(defHandle, errorBuf);
  }

  /**
   * Generate runs from an experiment definition
   * @param {Object} defHandle - Definition handle
   * @returns {Array} Array of run objects
   */
  static generateRuns(defHandle) {
    const errorBuf = Buffer.alloc(TAGUCHI_ERROR_SIZE);
    const runsPtr = ref.alloc('pointer');
    const count = ref.alloc('size_t');

    const result = taguchiLib.taguchi_generate_runs(
      defHandle,
      runsPtr,
      count,
      errorBuf
    );

    if (result !== 0) {
      throw new Error(`Generate error: ${ref.readCString(errorBuf)}`);
    }

    const runCount = count.deref();
    const runs = [];
    
    // Extract run information from the returned array
    // Note: This is a simplified approach; in a real implementation,
    // you would need more complex logic to extract individual runs
    for (let i = 0; i < runCount; i++) {
      const runPtr = ref.readPointer(runsPtr.deref(), i * ref.sizeof.pointer);
      runs.push({
        id: taguchiLib.taguchi_run_get_id(runPtr)
      });
    }

    return runs;
  }

  /**
   * Free an experiment definition
   * @param {Object} defHandle - Definition handle to free
   */
  static freeDefinition(defHandle) {
    if (defHandle) {
      taguchiLib.taguchi_free_definition(defHandle);
    }
  }
}

module.exports = { Taguchi, taguchiLib };

// Export default for easier usage
module.exports.default = Taguchi;