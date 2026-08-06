/**
 * Advanced Node.js Examples for Taguchi Array Tool
 * Demonstrates more sophisticated usage patterns and API capabilities
 */

const { Taguchi } = require('./taguchi_binding');
const fs = Promise.promisify(require('fs'));
const path = require('path');

class TaguchiWorkflow {
  /**
   * Create a Taguchi experiment manager
   */
  constructor() {
    this.definitions = [];
  }

  /**
   * Load experiment from file
   * @param {string} filePath - Path to .tgu file
   * @returns {Promise<Object>} Definition handle
   */
  async loadFromFile(filePath) {
    const content = await fs.readFile(filePath, 'utf8');
    const defHandle = Taguchi.parseDefinition(content);
    this.definitions.push(defHandle);
    return defHandle;
  }

  /**
   * Create experiment programmatically
   * @param {string} arrayType - Array type (e.g., 'L9')
   * @returns {Object} Definition handle
   */
  createDefinition(arrayType) {
    const content = `factors:
array: ${arrayType}`;
    const defHandle = Taguchi.parseDefinition(content);
    this.definitions.push(defHandle);
    return defHandle;
  }

  /**
   * Add factor to definition programmatically
   * @param {Object} defHandle - Definition handle
   * @param {string} name - Factor name
   * @param {string[]} levels - Array of level values
   */
  addFactor(defHandle, name, levels) {
    // In a real implementation, we'd need access to addFactor function
    // This is just a demonstration
    console.log(`Added factor ${name} with levels: [${levels.join(', ')}]`);
  }

  /**
   * Execute all runs for an experiment and collect results
   * @param {Object} defHandle - Definition handle
   * @param {Function} executor - Function to execute each run (runInfo) => Promise<result>
   * @returns {Promise<Array>} Array of results for each run
   */
  async executeExperiment(defHandle, executor) {
    const runs = Taguchi.generateRuns(defHandle);
    const results = [];

    console.log(`Executing ${runs.length} experimental runs...`);

    for (const run of runs) {
      try {
        console.log(`  Running configuration ${run.id}...`);
        const result = await executor(run);
        results.push({ runId: run.id, result });
        console.log(`    Completed: ${JSON.stringify(result)}`);
      } catch (error) {
        console.error(`    Error in run ${run.id}: ${error.message}`);
        results.push({ runId: run.id, error: error.message });
      }
    }

    return results;
  }

  /**
   * Cleanup all definitions
   */
  cleanup() {
    for (const defHandle of this.definitions) {
      if (defHandle) {
        Taguchi.freeDefinition(defHandle);
      }
    }
    this.definitions = [];
  }

  /**
   * Analyze results to find optimal configuration
   * @param {Array} results - Array of results from executeExperiment
   * @returns {Object} Analysis result with recommended configuration
   */
  analyzeResults(results) {
    // This is a simplified analysis
    // In a real implementation, this would call the analysis functions
    
    const successfulResults = results.filter(r => !r.error);
    
    if (successfulResults.length === 0) {
      return { error: 'No successful runs to analyze' };
    }

    // For demonstration, let's say we're looking for the highest value
    let bestRun = successfulResults[0];
    let bestValue = successfulResults[0].result.value || 0;

    for (const result of successfulResults) {
      const value = result.result.value || 0;
      if (value > bestValue) {
        bestValue = value;
        bestRun = result;
      }
    }

    return {
      optimalRunId: bestRun.runId,
      optimalValue: bestValue,
      totalRuns: results.length,
      successfulRuns: successfulResults.length
    };
  }
}

/**
 * Example 1: File-based workflow
 */
