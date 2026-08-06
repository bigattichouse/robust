#!/usr/bin/env node

/**
 * Basic Node.js Example for Taguchi Array Tool
 * Demonstrating the auto-selection feature
 */

// This is a demonstration of how one would use the auto-selection feature
// The actual Node.js implementation would use the function once the library is properly built

console.log("Taguchi Array Tool - Auto-Selection Feature Demo");
console.log("===============================================");

console.log("\\nNew Auto-Selection Capability:");
console.log("- When no array is specified, the tool automatically finds the best array");
console.log("- Based on number of factors and levels per factor");
console.log("- Minimizes number of experimental runs while maintaining orthogonality");

console.log("\\nExample usage patterns:");
console.log("For 3 factors with 2 levels each → Auto-selects L4 (4 runs)");
console.log("For 4 factors with 3 levels each → Auto-selects L9 (9 runs)"); 
console.log("For 13 factors with 3 levels each → Auto-selects L27 (27 runs)");
console.log("For mixed levels → Selects appropriate array (e.g., L16)");

console.log("\\nAPI Functions Added:");
console.log("- taguchi_suggest_optimal_array(def, error_buffer) - Auto-select function");
console.log("- Works with existing definition parsing and validation");
console.log("- Maintains all existing functionality");

console.log("\\nThis removes the guesswork from choosing orthogonal arrays!");