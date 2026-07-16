//  captions_convert_vtt_to_svg.cpp
//  -------------------------------
//
//  Reads VTT file and caption_template.svg file,
//  generates SVG files with captions.
//  SVG filename includes frame number where the
//  caption starts.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <cctype>

using namespace std;

static const int framesPerSecond = 30;

static const string inputVttFilename = "output_captions_and_timing.vtt";
static const string inputSvgTemplateFilename = "caption_template.svg";
static const string templateFileDir = "caption_frames/";
static const string outFilePrefixSvg = "caption_frame_";

static bool parseVttCueTimeRange(const string& line, double& startSeconds) {
    // Expected format (typical):
    // 00:00:03.000 --> 00:00:05.000
    // We'll parse only the start time.
    auto arrowPos = line.find("-->");
    if (arrowPos == string::npos) return false;

    string startPart = line.substr(0, arrowPos);
    // trim
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

    // startPart should look like HH:MM:SS.mmm
    int hh = 0, mm = 0, ss = 0, ms = 0;
    char c1 = 0, c2 = 0, dot = 0;
    if (sscanf(startPart.c_str(), "%d%c%d%c%d%c%d", &hh, &c1, &mm, &c2, &ss, &dot, &ms) != 7)
        return false;
    if (c1 != ':' || c2 != ':' || dot != '.')
        return false;

    startSeconds = ( hh * 3600.0 ) + ( mm * 60.0 ) + ss + (ms / 1000.0);
    return true;
}

static string formatFrameNumber5(int frameNumber) {
    ostringstream oss;
    oss << setw(5) << setfill('0') << frameNumber;
    return oss.str();
}

int main() {
    string outputCaptionText;
    string templateLinesBeforeCaption;
    string templateLinesAfterCaption;
    string vttInputLine;
    string frameNumberStart;
    string svgOutputContent;
    int captionCount = 0;

    // Open VTT input file
    ifstream vttInputFile(inputVttFilename);
    if (!vttInputFile.is_open()) {
        cerr << "Failed to open VTT file: " << inputVttFilename << "\n";
        return 1;
    }

    // Open SVG template file
    ifstream templateInputFile(inputSvgTemplateFilename);
    if (!templateInputFile.is_open()) {
        cerr << "Failed to open SVG template file: " << inputSvgTemplateFilename << "\n";
        return 1;
    }

    // Read entire template (then split around the first '>' and first '<' after it)
    // (This matches the intent of your pseudocode that searches for the line containing </text>.)
    string templateContent((istreambuf_iterator<char>(templateInputFile)), istreambuf_iterator<char>());

    // Find the first occurrence of </text>
    size_t closeTextPos = templateContent.find("</text>");
    if (closeTextPos == string::npos) {
        cerr << "Template does not contain </text>: " << inputSvgTemplateFilename << "\n";
        return 1;
    }

    // From the start of that </text> region, locate the first '>' and the next '<'
    // so we can inject text between them.
    // Typically: ...>SOME_CAPTION</text>
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

    templateLinesBeforeCaption = templateContent.substr(0, firstGreater + 1); // include first '>'
    templateLinesAfterCaption = templateContent.substr(firstLessAfterGreater); // include first '<'

    // Loop through VTT lines and process cues
    while (true) {
        // Find next timing line
        string timingLine;
        while (getline(vttInputFile, timingLine)) {
            if (timingLine.find("-->") != string::npos) break;
        }
        if (!vttInputFile && timingLine.empty()) break;

        double startSeconds = 0.0;
        if (!parseVttCueTimeRange(timingLine, startSeconds)) {
            // If it's not parseable, keep going
            if (!vttInputFile) break;
            continue;
        }

        // Convert start time to frame number
        // Use floor to get the frame that contains the time.
        int frameNumber = static_cast<int>(startSeconds * framesPerSecond);
        if (frameNumber < 0) frameNumber = 0;
        frameNumberStart = formatFrameNumber5(frameNumber);

        // Get next vttInputLine into outputCaptionText
        outputCaptionText.clear();

        // Read caption text lines until blank line or EOF
        // (Common VTT behavior; fits your "repeat loop if empty" idea.)
        while (getline(vttInputFile, vttInputLine)) {
            // Stop at empty line (cue separator) or EOF
            if (vttInputLine.empty()) break;

            if (!outputCaptionText.empty()) outputCaptionText += "\n";
            outputCaptionText += vttInputLine;
        }

        // If caption text is empty, just continue (do not write an overlay)
        if (outputCaptionText.empty()) {
            continue;
        }

        captionCount++;

        svgOutputContent = templateLinesBeforeCaption + outputCaptionText + templateLinesAfterCaption;

        // Write SVG output file
        string outFilename = templateFileDir + outFilePrefixSvg + frameNumberStart + ".svg";
        ofstream outFile(outFilename);
        if (!outFile.is_open()) {
            cerr << "Failed to write SVG file: " << outFilename << "\n";
            return 1;
        }
        outFile << svgOutputContent;
        outFile.close();

        // continue to next cue
        if (!vttInputFile) break;
    }

    cout << "Wrote " << captionCount << " caption overlay files\n";
    return 0;
}