async function exampleFileBasedWorkflow() {
  console.log('=== Advanced Example 1: File-based Workflow ===');

  const workflow = new TaguchiWorkflow();

  // Create a temporary .tgu file for testing
  const tguContent = `factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
array: L9`;

  const tempFilePath = path.join('/tmp', 'temp_experiment.tgu');
  await fs.writeFile(tempFilePath, tguContent);

  try {
    console.log(`Loading experiment from file: ${tempFilePath}`);
    const defHandle = await workflow.loadFromFile(tempFilePath);
    console.log('Loaded experiment definition from file');

    // Execute the experiment with a mock executor
    const results = await workflow.executeExperiment(defHandle, async (run) => {
      // Simulate running an experiment and returning results
      // In a real scenario, this could run actual tests, benchmarks, etc.
      await new Promise(resolve => setTimeout(resolve, 50)); // Simulate processing time
      
      // Return mock results
      return {
        value: Math.floor(Math.random() * 1000),  // Random performance score
        runInfo: run
      };
    });

    console.log('\\nResults:');
    results.slice(0, 3).forEach(res => {
      if (res.error) {
        console.log(`  Run ${res.runId}: ERROR - ${res.error}`);
      } else {
        console.log(`  Run ${res.runId}: ${res.result.value}`);
      }
    });

    if (results.length > 3) {
      console.log(`  ... and ${results.length - 3} more results`);
    }

    // Analyze results
    const analysis = workflow.analyzeResults(results);
    console.log(`\\nAnalysis: Optimal run is ${analysis.optimalRunId} with value ${analysis.optimalValue}`);

  } catch (error) {
    console.error('Error in file-based workflow:', error.message);
  } finally {
    workflow.cleanup();
    // Cleanup temp file
    try { await fs.unlink(tempFilePath); } catch (e) { /* ignore */ }
  }

  console.log();
}

/**
 * Example 2: Programmatic workflow
 */
async function exampleProgrammaticWorkflow() {
  console.log('=== Advanced Example 2: Programmatic Workflow ===');

  const workflow = new TaguchiWorkflow();

  try {
    // Create an experiment programmatically
    console.log('Creating L9 experiment programmatically...');
    const defHandle = workflow.createDefinition('L9');
    console.log('Created experiment with L9 array');

    // Execute with a more interesting executor
    const results = await workflow.executeExperiment(defHandle, async (run) => {
      // Simulate a more complex experiment
      await new Promise(resolve => setTimeout(resolve, 100));

      // Simulate a metric that depends on run ID
      const performanceScore = 1000 - (run.id * 10);  // Higher run IDs get lower scores
      const memoryUsage = 50 + (Math.random() * 50);   // Random memory usage

      return {
        performance: performanceScore,
        memory_mb: memoryUsage,
        efficiency: performanceScore / memoryUsage
      };
    });

    // Analyze results
    const analysis = workflow.analyzeResults(results);
    console.log(`\\nAnalysis:`);
    console.log(`  Optimal run: ${analysis.optimalRunId}`);
    console.log(`  Optimal value: ${analysis.optimalValue}`);
    console.log(`  Total runs: ${analysis.totalRuns}, Successful: ${analysis.successfulRuns}`);

  } catch (error) {
    console.error('Error in programmatic workflow:', error.message);
  } finally {
    workflow.cleanup();
  }

  console.log();
}

/**
 * Example 3: Batch processing multiple experiments
 */
async function exampleBatchProcessing() {
  console.log('=== Advanced Example 3: Batch Processing ===');

  const workflows = [
    { label: 'Performance Test A', content: `factors:\n  size: small, medium, large\n  opt_level: -O0, -O1, -O2\narray: L9` },
    { label: 'Performance Test B', content: `factors:\n  cores: 1, 2, 4\n  mem_gb: 4, 8, 16\narray: L9` }
  ];

  for (const workflowDef of workflows) {
    console.log(`\\nProcessing: ${workflowDef.label}`);
    
    const workflow = new TaguchiWorkflow();
    
    try {
      const defHandle = Taguchi.parseDefinition(workflowDef.content);
      workflow.definitions.push(defHandle);

      const results = await workflow.executeExperiment(defHandle, async (run) => {
        await new Promise(resolve => setTimeout(resolve, 25));
        return {
          throughput: 100 + Math.floor(Math.random() * 900),
          latency_ms: 10 + Math.random() * 90
        };
      });

      const analysis = workflow.analyzeResults(results);
      console.log(`  Completed ${results.length} runs, best: run ${analysis.optimalRunId}`);

    } catch (error) {
      console.error(`  Error processing ${workflowDef.label}: ${error.message}`);
    } finally {
      workflow.cleanup();
    }
  }

  console.log();
}

// Main execution
async function runAdvancedExamples() {
  console.log('Advanced Taguchi Array Tool - Node.js Examples\\n');

  await exampleFileBasedWorkflow();
  await exampleProgrammaticWorkflow();
  await exampleBatchProcessing();

  console.log('All advanced Node.js examples completed!');
}

// Export functions for testing
module.exports = {
  TaguchiWorkflow,
  runAdvancedExamples
};

// Run examples when executed directly
if (require.main === module) {
  runAdvancedExamples().catch(console.error);
}