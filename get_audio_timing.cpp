/**
 *  get_audio_timing.cpp
 *  --------------------
 *
 * Reads output_audio_narration_file_list.txt (produced by svg_animation_tool),
 * uses ffprobe to get the duration of each audio file, and writes a timing
 * summary showing each file's individual duration and cumulative end time,
 * both rounded to the nearest whole second.
 *
 * Output goes to get_audio_timing.txt.
 *
 * Build:
 *   g++ -std=c++17 -O2 -o get_audio_timing get_audio_timing.cpp
 *
 * Run:
 *   ./get_audio_timing
 *
 * Requires ffprobe (part of the ffmpeg package) to be installed.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <iomanip>
#include <cstdio>

//  Globals

std::string g_inputFile  = "output_audio_narration_file_list.txt";
std::string g_outputFile = "output_audio_timing.txt";

// Parsed audio filenames (from "file '...'" lines in the list file)
std::vector<std::string> g_audioFiles;

// Per-file durations in seconds (as returned by ffprobe)
std::vector<double> g_durations;

// Running cumulative end time in seconds
double g_cumulativeSecs = 0.0;

//  main

int main() {

    //  Read the file list
    std::ifstream listFile(g_inputFile);
    if (!listFile) {
        std::cerr << "Error: cannot open " << g_inputFile << "\n"
                  << "Run svg_animation_tool first to generate this file.\n";
        return 1;
    }

    std::string line;
    while (std::getline(listFile, line)) {
        // Each line is either empty or: file 'filename'
        // Strip leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Must start with "file '"
        if (line.size() < 7 || line.substr(0, 6) != "file '") continue;

        // Extract filename between the quotes — allow slashes in path
        size_t nameStart = 6;
        size_t nameEnd   = line.rfind('\'');
        if (nameEnd == std::string::npos || nameEnd <= nameStart) continue;

        std::string fname = line.substr(nameStart, nameEnd - nameStart);
        if (!fname.empty())
            g_audioFiles.push_back(fname);
    }

    if (g_audioFiles.empty()) {
        std::cerr << "Error: no audio files found in " << g_inputFile << "\n";
        return 1;
    }

    //  Get durations via ffprobe
    for (const std::string& fname : g_audioFiles) {
        // Build the ffprobe command — quote the filename to allow spaces/slashes
        std::string cmd = "ffprobe -v error -show_entries format=duration"
                          " -of default=noprint_wrappers=1:nokey=1 \""
                        + fname + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        double dur = 0.0;
        if (pipe) {
            char buf[64] = {};
            if (fgets(buf, sizeof(buf), pipe))
                dur = std::stod(std::string(buf));
            pclose(pipe);
        }
        if (dur <= 0.0) {
            std::cerr << "WARNING: could not get duration for '" << fname
                      << "' — treating as 0 seconds.\n";
        }
        g_durations.push_back(dur);
    }

    //  Format and write output
    std::ofstream outFile(g_outputFile);
    if (!outFile) {
        std::cerr << "Error: cannot open output file: " << g_outputFile << "\n";
        return 1;
    }

    // Determine column width for filenames
    size_t maxLen = 0;
    for (const auto& f : g_audioFiles) {
        std::string base = f;
        size_t sl = base.rfind('/');
        if (sl != std::string::npos) base = base.substr(sl + 1);
        if (base.size() > maxLen) maxLen = base.size();
    }
    int nameWidth = (int)maxLen + 2;

    // Heading
    std::string heading = std::string("File")
                        + std::string(nameWidth - 4, ' ')
                        + "Duration    Cumulative end time";
    outFile    << heading << "\n";

    // One line per file
    for (size_t k = 0; k < g_audioFiles.size(); ++k) {
        g_cumulativeSecs += g_durations[k];

        int durRounded  = (int)std::round(g_durations[k]);
        int cumRounded  = (int)std::round(g_cumulativeSecs);

        // Strip directory path for display
        std::string displayName = g_audioFiles[k];
        size_t slash = displayName.rfind('/');
        if (slash != std::string::npos)
            displayName = displayName.substr(slash + 1);

        // Format seconds as MM:SS when >= 60, plain integer otherwise
        auto fmtSecs = [](int secs) -> std::string {
            if (secs >= 60) {
                int m = secs / 60;
                int s = secs % 60;
                std::ostringstream o;
                o << m << ":" << std::setw(2) << std::setfill('0') << s;
                return o.str();
            }
            return std::to_string(secs);
        };

        std::ostringstream row;
        row << std::left << std::setw(nameWidth) << displayName
            << std::right << std::setw(8) << fmtSecs(durRounded)
            << "    "
            << std::setw(8) << fmtSecs(cumRounded);

        outFile    << row.str() << "\n";
    }

    std::cout  << "Audio timing written to: " << g_outputFile << "\n";
 
    return 0;
}
