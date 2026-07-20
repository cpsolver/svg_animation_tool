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
#include <cstdlib>
#include <cerrno>

//  Specify settings.
const int global_input_frame_rate = 30;
const int global_output_frame_rate = 3;
bool global_use_low_resolution = false;
bool global_have_captions = true;

namespace fs = std::filesystem;
std::vector<fs::path> animation_files;
std::vector<fs::path> caption_files;

std::ofstream trace{fs::path{"output_trace_render.txt"}, std::ios::out | std::ios::trunc};

constexpr size_t INVALID_FRAME = static_cast<size_t>(-1);
const fs::path INPUT_ANIMATION_DIR = "frames_svg";
const fs::path INPUT_CAPTION_DIR = "caption_frames";
const fs::path OUTPUT_DIR = "frames_png";

const std::string output_rendered_caption_filename = "output_rendered_caption.png";
fs::path input_caption_file_path;

const std::chrono::seconds COOLDOWN(2);
std::ofstream global_trace_stream;

//  Get the names of animation files and caption files.
//  The filenames include frame numbers.
void get_and_sort_animation_and_caption_filenames() {
    for (const auto& file_entry : fs::directory_iterator(INPUT_ANIMATION_DIR)) {
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
    return;
}

//  Render one file by converting it from SVG to PNG.
bool convert_svg_to_png(const fs::path& input_animation_file_path, const fs::path& output_png_file_path) {
    pid_t pid = fork();
    if (pid < 0) {
        trace << "ERROR: fork() failed before running Inkscape or ffmpeg/convert on "
            << input_animation_file_path.string();
        return false;
    }
    if (pid == 0) {
        //  execlp searches PATH itself in the child process; no shell
        //  is ever started.
        if (!global_use_low_resolution) {
            //  At full resolution, use Inkscape.
            std::string export_arg = "--export-filename=" + output_png_file_path.string();
            execlp("inkscape", "inkscape", input_animation_file_path.c_str(), "--export-type=png",
                   export_arg.c_str(), static_cast<char*>(nullptr));
            //  Write period to screen as progress indicator.
            std::cout << "." << std::flush;
        } else {
            //  At low resolution, use ffmpeg's "convert" utility.
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
        trace << "ERROR: Inkscape or ffmpeg/convert exited abnormally while converting "
            << input_animation_file_path.string();
        return false;
    }
    return fs::exists(output_png_file_path);
}

//  Extract a frame number from a filename.  For example the file "frame_10123"
//  is for frame number 10123.
int extract_frame_number_from_filename(const std::string& filename_without_extension) {
    size_t index_of_last_underscore = filename_without_extension.rfind('_');
    if (index_of_last_underscore == std::string::npos) return 0;
    try { return std::stoi(filename_without_extension.substr(index_of_last_underscore + 1)); }
    catch (...) { return 0; }
};

//  Determines whether the "recent" or "next" frame number is numerically nearest
//  to the calculated idealized_input_frame_number.
size_t get_nearest_frame(size_t idealized_input_frame_number,
                       size_t recent_frame_number,
                       size_t next_frame_number) {
    size_t distance_to_recent;
    if (idealized_input_frame_number >= recent_frame_number) {
        distance_to_recent = idealized_input_frame_number - recent_frame_number;
    } else {
        distance_to_recent = recent_frame_number - idealized_input_frame_number;
    }
    size_t distance_to_next;
    if (idealized_input_frame_number < next_frame_number) {
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
            global_use_low_resolution = true;
            break;
        }
    }

    trace << "Input frame rate is " << global_input_frame_rate << std::endl;
    trace << "Output frame rate is " << global_output_frame_rate << std::endl;

    //  Ensure output directory exists.
    if (fs::exists(OUTPUT_DIR)) {
        for (const auto& file_entry : fs::directory_iterator(OUTPUT_DIR)) {
            if (file_entry.is_regular_file() && file_entry.path().extension() == ".png") {
                fs::remove(file_entry.path());
            }
        }
    }
    fs::create_directories(OUTPUT_DIR); //  no error if it already exists

    //  Point to, and count, the input SVG files.
    //  They are in two directories.
    get_and_sort_animation_and_caption_filenames();
    trace << "Found " << std::to_string(animation_files.size())
        << " animation frames in "
        <<INPUT_ANIMATION_DIR.string() << std::endl;
    trace << "Found " << std::to_string(caption_files.size())
        << " caption frames in "
        << INPUT_CAPTION_DIR.string() << std::endl;

    //  Exit if only a few animation frames.
    if (animation_files.size() < 3) {
        trace << "Less than two animation frames found, so exiting" << std::endl;
        exit(1);
    }

    //  If only a few caption files, do not handle captions.
    if (caption_files.empty()) {
        global_have_captions = false;
        trace << "No captions" << std::endl;
    } else {
        global_have_captions = true;
        trace << "Captions found" << std::endl;
    }

    //  Get the first two animation file frame numbers.
    //  Use them as the "recent" and "next" animation frame numbers.
    //  Also point to the filenames for these two files.
    size_t recent_animation_file_index = 0;
    size_t next_animation_file_index = 1;
    std::string filename_stem = animation_files[recent_animation_file_index].stem().string();
    size_t recent_animation_frame_number = extract_frame_number_from_filename(filename_stem);
    filename_stem = animation_files[next_animation_file_index].stem().string();
    size_t next_animation_frame_number = extract_frame_number_from_filename(filename_stem);
    size_t rendered_animation_frame_number = -1;

    //  If there are captions, get the first two file frame numbers
    //  and use them as "recent" and "next" caption frame numbers.
    //  Also point to the filenames for these two files.
    size_t recent_caption_file_index = 0;
    size_t next_caption_file_index = 1;
    size_t recent_caption_frame_number;
    size_t next_caption_frame_number;
    size_t rendered_caption_frame_number = -1;
    if (global_have_captions) {
        filename_stem = caption_files[recent_caption_file_index].stem().string();
        recent_caption_frame_number = extract_frame_number_from_filename(filename_stem);
        filename_stem = caption_files[next_caption_file_index].stem().string();
        next_caption_frame_number = extract_frame_number_from_filename(filename_stem);
    }

    //  Store a file path that is set in one loop cycle and checked in later loop
    //  cycles.  Initialize it so it is recognized as invalid at the first frame.
    std::filesystem::path previous_output_target_file_path = "";

    //  Build the path to the file that holds the current rendered caption PNG file.
    //  It is overwritten at each new caption.
    std::ostringstream oss_output_caption_filename;
    oss_output_caption_filename << "output_rendered_caption.png";
    std::filesystem::path output_rendered_caption_file_path;
    output_rendered_caption_file_path = oss_output_caption_filename.str();

    //  Get the frame number of the last animation SVG file.
    size_t last_input_animation_file_index = animation_files.size() - 1;
    const fs::path& input_animation_file_path = animation_files[last_input_animation_file_index];
    std::string filename_without_extension = input_animation_file_path.stem().string();
    size_t last_animation_frame_number = extract_frame_number_from_filename(filename_without_extension);
    trace << "Last frame number is " << last_animation_frame_number << std::endl;

    //  Calculate the number of output frames.
    //  For testing scenarios the output frame rate will be much lower
    //  than the input frame rate.
    size_t number_of_output_frames = static_cast<size_t>(((last_animation_frame_number + 1) * global_output_frame_rate) / global_input_frame_rate);
    trace << "Number of output frames is " << number_of_output_frames << std::endl;

    //  Initialize frame numbers that track which animation and caption frame numbers
    //  were rendered during the previous time through the loop.
    static size_t previous_rendered_animation_frame_number = static_cast<size_t>(-1);
    static size_t previous_rendered_caption_frame_number = static_cast<size_t>(-1);

    //  Begin loop for each output frame number.
    for (size_t output_frame_number = 0; output_frame_number <= number_of_output_frames; output_frame_number++) {

        //  Calculate idealized input frame number that corresponds to output_frame_number.
        size_t idealized_input_frame_number = static_cast<size_t>((output_frame_number * global_input_frame_rate) / global_output_frame_rate);

        //  If now needed, point to next animation filename where frame number
        //  exceeds output_frame_number.
        while ((next_animation_frame_number < idealized_input_frame_number) &&
               (next_animation_file_index + 1 < animation_files.size())) {
            recent_animation_file_index++;
            next_animation_file_index++;
            recent_animation_frame_number = next_animation_frame_number;
            const fs::path& input_animation_file_path = animation_files[next_animation_file_index];
            std::string filename_without_extension = input_animation_file_path.stem().string();
            next_animation_frame_number = extract_frame_number_from_filename(filename_without_extension);
        }

        //  If now needed, point to next caption filename where frame number
        //  exceeds output_frame_number.
        if (global_have_captions) {
            while ((next_caption_frame_number < idealized_input_frame_number) &&
                   (next_caption_file_index + 1 < caption_files.size())) {
                recent_caption_file_index++;
                next_caption_file_index++;
                recent_caption_frame_number = next_caption_frame_number;
                const fs::path& input_caption_file_path = caption_files[next_caption_file_index];
                std::string filename_without_extension = input_caption_file_path.stem().string();
                next_caption_frame_number = extract_frame_number_from_filename(filename_without_extension);
            }
        }

        //  If the caption needs to change, or this is the first caption, convert the
        //  caption SVG file into a PNG file named "output_rendered_caption.png".
        //  This rendered PNG file is available to merge with each new animation frame.
        if (global_have_captions) {
            size_t nearest_caption_frame_number = get_nearest_frame(idealized_input_frame_number,
                              recent_caption_frame_number,
                              next_caption_frame_number);
            if (nearest_caption_frame_number != rendered_caption_frame_number) {
                rendered_caption_frame_number = nearest_caption_frame_number;
                // Build full input path.
                std::ostringstream oss_input_filename;
                oss_input_filename << "caption_frame_" << std::setw(5)
                    << std::setfill('0') << rendered_caption_frame_number << ".svg";
                std::filesystem::path input_caption_file_path =
                    std::filesystem::path(INPUT_CAPTION_DIR) / oss_input_filename.str();
                std::filesystem::remove(output_rendered_caption_file_path);
                trace << "Deleted " << output_rendered_caption_file_path.string() << std::endl;
                // Render new caption, ignore any error.
                bool ok = convert_svg_to_png(input_caption_file_path, output_rendered_caption_file_path);
                trace << "Converted " << input_caption_file_path.string() << " to "
                    << output_rendered_caption_file_path.string() << std::endl;
            }
        }

        //  Specify the final target path, in the output directory.
        //  The filename includes the output frame number.
        std::filesystem::path output_target_file_path;
        std::ostringstream oss_target_filename;
        oss_target_filename << "frame_" << std::setw(5)
            << std::setfill('0') << output_frame_number << ".png";
        output_target_file_path = OUTPUT_DIR / oss_target_filename.str();
        //  Save a copy of this path in case the next frame is the same.
        //  But do not save if this is the first frame.
        if (previous_output_target_file_path == "") {
            previous_output_target_file_path = output_target_file_path;
        }

        //  Specify the animation output path for this output frame.
        //  When captions are present, every animation frame renders
        //  into the same reused temporary filename,
        //  "output_rendered_animation.png".
        //  When there are no captions, the rendered PNG writes to
        //  the target output PNG file.
        std::filesystem::path output_rendered_animation_file_path;
        std::ostringstream oss_output_filename;
        if (global_have_captions) {
            output_rendered_animation_file_path = "output_rendered_animation.png";
        } else {
            output_rendered_animation_file_path = output_target_file_path;
        }

        //  If the animation has changed, convert the new frame from SVG to PNG.
        size_t nearest_animation_frame_number = get_nearest_frame(idealized_input_frame_number,
            recent_animation_frame_number, next_animation_frame_number);
        if ((nearest_animation_frame_number != rendered_animation_frame_number) || (rendered_animation_frame_number < 0)) {
            // Build full input path.
            std::ostringstream oss_input_filename;
            if ((nearest_animation_frame_number == recent_animation_frame_number) || (rendered_animation_frame_number < 0)) {
                oss_input_filename << "frame_" << std::setw(5) << std::setfill('0') << recent_animation_frame_number << ".svg";
                rendered_animation_frame_number = recent_animation_frame_number;
            } else {
                oss_input_filename << "frame_" << std::setw(5) << std::setfill('0') << next_animation_frame_number << ".svg";
                rendered_animation_frame_number = next_animation_frame_number;
            }
            std::filesystem::path input_animation_file_path =
                std::filesystem::path(INPUT_ANIMATION_DIR) / (oss_input_filename.str());
            std::filesystem::remove(output_rendered_animation_file_path);
            trace << "Deleted " << output_rendered_animation_file_path.string() << std::endl;
            // Render new animation, ignore any error.
            bool ok = convert_svg_to_png(input_animation_file_path, output_rendered_animation_file_path);
            trace << "Converted " << input_animation_file_path.string() << " to "
                << output_rendered_animation_file_path.string() << std::endl;
        }

        //  If captions are handled, and the previous output frame used the same animation
        //  frame and same caption frame, copy the previous output frame, then repeat the loop.
        if (false) {
        // if ((global_have_captions)
        //         && (rendered_animation_frame_number == previous_rendered_animation_frame_number)
        //         && (rendered_caption_frame_number == previous_rendered_caption_frame_number)
        //         && (output_frame_number > 0)) {
            std::filesystem::copy_file(previous_output_target_file_path, output_target_file_path,
                std::filesystem::copy_options::overwrite_existing);
            trace << "Copied " << previous_output_target_file_path.string()
                << " to " << output_target_file_path.string() << std::endl;
            //  Keep track of the current target file path in case it should be copied
            //  because of no change in animation or caption.
            previous_rendered_animation_frame_number = rendered_animation_frame_number;
            previous_rendered_caption_frame_number = rendered_caption_frame_number;
            previous_output_target_file_path = output_target_file_path;
            //  No need for cooldown.
            continue;
        }

        //  If copying the previous output frame would not be correct, create the new target
        //  PNG file by overlaying the current caption on top of the current animation frame.
        //  Use ImageMagick.
        //  bash version: magick -i animation_frame.png -i caption_frame.png -filter_complex "overlay=0:0" merged_frame.png
        if (global_have_captions) {
            std::string string_animation_path = output_rendered_animation_file_path.string();
            std::string string_caption_path = output_rendered_caption_file_path.string();
            std::string string_target_path = output_target_file_path.string();
            pid_t pid = fork();
            if (pid < 0) {
                trace << "ERROR: fork() failed before running magick" << std::endl;
            } else if (pid == 0) {
                //  Child process: exec magick here. If exec succeeds, this never returns.
                //  Directly specifies /usr/bin/ location without using "path" variable.
                execlp("/usr/bin/composite", "composite", "-compose", "over",
                        string_caption_path.c_str(), string_animation_path.c_str(),
                        string_target_path.c_str(), static_cast<char*>(nullptr));
                //  Only reached if exec itself failed.
                trace << "ERROR: execlp(magick) failed: " << strerror(errno) << std::endl;
                _exit(127);
            } else {
                //  Parent process: wait for the child and check its exit status.
                int status = 0;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    trace << "Merged " << string_animation_path << " and " << string_caption_path
                          << " into " << string_target_path << std::endl;
                    previous_rendered_animation_frame_number = rendered_animation_frame_number;
                    previous_rendered_caption_frame_number = rendered_caption_frame_number;
                    previous_output_target_file_path = output_target_file_path;
                } else {
                    trace << "ERROR: magick exited abnormally (status " << status
                          << ") merging " << string_animation_path << " and " << string_caption_path << std::endl;
                }
            }
        }

        //  CPU cooldown if Inkscape was used.
        if (!global_use_low_resolution) {
            std::this_thread::sleep_for(COOLDOWN);
        }

        //  Save the target file path so it can be copied during the next frame
        //  if the animation and caption frames remain the same.
        previous_output_target_file_path = output_target_file_path;

    //  Repeat loop for next output frame number.
    }

    trace << "Done" << std::endl;
    std::cout << "Done rendering" << std::endl;
    return 0;
}
