//  render_png_files_using_inkscape.cpp
//
//  Converts frames_svg/frame_*.svg files into frames_png/*.png files
//  using Inkscape.  Includes two-second cooldown after each Inkscape run.
//  If new frame is same as previous, file is just copied.
//
//  All progress/diagnostic detail is written to output_trace_render.txt.
//  The only thing written to standard output is "Done rendering".
//
//  Build:
//    g++ -std=c++17 -O2 -o render_png_files_using_inkscape render_png_files_using_inkscape.cpp
//  Run:
//  ./render_png_files_using_inkscape

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

const fs::path INPUT_DIR = "frames_svg";
const fs::path OUTPUT_DIR = "frames_png";
const fs::path TRACE_PATH = "output_trace_render.txt";
const std::chrono::seconds COOLDOWN(2);

bool use_low_resolution = false;

std::ofstream g_trace;

void trace(const std::string& message) {
    g_trace << message << '\n';
    g_trace.flush();
}

// 64-bit FNV-1a checksum.
uint64_t fnv1a64(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>());
}

std::vector<fs::path> gather_input_files() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(INPUT_DIR)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("frame_", 0) == 0 && entry.path().extension() == ".svg") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool convert_svg_to_png(const fs::path& input, const fs::path& output) {
    pid_t pid = fork();
    if (pid < 0) {
        trace("ERROR: fork() failed before running Inkscape or ImageMagick on " + input.string());
        return false;
    }
    if (pid == 0) {
        // execlp searches PATH itself in the child process; no shell
        // is ever started.
        if (!use_low_resolution) {
            std::string export_arg = "--export-filename=" + output.string();
            execlp("inkscape", "inkscape", input.c_str(), "--export-type=png",
                   export_arg.c_str(), static_cast<char*>(nullptr));
        } else {
            execlp("convert", "convert", "-density", "12", "-resize", "256x256", "-strip",
                   "-colors", "16", "-depth", "8", input.c_str(),
                   output.c_str(), static_cast<char*>(nullptr));
        }
        // Only reached if exec itself failed (e.g. program not found).
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        trace("ERROR: Inkscape or ImageMagick exited abnormally while converting " + input.string());
        return false;
    }
    return fs::exists(output);
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "lowres") {
            use_low_resolution = true;
            break;
        }
    }
    try {
        fs::create_directories(OUTPUT_DIR); // no error if it already exists

        g_trace.open(TRACE_PATH, std::ios::out | std::ios::trunc);

        std::vector<fs::path> inputs = gather_input_files();
        trace("Found " + std::to_string(inputs.size()) + " frame(s) in " +
              INPUT_DIR.string());

        uint64_t prev_checksum = 0;
        fs::path prev_output;
        bool have_prev = false;
        std::size_t rendered_count = 0;
        std::size_t copied_count = 0;

        for (const fs::path& input : inputs) {
            std::string content = read_file(input);
            uint64_t checksum = fnv1a64(content);
            fs::path output = OUTPUT_DIR / (input.stem().string() + ".png");

//            trace("Frame " + input.filename().string() + " checksum=" + std::to_string(checksum));

            if (have_prev && checksum == prev_checksum && fs::exists(prev_output)) {
                std::error_code ec;
                fs::copy_file(prev_output, output,
                              fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    trace("Duplicate copy " + output.string() );
                    copied_count++;
                    prev_output = output;
                    prev_checksum = checksum;
                    have_prev = true;
                    continue; // no sleep for copies
                }
                trace("ERROR: copy failed (" + ec.message() +
                      "); falling back to inkscape for " + input.string());
            }

            std::cout << "." << std::flush;
            trace("Rendering " + input.string() + " with inkscape");
//            trace("Rendering " + input.string() + " -> " + output.string() + " with inkscape");
            bool ok = convert_svg_to_png(input, output);
            if (ok) {
                rendered_count++;
                prev_output = output;
                prev_checksum = checksum;
                have_prev = true;
            } else {
                trace("WARNING: no usable output for " + input.string() +
                      "; it will not be used as a copy source");
                have_prev = false;
            }

            std::this_thread::sleep_for(COOLDOWN);
        }

        trace("Finished: " + std::to_string(rendered_count) +
              " rendered via inkscape, " + std::to_string(copied_count) +
              " copied from a previous frame");
    } catch (const std::exception& e) {
        if (g_trace.is_open()) {
            trace(std::string("FATAL: ") + e.what());
        }
    }

    if (g_trace.is_open()) {
        g_trace.close();
    }

    std::cout << std::endl;
    std::cout << "Done rendering" << std::endl;
    return 0;
}