#!/bin/bash
# generate_recipe_cards.sh
# Generates printable recipe cards for manual cookie experiment execution

if [ ! -f "cookie_experiment.tgu" ]; then
    echo "Creating sample cookie experiment definition..."
    cat > cookie_experiment.tgu << 'EOF'
factors:
  butter: 1/2 cup, 3/4 cup, 1 cup
  sugar_ratio: 1:1 brown:white, 2:1 brown:white, 3:1 brown:white
  flour_type: all_purpose, bread_flour, cake_flour
  eggs: 1, 2, 3
  chips: 1/2 cup, 3/4 cup, 1 cup
  baking_temp: 325F, 350F, 375F
  baking_time: 8min, 10min, 12min
array: L27
EOF
fi

# Make sure we have the library and binary
if [ ! -f "./taguchi" ]; then
    if [ -f "../taguchi" ]; then
        cp ../taguchi .
    elif [ -f "../../taguchi" ]; then
        cp ../../taguchi .
    else
        echo "Error: taguchi binary not found"
        exit 1
    fi
fi

EXPERIMENT_FILE="cookie_experiment.tgu"
OUTPUT_FILE="recipe_cards.txt"

echo "CHOCOLATE CHIP COOKIE EXPERIMENT RECIPE CARDS" > "$OUTPUT_FILE"
echo "===========================================" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"
echo "Generated: $(date)" >> "$OUTPUT_FILE"
echo "Total runs: 27" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Generate runs using our taguchi tool
echo "Generating experimental runs..." >&2
./taguchi generate "$EXPERIMENT_FILE" > temp_runs.txt 2>/dev/null

# Process the generated runs into recipe cards
while IFS= read -r line; do
    if [[ $line =~ ^Run\ ([0-9]+):\ (.+)$ ]]; then
        run_id="${BASH_REMATCH[1]}"
        factors="${BASH_REMATCH[2]}"
        
        echo "RECIPE CARD $run_id" >> "$OUTPUT_FILE"
        echo "=============" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        
        echo "INGREDIENT MEASUREMENTS:" >> "$OUTPUT_FILE"
        
        # Split the factors string by ', '
        IFS=',' read -ra factor_array <<< "$factors"
        for factor in "${factor_array[@]}"; do
            # Remove leading whitespace
            factor=$(echo "$factor" | sed 's/^ *//')
            
            if [[ $factor == *"="* ]]; then
                name=$(echo "$factor" | cut -d'=' -f1)
                value=$(echo "$factor" | cut -d'=' -f2-)
                
                # Convert factor names to more readable form
                case "$name" in
                    "butter") echo "  - Butter: $value" >> "$OUTPUT_FILE" ;;
                    "sugar_ratio") echo "  - Sugar Ratio: $value" >> "$OUTPUT_FILE" ;;
                    "flour_type") echo "  - Flour Type: $value" >> "$OUTPUT_FILE" ;;
                    "eggs") echo "  - Eggs: $value" >> "$OUTPUT_FILE" ;;
                    "chips") echo "  - Chocolate Chips: $value" >> "$OUTPUT_FILE" ;;
                    "baking_temp") echo "  - Oven Temperature: $value" >> "$OUTPUT_FILE" ;;
                    "baking_time") echo "  - Baking Time: $value" >> "$OUTPUT_FILE" ;;
                    *) echo "  - $name: $value" >> "$OUTPUT_FILE" ;;
                esac
            fi
        done
        echo "" >> "$OUTPUT_FILE"
        
        echo "INSTRUCTIONS:" >> "$OUTPUT_FILE"
        echo "  1. Gather ingredients according to measurements above" >> "$OUTPUT_FILE"
        echo "  2. Preheat oven to specified temperature" >> "$OUTPUT_FILE"
        echo "  3. Prepare dough following standard technique" >> "$OUTPUT_FILE"
        echo "  4. Bake for specified time until edges are golden" >> "$OUTPUT_FILE"
        echo "  5. Cool completely before evaluation" >> "$OUTPUT_FILE"
        echo "  6. Rate cookies and record results in results_template.csv" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        
        echo "RATING SHEET:" >> "$OUTPUT_FILE"
        echo "  Taste Score (1-10): _______" >> "$OUTPUT_FILE"
        echo "  Texture Score (1-10): _______" >> "$OUTPUT_FILE"
        echo "  Yield Count: _______" >> "$OUTPUT_FILE" 
        echo "  Appearance Score (1-10): _______" >> "$OUTPUT_FILE"
        echo "  Notes: _________________________" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        
        echo "----------------------------------------" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    fi
done < temp_runs.txt

rm temp_runs.txt 2>/dev/null || true

echo "Recipe cards generated: $OUTPUT_FILE" >&2
echo "Print these cards and use them to execute your cookie experiments systematically." >&2
echo "Complete all 27 runs then analyze results with:" >&2
echo "  ./taguchi analyze cookie_experiment.tgu results.csv" >&2
echo "" >&2
echo "Results should be in CSV format like:" >&2
echo "run_id,taste_score,texture_score,yield_count,appearance_score" >&2