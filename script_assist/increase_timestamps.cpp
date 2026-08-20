//  increase_timestamps.cpp
//  -----------------------
//
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
//    g++ -o increase_timestamps increase_timestamps.cpp
//  Run:
//    ./increase_timestamps


#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

int main() {
    std::ifstream input_file("input.txt");
    if (!input_file.is_open()) {
        std::cerr << "Could not open input.txt" << std::endl;
        return 1;
    }

    std::ofstream output_file("output.txt");
    if (!output_file.is_open()) {
        std::cerr << "Could not open output.txt" << std::endl;
        return 1;
    }

    std::string current_line;
    std::string timestamp_word;
    std::string revised_timestamp_word;

    while (std::getline(input_file, current_line)) {
        if (current_line.rfind("desired-timestamp", 0) == 0) {

            // Get the second space-delimited word
            std::istringstream iss(current_line);
            std::string first;
            iss >> first;            // "desired-timestamp"
            iss >> timestamp_word; // e.g., "23.7" or "4:23.7" or "1:23:56.7"

            // Supported forms:
            //   "SS"          (seconds as integer)        e.g., "56"
            //   "S.T"         (seconds with tenths)      e.g., "23.7"
            //   "M:SS.T"      (minute:seconds.tenths)    e.g., "4:23.7"
            //   "H:MM:SS.T"   (hour:minute:seconds.tenths) e.g., "1:23:56.7"
            //
            // "Seconds" part may include an optional first digit of tenths: "23.7" works.
            int h = 0;
            int m = 0;

            // seconds with tenths (0.1s resolution)
            int sec_whole = 0;
            int sec_tenths = 0;

            // Split by ':'
            std::vector<std::string> parts;
            {
                std::string tmp;
                std::istringstream ss(timestamp_word);
                while (std::getline(ss, tmp, ':')) parts.push_back(tmp);
            }

            // Function, parse seconds to avoid locale issues.
            auto parseSecondsPart = [&](const std::string& sPart) {
                // Expect "SS" or "SS.T" where T is 0..9 (optional fractional digit)
                size_t dot = sPart.find('.');
                if (dot == std::string::npos) {
                    sec_whole = std::stoi(sPart);
                    sec_tenths = 0;
                } else {
                    sec_whole = std::stoi(sPart.substr(0, dot));
                    std::string frac = sPart.substr(dot + 1);
                    // take first digit if present, else 0
                    if (!frac.empty() && std::isdigit(static_cast<unsigned char>(frac[0]))) {
                        sec_tenths = frac[0] - '0';
                    } else {
                        sec_tenths = 0;
                    }
                }
            };

            if (parts.size() == 1) {
                // "SS" or "SS.T"
                parseSecondsPart(parts[0]);
            } else if (parts.size() == 2) {
                // "M:SS" or "M:SS.T"
                m = std::stoi(parts[0]);
                parseSecondsPart(parts[1]);
            } else {
                // "H:MM:SS" or "H:MM:SS.T" (use first 3 parts)
                h = std::stoi(parts[0]);
                m = std::stoi(parts[1]);
                parseSecondsPart(parts[2]);
            }

            // Convert to tenths-of-seconds, add 2 seconds, which is 20 tenths of a second
            int total_tenths = (h * 3600 + m * 60 + sec_whole) * 10 + sec_tenths;
            total_tenths += 20;

            // Convert back using tenths
            int total_seconds = total_tenths / 10;   // whole seconds
            int tenths = total_tenths % 10;           // 0..9

            // Split into hours, minutes, seconds
            h = total_seconds / 3600;
            int rem = total_seconds % 3600;
            m = rem / 60;
            sec_whole = rem % 60;

// TODO: modified, not yet tested!!

            // Combine hours, colon, minutes, colon, seconds, period, tenths
            std::ostringstream out_stream;
            out_stream << h << ':';
            out_stream << std::setw(2) << std::setfill('0') << m << ':';
            out_stream << std::setw(2) << std::setfill('0') << sec_whole;
            if (tenths > 0) {
                out_stream << "." << tenths;
            }
            revised_timestamp_word = out_stream.str();

            // Remove leading zeros and colons
            revised_timestamp_word.erase(0, revised_timestamp_word.find_first_not_of("0:"));
            if (revised_timestamp_word.empty() || revised_timestamp_word.front() == '.') {
                revised_timestamp_word.insert(0, "0");
            }

            // Write revised line.
            output_file << "desired-timestamp " << revised_timestamp_word << "\n";
        } else {
            output_file << current_line << "\n";
        }
    }

    output_file.close();
    return 0;
}