for file in ./traces/*.bz2; do
    echo "Processing: $file"
    bunzip2 -kc "$file" | ./src/predictor --tage
done
