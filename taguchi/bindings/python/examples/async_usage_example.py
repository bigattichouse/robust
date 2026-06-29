#!/usr/bin/env python3
"""
Async Usage Example

This example demonstrates the AsyncTaguchi class for non-blocking
operations, useful for long-running experiments or async frameworks.
"""

import asyncio
import sys
from pathlib import Path

# Add taguchi package to path for examples
sys.path.insert(0, str(Path(__file__).parent.parent))

from taguchi import AsyncTaguchi, TaguchiConfig, Experiment


async def basic_async_example():
    """Basic async operations example."""
    print("=== Basic Async Example ===")
    
    # Create async Taguchi with custom config
    config = TaguchiConfig(cli_timeout=30, debug_mode=True)
    async_taguchi = AsyncTaguchi(config=config)
    
    try:
        # List arrays asynchronously
        print("Fetching available arrays...")
        arrays = await async_taguchi.list_arrays_async()
        print(f"✓ Found {len(arrays)} arrays: {', '.join(arrays)}")
        
        # You can perform other async operations while waiting
        print("✓ Async operation completed successfully")
        return True
        
    except Exception as e:
        print(f"Error: {e}")
        return False


async def concurrent_operations_example():
    """Example of running multiple async operations concurrently."""
    print("\n=== Concurrent Operations Example ===")
    
    config = TaguchiConfig(cli_timeout=60)
    
    async def list_arrays():
        """List available arrays."""
        async_taguchi = AsyncTaguchi(config=config)
        arrays = await async_taguchi.list_arrays_async()
        return f"Arrays: {', '.join(arrays)}"
    
    async def analyze_experiment(experiment_data):
        """Analyze an experiment asynchronously."""
        async_taguchi = AsyncTaguchi(config=config)
        # Simulate analysis operation
        await asyncio.sleep(0.1)  # Simulate processing time
        return f"Analysis complete for experiment {experiment_data['name']}"
    
    async def generate_runs(tgu_content):
        """Generate runs asynchronously."""
        async_taguchi = AsyncTaguchi(config=config)
        runs = await async_taguchi.generate_runs_async(tgu_content)
        return f"Generated {len(runs)} runs"
    
    try:
        # Prepare test data
        tgu_content = """
factors:
  temperature: low, high
  pressure: low, high
"""
        experiment_data = {"name": "Test Experiment 1"}
        
        # Run multiple operations concurrently
        print("Starting concurrent operations...")
        
        tasks = [
            list_arrays(),
            analyze_experiment(experiment_data),
            generate_runs(tgu_content)
        ]
        
        # Wait for all operations to complete
        results = await asyncio.gather(*tasks, return_exceptions=True)
        
        print("✓ Concurrent operations completed:")
        for i, result in enumerate(results):
            if isinstance(result, Exception):
                print(f"  Task {i+1}: Error - {result}")
            else:
                print(f"  Task {i+1}: {result}")
        
        return True
        
    except Exception as e:
        print(f"Error: {e}")
        return False


async def async_with_sync_integration():
    """Example of integrating async and sync operations."""
    print("\n=== Async-Sync Integration Example ===")
    
    try:
        # Create experiment using sync API
        with Experiment() as exp:
            exp.add_factor("temperature", ["low", "medium", "high"])
            exp.add_factor("time", ["5min", "10min"])
            
            print(f"✓ Experiment created with {exp.factor_count} factors")
            
            # Get TGU content for async processing
            tgu_path = exp.get_tgu_path()
            
            # Process asynchronously
            async_taguchi = AsyncTaguchi()
            
            print("Processing experiment asynchronously...")
            runs = await async_taguchi.generate_runs_async(tgu_path)
            print(f"✓ Generated {len(runs)} runs asynchronously")
            
            # Continue with sync processing
            print("Continuing with synchronous analysis...")
            # In a real scenario, you'd add actual experimental results here
            
        return True
        
    except Exception as e:
        print(f"Error: {e}")
        return False


