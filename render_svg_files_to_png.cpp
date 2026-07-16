//  render_svg_files_to_png.cpp
//
//  Converts frames_svg/frame_*.svg files into frames_png/*.png files
//  using Inkscape.  Includes two-second cooldown after each Inkscape run.
//  If new frame is same as previous, file is just copied.
//
//  A faster "lowres" (low-resolution) mode uses ffmpeg (its "convert"
//  utility) to convert the SVG files to low-resolution PNG files.
//  It renders only some of the SVG files for testing the timing at
//  fewer frames per second.
//
//  The full resolution mode checks for additional SVG files named
//  "caption_frame_nnnnn" (where n indicates a digit) and overlays
//  these captions on all animation frames.
//
//  All progress/diagnostic detail is written to output_trace_render.txt.
//  The only thing written to standard output is "Done rendering".
//
//  Build:
//    g++ -std=c++17 -O2 -o render_svg_files_to_png render_svg_files_to_png.cpp
//  Run:
//  ./render_svg_files_to_png

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <climits>

namespace fs = std::filesystem;

// "g_" prefix indicates global values
const int g_lowResInputFrameRate = 30;
const int g_lowResOutputFrameRate = 3;
bool use_low_resolution = false;

const fs::path INPUT_DIR = "frames_svg";
const fs::path OUTPUT_DIR = "frames_png";
const fs::path TRACE_PATH = "output_trace_render.txt";
const std::chrono::seconds COOLDOWN(2);

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

