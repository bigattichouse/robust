#!/usr/bin/env python3
"""
Taguchi Array Tool - Quick Demo
"""

from taguchi import Experiment, Analyzer


def demo_basic():
    print("=" * 60)
    print("DEMO: Taguchi Experiment Generation")
    print("=" * 60)

    with Experiment() as exp:
        exp.add_factor("learning_rate", ["0.001", "0.01", "0.1"])
        exp.add_factor("batch_size", ["32", "64", "128"])
        exp.add_factor("weight_decay", ["0.0", "0.1", "0.2"])

        runs = exp.generate()
        print(f"Array: {exp.array_type}, Runs: {exp.num_runs}")
        print(f"\nFirst 3 runs:")
        for run in runs[:3]:
            print(f"  Run {run['run_id']}: {run['factors']}")


def demo_analysis():
    print("\n" + "=" * 60)
    print("DEMO: Analysis with Simulated Results")
    print("=" * 60)

    # Simulated val_bpb results (lower is better)
    results = {1: 1.05, 2: 1.02, 3: 1.08, 4: 1.03, 5: 0.998,
               6: 1.045, 7: 1.04, 8: 1.015, 9: 1.055}

    with Experiment() as exp:
        exp.add_factor("depth", ["4", "6", "8"])
        exp.add_factor("lr", ["0.02", "0.04", "0.08"])
        exp.generate()

        with Analyzer(exp, metric_name="val_bpb") as analyzer:
            analyzer.add_results_from_dict(results)
            print(analyzer.summary())
            print("\nRecommended (lower val_bpb is better):")
            optimal = analyzer.recommend_optimal(higher_is_better=False)
            for factor, level in optimal.items():
                print(f"  {factor}: {level}")


if __name__ == "__main__":
    demo_basic()
    demo_analysis()
    print("\n" + "=" * 60)
    print("Demo complete!")
    print("=" * 60)