async def async_error_handling_example():
    """Example of error handling in async operations."""
    print("\n=== Async Error Handling Example ===")
    
    # Example: Timeout handling
    config = TaguchiConfig(cli_timeout=1)  # Very short timeout
    async_taguchi = AsyncTaguchi(config=config)
    
    try:
        print("Testing timeout handling...")
        # This might timeout in real scenarios with slow operations
        await async_taguchi.list_arrays_async()
        print("✓ Operation completed within timeout")
        
    except asyncio.TimeoutError:
        print("✓ Caught timeout error as expected")
    except Exception as e:
        print(f"Caught error: {type(e).__name__}: {e}")
        if hasattr(e, 'suggestions'):
            print(f"Suggestions: {e.suggestions}")
    
    # Example: Invalid command handling
    try:
        print("Testing invalid command handling...")
        await async_taguchi._run_command_async(['invalid-command'])
        
    except Exception as e:
        print(f"✓ Caught command error: {type(e).__name__}")
        if hasattr(e, 'operation'):
            print(f"  Operation: {e.operation}")
    
    return True


async def async_workflow_example():
    """Complete async workflow example."""
    print("\n=== Complete Async Workflow Example ===")
    
    config = TaguchiConfig(debug_mode=True, cli_timeout=30)
    
    try:
        # Step 1: Validate environment asynchronously
        print("Step 1: Validating environment...")
        async_taguchi = AsyncTaguchi(config=config)
        
        # Check available arrays
        arrays = await async_taguchi.list_arrays_async()
        print(f"✓ Environment validated, {len(arrays)} arrays available")
        
        # Step 2: Process multiple experiments concurrently
        print("Step 2: Processing multiple experiments...")
        
        experiments = [
            {
                "name": "Experiment A",
                "tgu": """
factors:
  temperature: low, high
  pressure: low, high
"""
            },
            {
                "name": "Experiment B", 
                "tgu": """
factors:
  catalyst: A, B, C
  time: short, long
"""
            }
        ]
        
        async def process_experiment(exp_data):
            """Process a single experiment."""
            print(f"  Processing {exp_data['name']}...")
            runs = await async_taguchi.generate_runs_async(exp_data['tgu'])
            return {
                "name": exp_data['name'],
                "runs": len(runs),
                "status": "completed"
            }
        
        # Process experiments concurrently
        experiment_tasks = [
            process_experiment(exp) for exp in experiments
        ]
        
        results = await asyncio.gather(*experiment_tasks)
        
        print("✓ Experiment processing completed:")
        for result in results:
            print(f"  {result['name']}: {result['runs']} runs, {result['status']}")
        
        # Step 3: Concurrent analysis (simulated)
        print("Step 3: Running analyses...")
        
        async def analyze_results(exp_result):
            """Simulate analysis of experiment results."""
            await asyncio.sleep(0.1)  # Simulate analysis time
            return f"{exp_result['name']} analysis: optimal settings found"
        
        analysis_tasks = [analyze_results(result) for result in results]
        analyses = await asyncio.gather(*analysis_tasks)
        
        print("✓ Analyses completed:")
        for analysis in analyses:
            print(f"  {analysis}")
        
        return True
        
    except Exception as e:
        print(f"Error in async workflow: {e}")
        return False


async def main():
    """Main async function to run all examples."""
    print("AsyncTaguchi Example Usage")
    print("=" * 40)
    
    # Check if asyncio is working correctly
    print(f"✓ Running on Python {sys.version_info.major}.{sys.version_info.minor}")
    print(f"✓ Asyncio event loop: {type(asyncio.get_event_loop()).__name__}")
    
    # Run all async examples
    examples = [
        ("Basic Async", basic_async_example),
        ("Concurrent Operations", concurrent_operations_example),
        ("Async-Sync Integration", async_with_sync_integration),
        ("Async Error Handling", async_error_handling_example),
        ("Complete Workflow", async_workflow_example),
    ]
    
    results = {}
    for name, example_func in examples:
        try:
            print(f"\n{'='*15} {name} {'='*15}")
            success = await example_func()
            results[name] = success
            if success:
                print(f"✓ {name} completed successfully")
            else:
                print(f"✗ {name} failed")
        except Exception as e:
            print(f"✗ {name} failed with exception: {e}")
            results[name] = False
    
    # Summary
    print(f"\n{'='*40}")
    print("Summary:")
    for name, success in results.items():
        status = "✓" if success else "✗"
        print(f"  {status} {name}")
    
    successful = sum(results.values())
    total = len(results)
    print(f"\nCompleted: {successful}/{total} async examples")


if __name__ == "__main__":
    # Run the async main function
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExample interrupted by user")
    except Exception as e:
        print(f"Failed to run async examples: {e}")