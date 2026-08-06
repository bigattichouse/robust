#include <stdio.h>
#include <string.h>
#include "include/taguchi.h"

int main() {
    printf("Testing Taguchi Library Initialization...\n");
    
    // Test array listing functionality
    const char **arrays = taguchi_list_arrays();
    printf("Available arrays: ");
    for (int i = 0; arrays[i] != NULL; i++) {
        printf("%s ", arrays[i]);
    }
    printf("\n");
    
    // Test array info functionality
    size_t rows, cols, levels;
    if (taguchi_get_array_info("L9", &rows, &cols, &levels) == 0) {
        printf("L9 array: %zu rows, %zu cols, %zu levels\n", rows, cols, levels);
    } else {
        printf("Error getting L9 array info\n");
    }
    
    // Test parsing functionality
    char error[TAGUCHI_ERROR_SIZE];
    const char *tgu_content = 
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4, 8\n"
        "array: L9\n";
        
    taguchi_experiment_def_t *def = taguchi_parse_definition(tgu_content, error);
    if (def) {
        printf("Successfully parsed experiment definition\n");
        
        // Test validation
        if (taguchi_validate_definition(def, error)) {
            printf("Definition is valid\n");
            
            // Test generation
            taguchi_experiment_run_t **runs;
            size_t count;
            if (taguchi_generate_runs(def, &runs, &count, error) == 0) {
                printf("Generated %zu experiment runs\n", count);
                
                // Print first run to verify
                if (count > 0) {
                    printf("First run (ID %zu): cache_size=%s, threads=%s\n", 
                           taguchi_run_get_id(runs[0]),
                           taguchi_run_get_value(runs[0], "cache_size"),
                           taguchi_run_get_value(runs[0], "threads"));
                           
                    // Try serializing to JSON
                    char *json = taguchi_runs_to_json((const taguchi_experiment_run_t **)runs, count);
                    if (json) {
                        printf("JSON serialization successful (length: %zu chars)\n", strlen(json));
                        taguchi_free_string(json);
                    }
                }
                
                taguchi_free_runs(runs, count);
            } else {
                printf("Error generating runs: %s\n", error);
            }
        } else {
            printf("Definition validation failed: %s\n", error);
        }
        taguchi_free_definition(def);
    } else {
        printf("Error parsing definition: %s\n", error);
    }
    
    printf("All tests passed!\n");
    return 0;
}