// TODO move vector fs to global declaration, change function to no return value
std::vector<fs::path> gather_input_files() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(INPUT_DIR)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("frame_", 0) == 0 && entry.path().extension() == ".svg") {
            files.push_back(entry.path());
        }
        if ((!use_low_resolution) && (name.rfind("caption_frame_", 0) == 0) && (entry.path().extension() == ".svg")) {

            // TODO: files.push_back onto separate caption-specific file list

        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool convert_svg_to_png(const fs::path& input, const fs::path& output) {
    pid_t pid = fork();
    if (pid < 0) {
        trace("ERROR: fork() failed before running Inkscape or ffmpeg/convert on " + input.string());
        return false;
    }
    if (pid == 0) {
        // execlp searches PATH itself in the child process; no shell
        // is ever started.
        if (!use_low_resolution) {
            // Inkscape is used at full resolution.
            std::string export_arg = "--export-filename=" + output.string();
            execlp("inkscape", "inkscape", input.c_str(), "--export-type=png",
                   export_arg.c_str(), static_cast<char*>(nullptr));
        } else {
            // ffmpeg's "convert" utility is used at low resolution.
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
        trace("ERROR: Inkscape or ffmpeg/convert exited abnormally while converting " + input.string());
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
        if (fs::exists(OUTPUT_DIR)) {
            for (const auto& entry : fs::directory_iterator(OUTPUT_DIR)) {
                if (entry.is_regular_file() && entry.path().extension() == ".png") {
                    fs::remove(entry.path());
                }
            }
        }
        fs::create_directories(OUTPUT_DIR); // no error if it already exists
        g_trace.open(TRACE_PATH, std::ios::out | std::ios::trunc);

        // TODO: change next line when change function gather_input_files
        std::vector<fs::path> inputs = gather_input_files();

        trace("Found " + std::to_string(inputs.size()) + " frame(s) in " +
              INPUT_DIR.string());

        uint64_t prev_checksum = 0;
        fs::path prev_output;
        bool have_prev = false;
        std::size_t rendered_count = 0;
        std::size_t copied_count = 0;
        int latestInputFrameNumber = 0;
        int nextOutputFrameNumber = 0;

        // Helper lambda: extract frame number from a stem like "frame_10123"
        auto extractFrameNum = [](const std::string& stem) -> int {
            size_t u = stem.rfind('_');
            if (u == std::string::npos) return 0;
            try { return std::stoi(stem.substr(u + 1)); }
            catch (...) { return 0; }
        };

        // Begin loop for each SVG file (index-based to allow look-ahead).
        for (size_t idx = 0; idx < inputs.size(); ++idx) {
            const fs::path& input = inputs[idx];
            std::string content = read_file(input);

            // Get the SVG frame number from the filename.
            std::string stem = input.stem().string();
            latestInputFrameNumber = extractFrameNum(stem);

            // Calculate checksum of this SVG animation file.
            uint64_t checksum = fnv1a64(content);

            if (!use_low_resolution) {
                // Normal (full resolution) mode

                // TODO: if need to render new caption image, convert it
                // from SVG to PNG using Inkscape, save it in temporary
                // "caption_current.png" file.
                // Also set have_prev as false.

                fs::path output = OUTPUT_DIR / (stem + ".png");

                if (have_prev && checksum == prev_checksum && fs::exists(prev_output)) {
                    std::error_code ec;
                    fs::copy_file(prev_output, output,
                                  fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        trace("Duplicate copy " + output.string());
                        copied_count++;
                        prev_output   = output;
                        prev_checksum = checksum;
                        have_prev     = true;
                        continue;
                    }
                    trace("ERROR: copy failed (" + ec.message() +
                          "); falling back to rendering for " + input.string());
                }

                // TODO: may need to merge/overlay caption on top of new animation image.
                // If so, change following code to write animation frame to temporary file
                // and allow that temporary animation file to be used again if the caption
                // changes.

                std::cout << "." << std::flush;
                trace("Rendering " + input.string());
                bool ok = convert_svg_to_png(input, output);
                if (ok) {
                    rendered_count++;
                    prev_output   = output;
                    prev_checksum = checksum;
                    have_prev     = true;

                    // TODO: changes here will be needed

                } else {
                    trace("WARNING: no usable output for " + input.string() +
                          "; it will not be used as a copy source");
                    have_prev = false;
                }
                std::this_thread::sleep_for(COOLDOWN);

            } else {
                // Low resolution mode
                // Caption merging not supported

                // TODO: move some of this code to earlier because it now will be
                // used by normal resolution mode.

                // Compute the output frame slot this input file starts at.
                int thisOutputFrame = latestInputFrameNumber
                                      * g_lowResOutputFrameRate
                                      / g_lowResInputFrameRate;

                // Look ahead to find the next input file's frame number, so we
                // know how many output slots this input file "owns" (freeze fill).
                int nextInputFrameNumber = INT_MAX;
                if (idx + 1 < inputs.size()) {
                    std::string nextStem = inputs[idx + 1].stem().string();
                    nextInputFrameNumber = extractFrameNum(nextStem);
                }
                int nextOutputFrameLimit = (nextInputFrameNumber == INT_MAX)
                    ? thisOutputFrame + 1  // last file: owns exactly one slot
                    : nextInputFrameNumber * g_lowResOutputFrameRate
                                           / g_lowResInputFrameRate;

                // Clamp to the watermark so we never re-write already-written slots.
                int outStart = std::max(thisOutputFrame, nextOutputFrameNumber);

                // If this input file owns no new output slots, skip it entirely.
                if (outStart >= nextOutputFrameLimit) continue;

                // Determine whether this SVG is a duplicate of the previous one.
                bool isDuplicate = have_prev
                                   && checksum == prev_checksum
                                   && fs::exists(prev_output);



                // Render the SVG to a temporary PNG once (only if not a duplicate).
                // We render to the first output slot's filename, then copy for the rest.
                std::ostringstream oss_filename;
                oss_filename << "frame_" << std::setw(5) << std::setfill('0') << outStart << ".png";
                fs::path firstOutput = OUTPUT_DIR / oss_filename.str();

                if (isDuplicate) {
                    // Copy previous PNG to the first output slot without re-rendering.
                    std::error_code ec;
                    fs::copy_file(prev_output, firstOutput,
                                  fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        trace("Duplicate copy " + firstOutput.string());
                        copied_count++;
                    } else {
                        trace("ERROR: copy failed (" + ec.message() +
                              "); falling back to rendering for " + input.string());
                        isDuplicate = false;  // fall through to render below
                    }
                }

                if (!isDuplicate) {
                    std::cout << "." << std::flush;
                    trace("Rendering " + input.string());
                    bool ok = convert_svg_to_png(input, firstOutput);
                    if (ok) {
                        rendered_count++;
                    } else {
                        trace("WARNING: no usable output for " + input.string() +
                              "; it will not be used as a copy source");
                        have_prev = false;
                        nextOutputFrameNumber = nextOutputFrameLimit;
                        continue;
                    }
                }

                // Update tracking state after the first output slot is written.
                prev_output   = firstOutput;
                prev_checksum = checksum;
                have_prev     = true;

                // Copy the rendered PNG to all remaining output slots this input
                // file owns (freeze fill: same image, multiple output frame numbers).
                for (int outF = outStart + 1; outF < nextOutputFrameLimit; ++outF) {
                    std::ostringstream oss_filename;
                    oss_filename << "frame_" << std::setw(5) << std::setfill('0') << outStart << ".png";
                    fs::path output = OUTPUT_DIR / oss_filename.str();
                    std::error_code ec;
                    fs::copy_file(firstOutput, output,
                                  fs::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        trace("Freeze copy " + output.string());
                        copied_count++;
                    } else {
                        trace("ERROR: freeze copy failed (" + ec.message() +
                              ") for " + output.string());
                    }
                }

                // Advance the watermark past all slots this input file owned.
                nextOutputFrameNumber = nextOutputFrameLimit;

                // No CPU cooldown in lowres mode because Inkscape not used.
            }
        // Repeat the loop for the next SVG file.
        }

        trace("Finished: " + std::to_string(rendered_count) +
              " rendered, " + std::to_string(copied_count) +
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