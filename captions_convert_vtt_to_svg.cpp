//  captions_convert_vtt_to_svg.cpp
//  -------------------------------
//
//  Reads VTT file and caption_template.svg file,
//  generates SVG files with captions.
//  SVG filename includes frame number where the
//  caption starts.
//
//  IMPORTANT:  Before running this program
//  delete all SVG files in output folder!
//  If deletion is not done, some SVG files
//  from previous runs can remain, which can
//  cause unexpected image sequences that are
//  difficult to debug.  Deletion is needed
//  because there are (or can be) skips in
//  the frame numbers, and old SVG files
//  from previous runs can remain within the
//  new SVG files.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;

static const int framesPerSecond = 30;

static const string inputVttFilename = "output_captions_and_timing.vtt";
static const string inputSvgTemplateFilename = "caption_template.svg";
static const string outFilePrefixSvg = "caption_frame_";
static const string caption_frame_zero_template = "caption_frame_zero_template.svg";
static const string templateZeroFileDir = "caption_frames_svg/";
static const string filename_caption_frame_zero = "caption_frame_00000.svg";

const fs::path templateFileDir = "caption_frames_svg/";


//---------------------------------
//  Parse timestamp.

static bool parseVttCueTimeRange(const string& line, double& startSeconds) {
    //  Expected format (typical):
    //  00:00:03.000 --> 00:00:05.000
    //  Parse the start time.
    auto arrowPos = line.find("-->");
    if (arrowPos == string::npos) return false;

    string startPart = line.substr(0, arrowPos);
    //  Trim.
    auto ltrim = [](string& s) {
        size_t i = 0;
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
        s.erase(0, i);
    };
    auto rtrim = [](string& s) {
        size_t i = s.size();
        while (i > 0 && isspace(static_cast<unsigned char>(s[i - 1]))) i--;
        s.erase(i);
    };

    ltrim(startPart);
    rtrim(startPart);

    //  startPart should look like HH:MM:SS.mmm
    int hh = 0, mm = 0, ss = 0, ms = 0;
    char c1 = 0, c2 = 0, dot = 0;
    if (sscanf(startPart.c_str(), "%d%c%d%c%d%c%d", &hh, &c1, &mm, &c2, &ss, &dot, &ms) != 7)
        return false;
    if (c1 != ':' || c2 != ':' || dot != '.')
        return false;

    startSeconds = ( hh * 3600.0 ) + ( mm * 60.0 ) + ss + (ms / 1000.0);
    return true;
}


//---------------------------------
//  Format frame number.

static string formatFrameNumber5(int frameNumber) {
    ostringstream oss;
    oss << setw(5) << setfill('0') << frameNumber;
    return oss.str();
}


//---------------------------------

