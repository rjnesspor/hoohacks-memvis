## Inspiration
C is a notoriously tricky beast. Often, it's the first language students learn that involves **dynamic memory management**. Because of this, it can be difficult for beginners (and experts alike!) to keep track of complex pointers and abstract heap memory. That's why we built Memory Visualizer (memvis)! 

## What it does
Memvis is divided into two parts, a profiler and a visualizer. Running the profiling tool against a compiled C program will generate a JSON file containing statistics about it. Uploading this JSON file to our intuitive visualizer software will display heaps (get it?) of helpful information about your program! 

Memvis helps you to identify and eliminate memory leaks, rogue functions, and otherwise mischief-making pieces of code.

## How we built it
Memvis is built on a backend written in **C** and **C++**, which handles the profiling and data generation. The frontend is written in **JavaScript** which takes care of parsing the input data and visualizing your program.

## Challenges we ran into

## Accomplishments that we're proud of

## What we learned

## What's next for Memory Visualizer 
