/* config.h - Build configuration */
#ifndef CONFIG_H
#define CONFIG_H

#define MAX_FACTORS 256

/*
 * Noise factors get their own, much smaller cap.
 *
 * They are few by nature: the outer array is the full factorial of them, so
 * eight two-level noise factors is already 256 runs per control row. Sizing
 * this at MAX_FACTORS would be meaningless AND expensive -- Factor is ~3.5 KB,
 * so 256 of them add ~900 KB to ExperimentDef, which is declared as a stack
 * local in several tests. That is exactly what happened: the struct doubled to
 * 1.8 MB and the coverage build (-O0, no eliding) started returning a
 * different design than the optimised build for the same input.
 */
#define MAX_NOISE_FACTORS 8
#define MAX_LEVELS 27
#define MAX_FACTOR_NAME 64
#define MAX_LEVEL_VALUE 128
#define MAX_EXPERIMENTS 8192
#define BUFFER_SIZE 8192

#endif /* CONFIG_H */