int main() {

    string outputCaptionText;
    string templateLinesBeforeCaption;
    string templateLinesAfterCaption;
    string vttInputLine;
    string frameNumberStart;
    string svgOutputContent;

    int captionCount = 0;
    fs::path path_to_caption_zero_template;


    //---------------------------------
    // Copy file caption_frame_zero_template.svg to caption_frames_svg/caption_frame_00000.svg
    path_to_caption_zero_template = caption_frame_zero_template;
    std::ostringstream oss_render_zero_filename;
    oss_render_zero_filename << filename_caption_frame_zero;
    fs::path output_render_zero_path = templateFileDir / oss_render_zero_filename.str();
    fs::copy_file(
        path_to_caption_zero_template,
        output_render_zero_path,
        std::filesystem::copy_options::overwrite_existing
    );


    //---------------------------------
    // Open VTT input file.
    ifstream vttInputFile(inputVttFilename);
    if (!vttInputFile.is_open()) {
        cerr << "Failed to open VTT file: " << inputVttFilename << "\n";
        return 1;
    }


    //---------------------------------
    // Open SVG input template file.
    ifstream templateInputFile(inputSvgTemplateFilename);
    if (!templateInputFile.is_open()) {
        cerr << "Failed to open SVG template file: " << inputSvgTemplateFilename << "\n";
        return 1;
    }


    //---------------------------------
    // Read the SVG template, find the text "</text>", find the
    // ">" and "<" angle brackets that enclose the caption, and save
    // the content in two parts, before the caption and after the caption.
    // Pattern being found: ...>SOME_CAPTION</text>
    string templateContent((istreambuf_iterator<char>(templateInputFile)), istreambuf_iterator<char>());
    size_t closeTextPos = templateContent.find("</text>");
    if (closeTextPos == string::npos) {
        cerr << "Template does not contain </text>: " << inputSvgTemplateFilename << "\n";
        return 1;
    }
    size_t firstGreater = templateContent.rfind('>', closeTextPos);
    if (firstGreater == string::npos) {
        cerr << "Could not locate '>' before </text> in template.\n";
        return 1;
    }
    size_t firstLessAfterGreater = templateContent.find('<', firstGreater + 1);
    if (firstLessAfterGreater == string::npos) {
        cerr << "Could not locate '<' after '>' before </text> in template.\n";
        return 1;
    }
    templateLinesBeforeCaption = templateContent.substr(0, firstGreater + 1);
    templateLinesAfterCaption = templateContent.substr(firstLessAfterGreater);


    //---------------------------------
    //  Loop through VTT lines and process the captions and timestamps.
    while (true) {
        // Find next timing line
        string timingLine;
        while (getline(vttInputFile, timingLine)) {
            if (timingLine.find("-->") != string::npos) break;
        }
        if (!vttInputFile && timingLine.empty()) break;


        //---------------------------------
        //  Get the caption start and ending times, and convert into
        //  starting and ending frame numbers.
        double startSeconds = 0.0;
        if (!parseVttCueTimeRange(timingLine, startSeconds)) {
            if (!vttInputFile) break;
            continue;
        }
        int frameNumber = static_cast<int>(startSeconds * framesPerSecond);
        if (frameNumber < 0) frameNumber = 0;
        frameNumberStart = formatFrameNumber5(frameNumber);


        //---------------------------------
        //  Read caption text lines until empty line or end of file reached.
        outputCaptionText.clear();
        while (getline(vttInputFile, vttInputLine)) {
            if (vttInputLine.empty()) break;
            if (!outputCaptionText.empty()) outputCaptionText += "\n";
            outputCaptionText += vttInputLine;
        }


        //---------------------------------
        //  If the caption text is empty, copy the empty SVG template,
        //  Then repeat the loop.
        if (outputCaptionText.empty()) {
            path_to_caption_zero_template = caption_frame_zero_template;
            std::ostringstream oss_render_zero_filename;
            oss_render_zero_filename << filename_caption_frame_zero;
            fs::path output_render_zero_path = templateFileDir / oss_render_zero_filename.str();
            fs::copy_file(path_to_caption_zero_template, output_render_zero_path,
                std::filesystem::copy_options::overwrite_existing);
            continue;
        }


        //---------------------------------
        //  Increment caption count, and generate SVG output content.
        captionCount++;
        svgOutputContent = templateLinesBeforeCaption + outputCaptionText + templateLinesAfterCaption;


        //---------------------------------
        //  Write SVG output file
        fs::path outFilenamePath = templateFileDir / (outFilePrefixSvg + frameNumberStart + ".svg");
        std::string outFilename = outFilenamePath.string();
        ofstream outFile(outFilename);
        if (!outFile.is_open()) {
            cerr << "Failed to write SVG file: " << outFilename << "\n";
            return 1;
        }
        outFile << svgOutputContent;
        outFile.close();


        //---------------------------------
        //  Repeat the loop until done.
        if (!vttInputFile) break;
    }

    cout << "Wrote " << captionCount << " caption overlay files\n";
    return 0;
}
