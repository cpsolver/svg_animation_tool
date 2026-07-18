//  render_svg_files_to_png.cpp
//
//  Converts frames_svg/frame_*.svg files into frames_png/*.png files.

//  Normal mode uses Inkscape to convert each SVG file into a PNG file.
//  A two-second cooldown occurs after each Inkscape run.
//
//  A faster "lowres" (low-resolution) mode uses ffmpeg (its "convert"
//  utility) to convert the SVG files to low-resolution PNG files.
//
//  If the output frames per second are less than the input frames per
//  second then only some animation SVG files are rendered.  This mode is
//  useful to check timing, without wasting time rendering every file.

//  Caption SVG files, if they are present, are overlayed on top of the
//  animation frames.

//  The caption SVG files must be named "caption_frame_nnnnn" where n indicates
//  a digit, and must be in the specified directory.
//
//  All progress/diagnostic detail is written to output_trace_render.txt.
//  The only thing written to standard output is "Done rendering".
//
//  Build:
//    g++ -std=c++17 -O2 -o render_svg_files_to_png render_svg_files_to_png.cpp
//
//  Run:
//    ./render_svg_files_to_png

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

//  Specify settings.
// "g_" prefix indicates global values
const int g_input_frame_rate = 30;
const int g_output_frame_rate = 3;
bool use_low_resolution = false;
bool have_captions = true;

namespace fs = std::filesystem;
std::vector<fs::path> animation_files;
std::vector<fs::path> caption_files;

constexpr size_t INVALID_FRAME = static_cast<size_t>(-1);
const fs::path INPUT_ANIMA_DIR = "frames_svg";
const fs::path INPUT_CAPTION_DIR = "caption_frames";
const fs::path OUTPUT_DIR = "frames_png";
const fs::path TRACE_PATH = "output_trace_render.txt";
const std::chrono::seconds COOLDOWN(2);

std::ofstream g_trace_stream;

//  Write trace info to a trace file.
void trace(const std::string& message) {
    g_trace_stream << message << '\n';
    g_trace_stream.flush();
}

//  Get the names of animation files and caption files.
//  Those names include frame numbers.
bool gather_input_files() {
    for (const auto& file_entry : fs::directory_iterator(INPUT_ANIMA_DIR)) {
        if (!file_entry.is_regular_file()) continue;
        std::string file_name = file_entry.path().filename().string();
        if ((file_name.rfind("frame_", 0) == 0) && (file_entry.path().extension() == ".svg")) {
            animation_files.push_back(file_entry.path());
        }
    }
    std::sort(animation_files.begin(), animation_files.end());
    for (const auto& file_entry : fs::directory_iterator(INPUT_CAPTION_DIR)) {
        if (!file_entry.is_regular_file()) continue;
        std::string file_name = file_entry.path().filename().string();
        if ((file_name.rfind("caption_frame_", 0) == 0) && (file_entry.path().extension() == ".svg")) {
            caption_files.push_back(file_entry.path());
        }
    }
    std::sort(caption_files.begin(), caption_files.end());
    return true;
}

//  Render one file.  In other words, convert one SVG file into a PNG file.
bool convert_svg_to_png(const fs::path& input_animation_file_path, const fs::path& output_png_file_path) {
    pid_t pid = fork();
    if (pid < 0) {
        trace("ERROR: fork() failed before running Inkscape or ffmpeg/convert on " + input_animation_file_path.string());
        return false;
    }
    if (pid == 0) {
        //  execlp searches PATH itself in the child process; no shell
        //  is ever started.
        if (!use_low_resolution) {
            //  Inkscape is used at full resolution.
            std::string export_arg = "--export-filename=" + output_png_file_path.string();
            execlp("inkscape", "inkscape", input_animation_file_path.c_str(), "--export-type=png",
                   export_arg.c_str(), static_cast<char*>(nullptr));
        } else {
            //  ffmpeg's "convert" utility is used at low resolution.
            execlp("convert", "convert", "-density", "12", "-resize", "256x256", "-strip",
                   "-colors", "16", "-depth", "8", input_animation_file_path.c_str(),
                   output_png_file_path.c_str(), static_cast<char*>(nullptr));
        }
        //  Only reached if exec itself failed (e.g. program not found).
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        trace("ERROR: Inkscape or ffmpeg/convert exited abnormally while converting " + input_animation_file_path.string());
        return false;
    }
    return fs::exists(output_png_file_path);
}

//  Extract a frame number from a filename.  For example the file "frame_10123"
//  is for frame number 10123.
int extractFrameNum(const std::string& filename_without_extension) {
    size_t index_of_last_underscore = filename_without_extension.rfind('_');
    if (index_of_last_underscore == std::string::npos) return 0;
    try { return std::stoi(filename_without_extension.substr(index_of_last_underscore + 1)); }
    catch (...) { return 0; }
};

