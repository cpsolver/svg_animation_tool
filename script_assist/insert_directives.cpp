//  insert_directives.cpp
//  --------------------
//
//  Can be modified to insert, remove, or modify
//  directives in script.
//  Comments below describe what the current
//  version of this code does.
//
//  Local file input.txt must contain a copy of the
//  script.  Modified script written to output.txt
//  file.  View output file before copying script
//  to overwrite actual script file.
//
//  Compile:
//    g++ -o insert_directives insert_directives.cpp
//  Run:
//    ./insert_directives


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

    std::vector<std::string> all_input_lines;
    std::string current_line;
    while (std::getline(input_file, current_line)) {
        all_input_lines.push_back(current_line);
    }
    input_file.close();

    // Copy all the input lines to the output lines,
    // but insert two lines after every line that
    // begins with "desired-timestamp".
    // The first inserted line is empty.
    // The second inserted line is:
    // percent-scale-animation-duration 100
    std::vector<std::string> all_output_lines;
    for (int i = 0; i < (int)all_input_lines.size(); i++) {
        all_output_lines.push_back(all_input_lines[i]);
        if (all_input_lines[i].rfind("desired-timestamp", 0) == 0) {
            all_output_lines.push_back("");
            all_output_lines.push_back("percent-scale-animation-duration 100");
        }
    }

    std::ofstream output_file("output.txt");
    if (!output_file.is_open()) {
        std::cerr << "Could not open output.txt" << std::endl;
        return 1;
    }

    for (const std::string& line : all_output_lines) {
        output_file << line << "\n";
    }
    output_file.close();

    return 0;
}