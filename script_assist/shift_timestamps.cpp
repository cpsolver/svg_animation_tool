//  shift_timestamps.cpp
//  --------------------

//  Modifies timestamps in script.
//
//  Comments below describe what the current
//  version of this code does.
//
//  Local file input.txt must contain a copy of the
//  script.  Modified script written to output.txt
//  file.  View output file before copying script
//  to overwrite actual script file.
//
//  Compile:
//    g++ -o shift_timestamps shift_timestamps.cpp
//  Run:
//    ./shift_timestamps


#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    std::ifstream input_file("input.txt");
    if (!input_file.is_open()) {
        std::cerr << "Could not open input.txt" << std::endl;
        return 1;
    }

    std::vector<std::string> all_lines;
    std::string current_line;
    while (std::getline(input_file, current_line)) {
        all_lines.push_back(current_line);
    }
    input_file.close();

    // Find indices of lines beginning with "desired-timestamp"
    std::vector<int> timestamp_line_indices;
    for (int i = 0; i < (int)all_lines.size(); i++) {
        if (all_lines[i].rfind("desired-timestamp", 0) == 0) {
            timestamp_line_indices.push_back(i);
        }
    }

    // Extract the second space-delimited word from each timestamp line
    std::vector<std::string> second_words;
    for (int idx : timestamp_line_indices) {
        std::istringstream line_stream(all_lines[idx]);
        std::string first_word, second_word;
        line_stream >> first_word >> second_word;
        second_words.push_back(second_word);
    }

    // Shift words: each line takes the word from the next occurrence
    // (end case ignored, so the last timestamp line is left as-is)
    for (int i = 1; i < (int)second_words.size(); i++) {
        int line_index = timestamp_line_indices[i];
        std::string prev_word = second_words[i - 1];

        // Rebuild the line with the second word replaced
        std::istringstream line_stream(all_lines[line_index]);
        std::string first_word, old_second_word, remainder;
        line_stream >> first_word >> old_second_word;
        std::getline(line_stream, remainder); // rest of the line (after second word)

        all_lines[line_index] = first_word + " " + prev_word + remainder;
    }

    std::ofstream output_file("output.txt");
    if (!output_file.is_open()) {
        std::cerr << "Could not open output.txt" << std::endl;
        return 1;
    }

    for (const std::string& line : all_lines) {
        output_file << line << "\n";
    }
    output_file.close();

    return 0;
}