//  Determines whether the "recent" or "next" frame number is numerically nearest
//  to the calculated idealized_input_frame_number.
size_t findNearestFrame(size_t idealized_input_frame_number,
                       size_t recent_frame_number,
                       size_t next_frame_number) {
    size_t distance_to_recent;
    if (idealized_input_frame_number >= recent_frame_number) {
        distance_to_recent = idealized_input_frame_number - recent_frame_number;
    } else {
        distance_to_recent = recent_frame_number - idealized_input_frame_number;
    }
    size_t distance_to_next;
    if (next_frame_number >= idealized_input_frame_number) {
        distance_to_next = next_frame_number - idealized_input_frame_number;
    } else {
        distance_to_next = idealized_input_frame_number - next_frame_number;
    }
    size_t nearest_frame_number;
    if (distance_to_next < distance_to_recent) {
        nearest_frame_number = next_frame_number;
    } else {
        nearest_frame_number = recent_frame_number;
    }
    return nearest_frame_number;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "lowres") {
            use_low_resolution = true;
            break;
        }
    }

    std::cout << "Input frame rate is " << g_input_frame_rate << std::endl;
    std::cout << "Output frame rate is " << g_output_frame_rate << std::endl;

    try {

        //  Prepare to get files from specified directory.
        if (fs::exists(OUTPUT_DIR)) {
            for (const auto& file_entry : fs::directory_iterator(OUTPUT_DIR)) {
                if (file_entry.is_regular_file() && file_entry.path().extension() == ".png") {
                    fs::remove(file_entry.path());
                }
            }
        }
        fs::create_directories(OUTPUT_DIR); //  no error if it already exists
        g_trace_stream.open(TRACE_PATH, std::ios::out | std::ios::trunc);

        //  Point to, and count, the input SVG files, and handle small counts.
        bool ok = gather_input_files();
        trace("Found " + std::to_string(animation_files.size()) + " animation frames in " +
              INPUT_ANIMA_DIR.string());
        trace("Found " + std::to_string(caption_files.size()) + " caption frames in " +
              INPUT_CAPTION_DIR.string());

        //  Exit if fewer than two animation frames.
        if (animation_files.size() < 2) {
            std::cout << "Less than two animation frames found, so exiting" << std::endl;
            exit(1);
        }

        //  If no caption files, flag this so captions are not handled.
        if (caption_files.empty()) {
            have_captions = false;
            std::cout << "No captions" << std::endl;
        } else {
            have_captions = true;
            std::cout << "Captions found" << std::endl;
        }

        //  Get the first two animation file frame numbers.
        //  Use them as the "recent" and "next" animation frame numbers.
        size_t recent_animation_file_index = 0;
        size_t next_animation_file_index = 1;
        std::string filename_stem = animation_files[recent_animation_file_index].stem().string();
        size_t recent_animation_frame_number = extractFrameNum(filename_stem);
        filename_stem = animation_files[next_animation_file_index].stem().string();
        size_t next_animation_frame_number = extractFrameNum(filename_stem);
        size_t rendered_animation_frame_number = -1;

        //  If there are captions, get the first two file frame numbers
        //  and use them as "recent" and "next" caption frame numbers.
        size_t recent_caption_file_index = 0;
        size_t next_caption_file_index = 1;
        size_t recent_caption_frame_number;
        size_t next_caption_frame_number;
        size_t rendered_caption_frame_number = -1;
        if (have_captions) {
            filename_stem = caption_files[recent_caption_file_index].stem().string();
            recent_caption_frame_number = extractFrameNum(filename_stem);
            filename_stem = caption_files[next_caption_file_index].stem().string();
            next_caption_frame_number = extractFrameNum(filename_stem);
        }

        //  Get the frame number of the last animation SVG file.
        size_t last_input_animation_file_index = animation_files.size() - 1;
        const fs::path& input_animation_file_path = animation_files[last_input_animation_file_index];
        std::string filename_without_extension = input_animation_file_path.stem().string();
        size_t last_animation_frame_number = extractFrameNum(filename_without_extension);
        std::cout << "Last frame number is " << last_animation_frame_number << std::endl;

        //  Calculate number of output frames.
        size_t number_of_output_frames = static_cast<size_t>(((last_animation_frame_number + 1) * g_output_frame_rate) / g_input_frame_rate);
        std::cout << "Number of output frames is " << number_of_output_frames << std::endl;

        //  Begin loop for each output frame number.
        for (size_t output_frame_number = 0; output_frame_number <= number_of_output_frames; output_frame_number++) {

            //  Calculate idealized input frame number that corresponds to output_frame_number.
            size_t idealized_input_frame_number = static_cast<size_t>((output_frame_number * g_input_frame_rate) / g_output_frame_rate);

            //  If needed, point to next animation frame file where frame number exceeds output_frame_number.
            while ((next_animation_frame_number < idealized_input_frame_number) &&
                   (next_animation_file_index + 1 < animation_files.size())) {
                recent_animation_file_index++;
                next_animation_file_index++;
                recent_animation_frame_number = next_animation_frame_number;
                const fs::path& input_animation_file_path = animation_files[next_animation_file_index];
                std::string filename_without_extension = input_animation_file_path.stem().string();
                next_animation_frame_number = extractFrameNum(filename_without_extension);
            }

            //  If needed, point to next caption frame file where frame number exceeds output_frame_number.
            if (have_captions) {
                while ((next_caption_frame_number < idealized_input_frame_number) &&
                       (next_caption_file_index + 1 < caption_files.size())) {
                    recent_caption_file_index++;
                    next_caption_file_index++;
                    recent_caption_frame_number = next_caption_frame_number;
                    const fs::path& input_caption_file_path = caption_files[next_caption_file_index];
                    std::string filename_without_extension = input_caption_file_path.stem().string();
                    next_caption_frame_number = extractFrameNum(filename_without_extension);
                }
            }

            //  If the next animation frame is needed, convert it from SVG to PNG.
            size_t nearest_animation_frame_number = findNearestFrame(idealized_input_frame_number,
                              recent_animation_frame_number,
                              next_animation_frame_number);
                if (nearest_animation_frame_number != rendered_animation_frame_number) {
                    std::ostringstream oss_animation_filename;
                    if (nearest_animation_frame_number == recent_animation_frame_number) {
                        oss_animation_filename << "frame_" << std::setw(5) << std::setfill('0') << recent_animation_frame_number << ".png";
                        rendered_animation_frame_number = recent_animation_frame_number;
                    } else {
                        oss_animation_filename << "frame_" << std::setw(5) << std::setfill('0') << next_animation_frame_number << ".png";
                        rendered_animation_frame_number = next_animation_frame_number;
                    }
                    fs::path output_path = OUTPUT_DIR / oss_animation_filename.str();
                    std::cout << "Need to render " << oss_animation_filename.str() << std::endl;

            // // bool ok = convert_svg_to_png(input_animation_file_path, output_png_file_path);
            //     std::cout << "." << std::flush;
            //     if (ok) {
            //         rendered_count++;
            //     } else {
            //         trace("WARNING: no usable output for " + input_animation_file_path.string() +
            //               "; it will not be used as a copy source");
            //         have_prev = false;
            //         next_output_frame_number = next_output_frame_limit;
            //         continue;
            //     }


                }


            //  If the next caption frame is needed, convert it from SVG to PNG.
            if (have_captions) {
                size_t nearest_caption_frame_number = findNearestFrame(idealized_input_frame_number,
                                  recent_caption_frame_number,
                                  next_caption_frame_number);
                if (nearest_caption_frame_number != rendered_caption_frame_number) {
                    std::ostringstream oss_caption_filename;
                    if (nearest_caption_frame_number == recent_caption_frame_number) {
                        oss_caption_filename << "caption_frame_" << std::setw(5) << std::setfill('0') << recent_caption_frame_number << ".png";
                        rendered_caption_frame_number = recent_caption_frame_number;
                    } else {
                        oss_caption_filename << "caption_frame_" << std::setw(5) << std::setfill('0') << next_caption_frame_number << ".png";
                        rendered_caption_frame_number = next_caption_frame_number;
                    }
                    fs::path output_path = OUTPUT_DIR / oss_caption_filename.str();
                    std::cout << "Need to render " << oss_caption_filename.str() << std::endl;
                }
            }


            //  Need to overlay animation frame with caption frame.
            //  Allow rendered animation file to be used again if caption changes.
            size_t merged_animation_frame_number = rendered_animation_frame_number;
            size_t merged_caption_frame_number = rendered_caption_frame_number;
            size_t merged_output_frame_number = output_frame_number;
            std::cout << "Merged frame " << merged_output_frame_number << " holds " << merged_animation_frame_number << " and " << merged_caption_frame_number << std::endl;


            //  CPU cooldown if Inkscape was used.
            if (!use_low_resolution) {
                std::this_thread::sleep_for(COOLDOWN);
            }

        //  Repeat loop for next output frame number.
        }

        trace("Rendering finished");
    } catch (const std::exception& e) {
        if (g_trace_stream.is_open()) {
            trace(std::string("FATAL: ") + e.what());
        }
    }

    if (g_trace_stream.is_open()) {
        g_trace_stream.close();
    }

    std::cout << std::endl;
    std::cout << "Done rendering" << std::endl;
    return 0;
}
