//  rename_png_files_to_not_skip_frame_numbers.cpp


#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

int main() {
    const fs::path input_dir = "frames_png";
//    const fs::path input_dir = "frames_with_captions_png";
    const fs::path output_dir = "renumbered_frames_png";

    fs::create_directories(output_dir);

    std::vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(input_dir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".png" &&
            entry.path().filename().string().rfind("frame_", 0) == 0) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    for (std::size_t i = 0; i < files.size(); ++i) {
        std::ostringstream name;
        name << "frame_" << std::setw(5) << std::setfill('0') << (i + 1) << ".png";

        fs::copy_file(files[i], output_dir / name.str(),
                      fs::copy_options::overwrite_existing);
    }
}
