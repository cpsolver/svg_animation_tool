/**
 * svg_animation_tool.cpp
 *
 * Script-driven SVG keyframe animator.
 * Reads a plain-text script file and generates interpolated SVG frames
 * suitable for assembling into an animation video using Inkscape and Shotcut.
 */

/*
 * MIT License
 *
 * Copyright (c) 2025 CPSolver at SolutionsCreative.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * -- Build ------------------------------------------------
 *
 *   g++ -std=c++17 -O2 -o svg_animation_tool svg_animation_tool.cpp
 *
 * -- Usage ------------------------------------------------
 *
 *   ./svg_animation_tool script.txt
 *
 *   The script file and all SVG files it references must be in the
 *   current working directory (no paths in filenames).
 *
 * -- Script language ------------------------------------------------
 *
 *   Tokens are whitespace-separated (spaces, tabs, newlines).
 *   There is no comment syntax — use text blocks (see below) instead.
 *
 *   Keyframe tokens:
 *
 *     someFile.svg   - Register a keyframe SVG. Pushed into a sliding
 *                      window of "last two seen". If a previously seen
 *                      SVG is bumped out without being used in an
 *                      'animate', a warning is printed.
 *
 *     animate        - Interpolate between the two most recently seen
 *                      SVG files, emitting frames_per_step output frames
 *                      with smootherstep easing. Requires at least two
 *                      SVG files. Consecutive 'animate' calls share their
 *                      boundary frame (last frame of segment N == first
 *                      frame of segment N+1).
 *
 *     freeze N       - Emit N identical copies of the most recently seen
 *                      keyframe. N must be a positive integer.
 *
 *   Text block tokens:
 *
 *     prefix-begin   - Opens a named text block. 'prefix' may contain
 *                      letters, digits, and hyphens. Everything after
 *                      this token is collected as raw text until a
 *                      standalone sequence of 3 or more dashes (----)
 *                      is found. The text is normalized (newlines to
 *                      spaces, multiple spaces collapsed, trimmed) and
 *                      accumulated under the prefix name. Multiple
 *                      blocks with the same prefix are concatenated
 *                      with a single space. Examples:
 *                        vocal-begin, comment-begin, animate-begin
 *                      All text blocks are written to the trace file.
 *                      Script directives inside a block are treated as
 *                      plain text and are not executed.
 *
 *   Object and motion directives:
 *
 *     object-ids     - Begins collecting a list of SVG element ids.
 *                      Subsequent unrecognized tokens are appended to
 *                      the list until another directive is seen.
 *
 *     spread-out [N] - Stagger start/end times of the most recent
 *                      object-ids by N frames per object (default 1).
 *                      Uses the current spread-out direction setting
 *                      (see spread-out-start-X-end-Y below).
 *
 *     spread-out-start-X-end-Y
 *                    - Set the sort direction for the next spread-out.
 *                      X controls start order (keyframe A positions);
 *                      Y controls end order (keyframe B positions).
 *                      Start and end must be on the same axis:
 *                        vertical:   top, bottom
 *                        horizontal: left, right
 *                      Valid examples:
 *                        spread-out-start-top-end-top     (default)
 *                        spread-out-start-top-end-bottom
 *                        spread-out-start-bottom-end-top
 *                        spread-out-start-bottom-end-bottom
 *                        spread-out-start-left-end-left
 *                        spread-out-start-left-end-right
 *                        spread-out-start-right-end-left
 *                        spread-out-start-right-end-right
 *                      Mixed-axis tokens (e.g. start-top-end-left) are
 *                      invalid and produce a warning. The setting
 *                      persists until changed.
 *
 *     arc-height [N] - Apply an upward arc to the Y (vertical) path
 *                      of the most recent object-ids. Peak height is
 *                      N percent of horizontal travel distance
 *                      (default 30). The arc is always vertical —
 *                      it lifts objects upward regardless of whether
 *                      spread-out uses a left/right stagger direction.
 *                      Uses the current arc-degrees setting.
 *
 *     arc-degrees [N] - Set the ellipse trim angle for arc-height
 *                       (default 20). Larger values give a flatter
 *                       peak. Does not by itself apply an arc.
 *
 *   Settings directives:
 *
 *     frames-per-step N  - Frames per animate segment (default 30).
 *                          Must be a positive integer.
 *
 *     output-directory D - Directory for output frames (default
 *                          "frames_svg"). Must not contain a period.
 *
 *     Any other token    - If a collecting mode is active (e.g.
 *                          object-ids), added to the active list.
 *                          Otherwise warned and skipped.
 *
 * -- Output files ------------------------------------------------
 *
 *   stdout                     - user-facing progress (animate #, frames)
 *   stderr                     - I/O errors only (file not found, etc.)
 *   output_trace_animate.txt   - verbose trace: element registry, numeric
 *                                values, change detection detail
 *   output_summary_animate.txt - concise run summary: settings, per-segment
 *                                change counts, animated id list
 *
 *   Frames are written as: <output_dir>/frame_NNNN.svg
 *   The frame counter is global across all animate/freeze operations.
 *   Trace and summary files are overwritten on each run.
 *   Easing is always smootherstep and is not configurable.
 *
 * -- Converting frames to video ------------------------------------------------
 *
 *   mkdir -p frames_png
 *   for f in frames_svg/frame_*.svg; do
 *     inkscape "$f" --export-type=png \
 *       --export-filename="frames_png/$(basename "${f%.svg}").png"
 *   done
 *   Then import the frames_png/ image sequence into Shotcut
 *   (or another video editor) to assemble the final movie.
 *
 * -- About SVG interpolation ------------------------------------------------
 *
 *   Each 'animate' segment uses a four-phase approach:
 *
 *   Phase 1 - Element registry: every id="..." in each SVG is found
 *     along with the line range of its opening tag.
 *
 *   Phase 2 - Value extraction: for each element, all numeric attribute
 *     values in its line range are collected with their attribute name,
 *     line number, and position index.
 *
 *   Phase 3 - Change detection: elements present in both A and B are
 *     compared by {attrName, valueIndex}. Only values that differ are
 *     recorded as ValueChange entries, each storing A and B line numbers
 *     independently (line-number drift between keyframes is handled).
 *
 *   Phase 4 - Frame generation with midpoint base switch:
 *     Frames before the midpoint use SVG A as the base document.
 *     Frames from the midpoint onward use SVG B as the base document.
 *     The midpoint is when objects are most spread apart, making the
 *     stacking-order switch least visible. Interpolated numeric values
 *     are patched directly into the base document lines; all other
 *     content is verbatim from the base.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <regex>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <optional>
#include <algorithm>

namespace fs = std::filesystem;

// ------------------------------------------------
// Easing functions
// ------------------------------------------------

double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double smootherstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// ------------------------------------------------
// File utilities
// ------------------------------------------------

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write file: " + path);
    f << content;
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
        lines.push_back(line);
    return lines;
}

// ------------------------------------------------
// Script tokenizer
// ------------------------------------------------

/// Returns true if tok is a valid -begin prefix token:
/// one or more chars from [a-zA-Z0-9-] followed by "-begin".
bool isBeginToken(const std::string& tok) {
    if (tok.size() <= 6) return false;  // must be longer than "-begin"
    if (tok.substr(tok.size() - 6) != "-begin") return false;
    // Prefix must be non-empty and contain only [a-zA-Z0-9-]
    std::string prefix = tok.substr(0, tok.size() - 6);
    if (prefix.empty()) return false;
    for (char c : prefix)
        if (!std::isalnum((unsigned char)c) && c != '-')
            return false;
    return true;
}

/// Returns true if tok is a closing delimiter: 3 or more dashes.
bool isClosingDashes(const std::string& tok) {
    if (tok.size() < 3) return false;
    for (char c : tok)
        if (c != '-') return false;
    return true;
}

/// Normalize collected block text:
///   - newlines → space
///   - collapse multiple spaces → one
///   - trim leading and trailing spaces
std::string normalizeBlockText(const std::string& raw) {
    std::string s = raw;
    // newlines → space
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    // collapse multiple spaces
    std::string result;
    bool lastWasSpace = true;  // true to trim leading spaces
    for (char c : s) {
        if (c == ' ') {
            if (!lastWasSpace) result += c;
            lastWasSpace = true;
        } else {
            result += c;
            lastWasSpace = false;
        }
    }
    // trim trailing space
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

/// Load and tokenize a script file.
/// - No comment syntax: all characters are significant.
/// - Tokens matching [a-zA-Z0-9-]+-begin open a text-collection block.
///   Raw text is collected until a standalone token of 3+ dashes is found.
///   Collected text is normalized and stored in scriptText[prefix].
///   If the same prefix appears again, it overwrites the previous entry.
/// - All other tokens are returned in the tokens vector for normal processing.
std::vector<std::string> loadScript(const std::string& path,
                                     std::map<std::string,std::string>& scriptText) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open script: " + path);

    // Read entire file as a string for easy raw-text extraction
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    std::vector<std::string> tokens;
    size_t pos = 0;
    size_t len = content.size();

    // Helper: skip whitespace
    auto skipWS = [&]() {
        while (pos < len && std::isspace((unsigned char)content[pos])) ++pos;
    };

    // Helper: read next whitespace-delimited token starting at pos
    // Returns "" if at end of file
    auto readTok = [&]() -> std::string {
        skipWS();
        if (pos >= len) return "";
        size_t start = pos;
        while (pos < len && !std::isspace((unsigned char)content[pos])) ++pos;
        return content.substr(start, pos - start);
    };

    while (pos < len) {
        size_t tokStart = pos;
        std::string tok = readTok();
        if (tok.empty()) break;

        if (isBeginToken(tok)) {
            // Extract prefix (everything before "-begin")
            std::string prefix = tok.substr(0, tok.size() - 6);

            // Collect raw text until closing dashes token
            std::string raw;
            while (pos < len) {
                // Peek at next token without consuming whitespace yet
                size_t savedPos = pos;
                std::string next = readTok();
                if (next.empty()) break;  // EOF — block not closed
                if (isClosingDashes(next)) break;  // found closing dashes
                // Not a closing token — append the whitespace + token to raw
                raw += content.substr(savedPos, pos - savedPos);
            }

            std::string normalized = normalizeBlockText(raw);
            if (!normalized.empty()) {
                auto it = scriptText.find(prefix);
                if (it == scriptText.end())
                    scriptText[prefix] = normalized;
                else
                    it->second += " " + normalized;
            }

        } else {
            tokens.push_back(tok);
        }
    }

    return tokens;
}

// ------------------------------------------------
// Phase 1 — Element registry
// ------------------------------------------------

struct NumericValue;  // forward declaration
struct Element {
    std::string id;
    int         tagOpenLine;
    int         idLine;
    int         tagCloseLine;
    std::vector<NumericValue> values;
};

std::string extractId(const std::string& s, size_t i) {
    if (s.substr(i, 4) != "id=\"") return "";
    bool standalone = (i == 0) || std::isspace((unsigned char)s[i - 1]);
    if (!standalone) return "";
    size_t start = i + 4;
    size_t end   = s.find('"', start);
    if (end == std::string::npos) return "";
    return s.substr(start, end - start);
}

/// Returns true if the id is a namedview element (namedview + digits).
/// These are Inkscape viewport elements that are never candidates for animation.
bool isNamedView(const std::string& id) {
    if (id.size() <= 9) return false;
    if (id.substr(0, 9) != "namedview") return false;
    for (size_t i = 9; i < id.size(); ++i)
        if (!std::isdigit((unsigned char)id[i])) return false;
    return id.size() > 9;  // must have at least one digit after "namedview"
}

/// Returns true if the id looks auto-generated by Inkscape:
/// one or more letters followed by 3 or more digits, with an optional
/// hyphen-number suffix (e.g. tspan1563, rect9235, text9031, use4-7).
/// These ids are still processed normally — this is display-only filtering.
bool isAutoGeneratedId(const std::string& id) {
    static const std::regex autoRe(R"(^[a-zA-Z]+[0-9]{3,}(-[0-9]+)?$)");
    return std::regex_match(id, autoRe);
}

/// Scan all lines of an SVG and build a map of id -> Element.
/// Phase 1 results are written to the trace file.
std::map<std::string, Element> parseElements(
        const std::vector<std::string>& lines,
        const std::string&              filename,
        std::ofstream&                  trace)
{
    std::map<std::string, Element> elements;

    int  tagOpenLine  = -1;
    bool inTag        = false;
    std::string pendingId;
    int         pendingIdLine   = -1;
    int         pendingOpenLine = -1;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        int lineNum = (int)(li + 1);

        for (size_t ci = 0; ci < line.size(); ++ci) {
            if (line[ci] == '<') {
                pendingId.clear();
                pendingIdLine   = -1;
                pendingOpenLine = lineNum;
                inTag = true;
            } else if (line[ci] == '>') {
                if (!pendingId.empty()) {
                    Element e;
                    e.id           = pendingId;
                    e.tagOpenLine  = pendingOpenLine;
                    e.idLine       = pendingIdLine;
                    e.tagCloseLine = lineNum;
                    elements[pendingId] = e;
                    pendingId.clear();
                    pendingIdLine   = -1;
                    pendingOpenLine = -1;
                }
                inTag = false;
            } else if (inTag) {
                if (line.substr(ci, 4) == "id=\"") {
                    bool standalone = (ci == 0) ||
                                      std::isspace((unsigned char)line[ci - 1]);
                    if (standalone) {
                        size_t start = ci + 4;
                        size_t end   = line.find('"', start);
                        if (end != std::string::npos) {
                            std::string captured = line.substr(start, end - start);
                            // Skip namedview elements entirely — they are
                            // Inkscape viewport metadata, never animated.
                            if (!isNamedView(captured)) {
                                pendingId     = captured;
                                pendingIdLine = lineNum;
                            }
                            ci = end;
                        }
                    }
                }
            }
        }
    }

    // Trace: element registry — show only uniquely named (non-auto-generated) ids
    int shownCount = 0;
    std::string listing;
    for (const auto& kv : elements) {
        const Element& e = kv.second;
        if (isAutoGeneratedId(e.id)) continue;
        listing += "  id=\"" + e.id + "\""
                 + "  open=" + std::to_string(e.tagOpenLine)
                 + "  id_line=" + std::to_string(e.idLine)
                 + "  close=" + std::to_string(e.tagCloseLine) + "\n";
        ++shownCount;
    }
    trace << "Uniquely named elements found in " << filename << ":\n"
          << listing
          << "  Shown: " << shownCount
          << "  Total registered: " << elements.size() << "\n";

    return elements;
}

// ------------------------------------------------
// Phase 2 — Numeric value extraction
// ------------------------------------------------

struct NumericValue {
    std::string attrName;
    int         lineNum;
    int         valueIndex;
    double      value;
};

std::string extractAttrName(const std::string& line, size_t numPos) {
    if (numPos == 0) return "(unknown)";
    size_t quotePos = line.rfind('"', numPos - 1);
    if (quotePos == std::string::npos || quotePos == 0) return "(unknown)";
    size_t eqPos = quotePos - 1;
    while (eqPos > 0 && std::isspace((unsigned char)line[eqPos])) --eqPos;
    if (line[eqPos] != '=') return "(unknown)";
    size_t nameEnd = eqPos - 1;
    while (nameEnd > 0 && std::isspace((unsigned char)line[nameEnd])) --nameEnd;
    size_t nameStart = nameEnd;
    while (nameStart > 0 &&
           (std::isalnum((unsigned char)line[nameStart - 1]) ||
            line[nameStart - 1] == '-' || line[nameStart - 1] == ':' ||
            line[nameStart - 1] == '_'))
        --nameStart;
    if (nameStart > nameEnd) return "(unknown)";
    return line.substr(nameStart, nameEnd - nameStart + 1);
}

std::vector<NumericValue> extractValues(
        const std::vector<std::string>& lines,
        int tagOpenLine,
        int tagCloseLine)
{
    static const std::regex numRe(R"(-?(?:\d+\.?\d*|\.\d+))");
    std::vector<NumericValue> values;
    int startIdx = std::max(0, tagOpenLine - 1);
    int endIdx   = std::min((int)lines.size() - 1, tagCloseLine - 1);
    for (int li = startIdx; li <= endIdx; ++li) {
        const std::string& line = lines[li];
        int lineNum = li + 1;
        int valueIndex = 0;
        for (auto it = std::sregex_iterator(line.begin(), line.end(), numRe);
             it != std::sregex_iterator(); ++it)
        {
            size_t pos = (size_t)it->position();
            if (pos > 0 && std::isalpha((unsigned char)line[pos - 1])) continue;
            NumericValue nv;
            nv.attrName   = extractAttrName(line, pos);
            nv.lineNum    = lineNum;
            nv.valueIndex = valueIndex;
            nv.value      = std::stod(it->str());
            values.push_back(nv);
            ++valueIndex;
        }
    }
    return values;
}

/// Add Phase 2 values to all elements; write detail to trace file.
void extractAllValues(
        std::map<std::string, Element>& elements,
        const std::vector<std::string>& lines,
        const std::string&              filename,
        std::ofstream&                  trace)
{
    for (auto& kv : elements) {
        Element& e = kv.second;
        e.values = extractValues(lines, e.tagOpenLine, e.tagCloseLine);
    }

    trace << "Numeric values in " << filename << ":\n";
    for (const auto& kv : elements) {
        const Element& e = kv.second;
        if (e.values.empty()) continue;
        trace << "  id=\"" << e.id << "\"\n";
        for (const auto& nv : e.values)
            trace << "    line=" << nv.lineNum
                  << "  attr=" << nv.attrName
                  << "  [" << nv.valueIndex << "]"
                  << "  value=" << nv.value << "\n";
    }
}

// ------------------------------------------------
// SVG file
// ------------------------------------------------

struct SvgFile {
    std::string                    filename;
    std::vector<std::string>       lines;
    std::map<std::string, Element> elements;
};

/// Load SVG, run Phase 1 & 2.  Progress goes to stdout; detail to trace.
SvgFile loadSvg(const std::string& filename, std::ofstream& trace) {
    std::cout << "  Loading: " << filename << "\n";
    auto lines = readLines(filename);
    std::cout << "    " << lines.size() << " lines\n";
    auto elements = parseElements(lines, filename, trace);
    extractAllValues(elements, lines, filename, trace);
    return { filename, std::move(lines), std::move(elements) };
}

// ------------------------------------------------
// Phase 3 — Change detection
// ------------------------------------------------

struct ValueChange {
    std::string id;
    std::string attrName;
    int         lineNumA;
    int         lineNumB;
    int         valueIndex;
    double      valueA;
    double      valueB;
};

/// Compare two parsed SVGs, return changed values.
/// Detail goes to trace file; summary line goes to both stdout and summary file.
std::vector<ValueChange> detectChanges(
        const SvgFile& a,
        const SvgFile& b,
        std::ofstream& trace,
        std::ofstream& summary,
        std::set<std::string>& changedIdsOut)
{
    std::vector<ValueChange> changes;

    trace << "Changes between " << a.filename
          << " and " << b.filename << ":\n";

    int sharedIds   = 0;
    int changedVals = 0;

    // Collect the set of ids that actually have changes (for summary and meld)
    std::set<std::string>& changedIds = changedIdsOut;
    changedIds.clear();

    for (const auto& kvA : a.elements) {
        const std::string& id    = kvA.first;
        const Element&     elemA = kvA.second;

        auto itB = b.elements.find(id);
        if (itB == b.elements.end()) continue;
        const Element& elemB = itB->second;
        ++sharedIds;

        using Key = std::pair<std::string, int>;
        std::map<Key, const NumericValue*> bLookup;
        for (const auto& nv : elemB.values)
            bLookup[{nv.attrName, nv.valueIndex}] = &nv;

        bool firstForId = true;
        for (const auto& nvA : elemA.values) {
            Key key{nvA.attrName, nvA.valueIndex};
            auto itNvB = bLookup.find(key);
            if (itNvB == bLookup.end()) continue;
            const NumericValue& nvB = *itNvB->second;
            if (std::fabs(nvA.value - nvB.value) < 1e-9) continue;

            ValueChange vc;
            vc.id         = id;
            vc.attrName   = nvA.attrName;
            vc.lineNumA   = nvA.lineNum;
            vc.lineNumB   = nvB.lineNum;
            vc.valueIndex = nvA.valueIndex;
            vc.valueA     = nvA.value;
            vc.valueB     = nvB.value;
            changes.push_back(vc);
            ++changedVals;
            changedIds.insert(id);

            if (firstForId) {
                trace << "  id=\"" << id << "\"\n";
                firstForId = false;
            }
            trace << "    attr=" << nvA.attrName
                  << "  [" << nvA.valueIndex << "]"
                  << "  lineA=" << nvA.lineNum
                  << "  lineB=" << nvB.lineNum
                  << "  A=" << nvA.value
                  << "  B=" << nvB.value << "\n";
        }
    }

    std::string changeLine = "  Shared ids: " + std::to_string(sharedIds)
                           + "  Changed values: " + std::to_string(changedVals);
    std::cout  << changeLine << "\n";
    summary    << changeLine << "\n";
    trace      << changeLine << "\n";

    // Write space-delimited list of animated ids to summary
    if (!changedIds.empty()) {
        summary << "  Animated ids: ";
        bool first = true;
        for (const auto& id : changedIds) {
            if (!first) summary << " ";
            summary << id;
            first = false;
        }
        summary << "\n";
    }

    return changes;
}


// ------------------------------------------------
// Arc path
// ------------------------------------------------

/// One spread-out directive, capturing the object-ids list, delay, and
/// sort directions for start (keyframe A) and end (keyframe B) ordering.
/// startDir / endDir are one of: "top" "bottom" "left" "right"
struct SpreadEntry {
    std::vector<std::string> ids;
    int                      delay;
    std::string              startDir;  // default "top"
    std::string              endDir;    // default "top"
};

/// Per-object timing computed from a SpreadEntry at animate time.
struct ObjectTiming {
    std::string id;
    int         startFrame;
    int         endFrame;
};

/// One arc-height directive, capturing the object-ids list and shape
/// parameters at the moment the directive was encountered.
struct ArcEntry {
    std::vector<std::string> ids;         // snapshot of object-ids at that moment
    int                      peakPercent; // peak height as % of horizontal span
    int                      trimDegrees; // ellipse trim angle (default 20)
};

/// Compute the Y arc offset at animation parameter t (0..1).
///
/// The arc is a trimmed half-ellipse:
///   - Full half-ellipse runs 0° to 180°.
///   - Trim angle trims both ends, so the arc runs trimDeg° to (180-trimDeg°).
///   - At angle theta: x_raw = cos(theta), y_raw = sin(theta).
///   - Normalized so the arc endpoints map to y=0 and peak maps to y=1.
///   - Scaled by (peakPercent/100) * hSpan.
///   - Result is subtracted from Y (Y increases downward, arc lifts upward).
///
/// hSpan is the absolute horizontal distance the object travels.
double computeArcOffset(double t, int trimDegrees, int peakPercent, double hSpan)
{
    const double PI  = std::acos(-1.0);
    double trimRad   = trimDegrees * PI / 180.0;
    // Angle at parameter t within [trimRad, PI - trimRad]
    double theta     = trimRad + t * (PI - 2.0 * trimRad);
    double yRaw      = std::sin(theta);
    double yBase     = std::sin(trimRad);   // y at the endpoints
    // Normalize so endpoints = 0, peak = 1
    double yNorm     = (yRaw - yBase) / (1.0 - yBase);
    double peakHeight = (peakPercent / 100.0) * hSpan;
    return yNorm * peakHeight;
}

// ------------------------------------------------
// Phase 4 — Frame generation with midpoint base switch
// ------------------------------------------------

std::string formatValue(double vi,
                        const std::string& srcA,
                        const std::string& srcB)
{
    bool srcAisInt = (srcA.find('.') == std::string::npos);
    bool srcBisInt = (srcB.find('.') == std::string::npos);
    if (srcAisInt && srcBisInt && std::fabs(vi - std::round(vi)) < 1e-9 && std::fabs(vi) > 1e-9 && std::fabs(vi - 1.0) > 1e-9)
        return std::to_string((long long)std::llround(vi));
    const std::string& decSrc = !srcAisInt ? srcA : srcB;
    size_t dotPos    = decSrc.find('.');
    int    decPlaces = (dotPos == std::string::npos)
                       ? 1 : (int)(decSrc.size() - dotPos - 1);
    decPlaces = std::max(decPlaces, 1);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decPlaces) << vi;
    return oss.str();
}

std::string generateFrame(const SvgFile&                              svgA,
                           const SvgFile&                              svgB,
                           const std::vector<ValueChange>&             changes,
                           const std::vector<ArcEntry>&                arcEntries,
                           const std::map<std::string, ObjectTiming>&  timingMap,
                           int                                         frameIndex,
                           int                                         expandedFrames,
                           int                                         midFrame,
                           double                                      tGlobal)
{
    bool useB = (frameIndex >= midFrame);
    const SvgFile& base = useB ? svgB : svgA;
    std::vector<std::string> outLines = base.lines;

    // Compute per-object t, falling back to tGlobal for ids not in timingMap.
    // localT is clamped to [0,1] then eased.
    auto getT = [&](const std::string& id) -> double {
        auto it = timingMap.find(id);
        if (it == timingMap.end()) return tGlobal;
        const ObjectTiming& ot = it->second;
        int   span    = ot.endFrame - ot.startFrame;
        if (span <= 0) return 1.0;
        double local  = (double)(frameIndex - ot.startFrame) / (double)span;
        local = std::clamp(local, 0.0, 1.0);
        return smootherstep(local);
    };

    static const std::regex numRe(R"(-?(?:\d+\.?\d*|\.\d+))");

    for (const auto& vc : changes) {
        int lineNum = useB ? vc.lineNumB : vc.lineNumA;
        int lineIdx = lineNum - 1;
        if (lineIdx < 0 || lineIdx >= (int)outLines.size()) continue;

        std::string& line = outLines[lineIdx];
        int idx = 0;
        std::string result;
        size_t prev = 0;
        bool patched = false;

        for (auto it = std::sregex_iterator(line.begin(), line.end(), numRe);
             it != std::sregex_iterator(); ++it)
        {
            size_t pos = (size_t)it->position();
            if (pos > 0 && std::isalpha((unsigned char)line[pos - 1])) continue;
            if (idx == vc.valueIndex) {
                result += line.substr(prev, pos - prev);
                double tEased = getT(vc.id);
                double vi = vc.valueA + (vc.valueB - vc.valueA) * tEased;
                std::ostringstream srcA_str, srcB_str;
                srcA_str << vc.valueA;
                srcB_str << vc.valueB;
                result += formatValue(vi, srcA_str.str(), srcB_str.str());
                prev = pos + it->str().size();
                patched = true;
                break;
            }
            ++idx;
        }

        if (patched) {
            result += line.substr(prev);
            line = result;
        }
    }

    // Apply arc Y offsets for each ArcEntry
    static const std::regex numRe2(R"(-?(?:\d+\.?\d*|\.\d+))");
    for (const auto& arc : arcEntries) {
        for (const auto& id : arc.ids) {
            // Find the ValueChange for this id's Y translation (transform [5])
            // and X translation (transform [4]) to compute hSpan.
            double hSpan   = 0.0;
            int    yLineNum = -1;
            int    yValIdx  = -1;
            double yValueAtT = 0.0;

            double tArc = getT(id);
            for (const auto& vc : changes) {
                if (vc.id != id) continue;
                if (vc.attrName == "transform" && vc.valueIndex == 4)
                    hSpan = std::fabs(vc.valueB - vc.valueA);
                if (vc.attrName == "transform" && vc.valueIndex == 5) {
                    yLineNum  = useB ? vc.lineNumB : vc.lineNumA;
                    yValIdx   = vc.valueIndex;
                    yValueAtT = vc.valueA + (vc.valueB - vc.valueA) * tArc;
                }
            }

            if (yLineNum < 0 || hSpan < 1e-9) continue;

            double offset = computeArcOffset(tArc, arc.trimDegrees,
                                             arc.peakPercent, hSpan);
            double newY   = yValueAtT - offset;

            int lineIdx = yLineNum - 1;
            if (lineIdx < 0 || lineIdx >= (int)outLines.size()) continue;
            std::string& line = outLines[lineIdx];

            // Patch the value at yValIdx on this line
            int idx2 = 0;
            std::string result2;
            size_t prev2 = 0;
            bool patched2 = false;
            for (auto it = std::sregex_iterator(line.begin(), line.end(), numRe2);
                 it != std::sregex_iterator(); ++it)
            {
                size_t pos = (size_t)it->position();
                if (pos > 0 && std::isalpha((unsigned char)line[pos - 1])) continue;
                if (idx2 == yValIdx) {
                    result2 += line.substr(prev2, pos - prev2);
                    std::ostringstream sa, sb;
                    sa << yValueAtT; sb << yValueAtT;
                    result2 += formatValue(newY, sa.str(), sb.str());
                    prev2 = pos + it->str().size();
                    patched2 = true;
                    break;
                }
                ++idx2;
            }
            if (patched2) {
                result2 += line.substr(prev2);
                line = result2;
            }
        }
    }

    std::string out;
    out.reserve(outLines.size() * 80);
    for (const auto& ln : outLines)
        out += ln + '\n';
    return out;
}

// ------------------------------------------------
// Frame writer
// ------------------------------------------------

void writeFrame(const std::string& outDir,
                int                frameNum,
                int                digits,
                const std::string& svgContent)
{
    std::ostringstream fname;
    fname << outDir << "/frame_"
          << std::setw(digits) << std::setfill('0') << frameNum
          << ".svg";
    writeFile(fname.str(), svgContent);
}

// ------------------------------------------------
// main
// ------------------------------------------------

int main(int argc, char* argv[]) {

    // ── Hardcoded options ─────────────────────────────────────────────────────
    const std::string TRACE_FILE   = "output_trace_animate.txt";
    const std::string SUMMARY_FILE = "output_summary_animate.txt";

    // ── Script-controlled options (set by directives before first animate) ────
    int         frames_per_step = 30;          // frames-per-step directive
    std::string output_dir      = "frames_svg"; // output-directory directive

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <script.txt>\n"
                  << "  file.svg   — register a keyframe\n"
                  << "  animate    — interpolate between last two keyframes\n"
                  << "  freeze N   — repeat current keyframe N times\n"
                  << "  object-ids — begin collecting element id list\n"
                  << "  # ...      — comment\n";
        return 1;
    }

    const std::string scriptPath = argv[1];

    // ── Load script ──────────────────────────────────────────────────────────
    std::map<std::string,std::string> scriptText;  // prefix -> normalized text
    std::vector<std::string> scriptTokens;
    try {
        scriptTokens = loadScript(scriptPath, scriptText);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    if (scriptTokens.empty()) {
        std::cerr << "Error: script file is empty or has no tokens.\n";
        return 1;
    }

    // ── Open trace and summary files ─────────────────────────────────────────
    std::ofstream trace(TRACE_FILE);
    if (!trace) {
        std::cerr << "Error: cannot open trace file: " << TRACE_FILE << "\n";
        return 1;
    }
    std::ofstream summary(SUMMARY_FILE);
    if (!summary) {
        std::cerr << "Error: cannot open summary file: " << SUMMARY_FILE << "\n";
        return 1;
    }


    // ── Settings header — stdout and summary ─────────────────────────────────
    std::cout << "Script: " << scriptPath << "  ("
              << scriptTokens.size() << " token(s))\n\n";

    // Settings header printed after directives are processed (below).
    // Summary header is written after output_dir is confirmed valid.

    trace   << "Script: " << scriptPath << "\n\n";

    // Log any collected text blocks
    if (!scriptText.empty()) {
        trace << "Text blocks:\n";
        for (const auto& kv : scriptText)
            trace << "  [" << kv.first << "]: " << kv.second << "\n";
        trace << "\n";
    }

    // Settings header printed to stdout/summary after all directives processed.
    // (frames_per_step and output_dir may be changed by directives.)

    // ── Script execution state ───────────────────────────────────────────────
    std::deque<SvgFile> window;
    std::deque<bool>    windowUsed;

    int  globalFrame       = 0;
    int  animateCount      = 0;
    bool prevWasAnimate    = false;
    bool firstSvgSeen      = false;  // locks output_dir and triggers cleanup
    std::string meldSection;  // accumulated meld lines, written at end
    const int DIGITS    = 4;

    // ── Directive state ──────────────────────────────────────────────────────
    std::string              collectingMode;
    std::vector<std::string> objectIds;

    int spreadOut     = -1;
    int         currentArcDeg   = 20;    // updated by arc-degrees
    std::string currentSpreadStart = "top";  // updated by spread-out-start-*-end-*
    std::string currentSpreadEnd   = "top";  // updated by spread-out-start-*-end-*
    // frames_per_step and output_dir are declared above and updated by directives

    // Arc entries: one per arc-height directive, each with its own id snapshot
    std::vector<ArcEntry> arcEntries;

    // Spread entries: one per spread-out directive, each with its own id snapshot
    std::vector<SpreadEntry> spreadEntries;

    // ── Helper: consume optional integer parameter ───────────────────────────
    auto consumeOptionalInt = [&](size_t& idx) -> int {
        if (idx + 1 >= scriptTokens.size()) return -1;
        const std::string& next = scriptTokens[idx + 1];
        try {
            size_t pos = 0;
            int val = std::stoi(next, &pos);
            if (pos == next.size()) { ++idx; return val; }
        } catch (...) {}
        return -1;
    };

    // ── Helper: flush object-ids list to summary if non-empty ───────────────
    auto flushObjectIds = [&]() {
        if (objectIds.empty()) return;
        summary << "Object ids (" << objectIds.size() << "):\n";
        for (size_t k = 0; k < objectIds.size(); ++k) {
            if (k) summary << " ";
            summary << objectIds[k];
        }
        summary << "\n";
        objectIds.clear();   // prevent re-printing on next flush
    };

    // ── Process tokens ───────────────────────────────────────────────────────
    for (size_t i = 0; i < scriptTokens.size(); ) {
        const std::string& tok = scriptTokens[i];

        // ── SVG filename ─────────────────────────────────────────────────────
        if (tok.size() > 4 && tok.substr(tok.size() - 4) == ".svg") {
            flushObjectIds();
            collectingMode = "";

            // First SVG seen: lock output_dir, create it, and delete any
            // stale frame_NNNN.svg files from a previous run.
            if (!firstSvgSeen) {
                firstSvgSeen = true;
                try {
                    fs::create_directories(output_dir);
                } catch (...) {
                    std::cerr << "Error: cannot create output directory: "
                              << output_dir << "\n";
                    return 1;
                }
                int deleted = 0;
                std::regex frameRe(R"(frame_[0-9]+\.svg)");
                for (const auto& entry : fs::directory_iterator(output_dir)) {
                    if (entry.is_regular_file() &&
                        std::regex_match(entry.path().filename().string(), frameRe)) {
                        fs::remove(entry.path());
                        ++deleted;
                    }
                }
                if (deleted > 0)
                    std::cout << "  Cleared " << deleted
                              << " old frame(s) from '" << output_dir << "/'\n";
            }

            if (window.size() == 2 && !windowUsed[0])
                std::cout << "WARNING: '" << window[0].filename
                          << "' was pushed out without being used in 'animate'.\n";

            SvgFile svg;
            try {
                svg = loadSvg(tok, trace);
            } catch (const std::exception& e) {
                std::cerr << "Error loading '" << tok << "': " << e.what() << "\n";
                ++i; continue;
            }

            if (window.size() == 2) { window.pop_front(); windowUsed.pop_front(); }
            window.push_back(std::move(svg));
            windowUsed.push_back(false);
            prevWasAnimate = false;
            ++i; continue;
        }

        // ── animate ──────────────────────────────────────────────────────────
        if (tok == "animate") {
            flushObjectIds();
            collectingMode = "";
            if (window.size() < 2) {
                std::cout << "WARNING: 'animate' requires two keyframes; only "
                          << window.size() << " available. Skipping.\n";
                ++i; continue;
            }

            const SvgFile& svgA = window[0];
            const SvgFile& svgB = window[1];

            ++animateCount;
            std::string animHeader = "animate #" + std::to_string(animateCount)
                                   + ": " + svgA.filename
                                   + " → " + svgB.filename;
            std::cout  << animHeader << "\n";
            summary    << animHeader << "\n";
            trace      << animHeader << "\n";

            // Phase 3
            std::set<std::string> changedIds;
            auto changes = detectChanges(svgA, svgB, trace, summary, changedIds);

            // ── Step 1: spread-out timing ────────────────────────────────────
            // Build a quick lookup: id -> {xA, yA, xB, yB}
            struct Pos { double xA=0, yA=0, xB=0, yB=0; };
            std::map<std::string, Pos> posMap;
            for (const auto& vc : changes) {
                if (vc.attrName != "transform") continue;
                auto& p = posMap[vc.id];
                if (vc.valueIndex == 4) { p.xA = vc.valueA; p.xB = vc.valueB; }
                if (vc.valueIndex == 5) { p.yA = vc.valueA; p.yB = vc.valueB; }
            }

            // Per-object timing for this segment
            std::map<std::string, ObjectTiming> timingMap;
            int expandedFrames = frames_per_step;

            for (const auto& se : spreadEntries) {
                if (se.ids.empty()) continue;

                // Filter to only ids that actually move in this segment
                std::vector<std::string> activeIds;
                for (const auto& id : se.ids)
                    if (posMap.count(id)) activeIds.push_back(id);

                if (activeIds.empty()) {
                    trace << "  spread-out: no moving ids in this segment, skipping\n";
                    continue;
                }

                int n        = (int)activeIds.size();
                int delay    = se.delay;
                int expanded = frames_per_step + delay * (n - 1);
                if (expanded > expandedFrames) expandedFrames = expanded;

                // Sort comparator driven by direction string and A/B keyframe
                // dir: "top"=asc Y, "bottom"=desc Y, "left"=asc X, "right"=desc X
                auto makeComparator = [&](const std::string& dir, bool useA) {
                    return [&, dir, useA](const std::string& ia,
                                         const std::string& ib) -> bool {
                        const Pos& pa = posMap[ia];
                        const Pos& pb = posMap[ib];
                        double va, vb;
                        if (dir == "top") {
                            va = useA ? pa.yA : pa.yB;
                            vb = useA ? pb.yA : pb.yB;
                            return va < vb;  // lower Y = higher on screen = first
                        } else if (dir == "bottom") {
                            va = useA ? pa.yA : pa.yB;
                            vb = useA ? pb.yA : pb.yB;
                            return va > vb;  // higher Y = lower on screen = first
                        } else if (dir == "left") {
                            va = useA ? pa.xA : pa.xB;
                            vb = useA ? pb.xA : pb.xB;
                            return va < vb;  // lower X = leftmost = first
                        } else {  // "right"
                            va = useA ? pa.xA : pa.xB;
                            vb = useA ? pb.xA : pb.xB;
                            return va > vb;  // higher X = rightmost = first
                        }
                    };
                };

                std::vector<std::string> startOrder = activeIds;
                std::sort(startOrder.begin(), startOrder.end(),
                          makeComparator(se.startDir, true));

                std::vector<std::string> endOrder = activeIds;
                std::sort(endOrder.begin(), endOrder.end(),
                          makeComparator(se.endDir, false));

                // Build rank maps
                std::map<std::string, int> startRank, endRank;
                for (int r = 0; r < n; ++r) {
                    startRank[startOrder[r]] = r;
                    endRank[endOrder[r]]     = r;
                }

                // Assign and log
                trace << "  spread-out timing (delay=" << delay
                      << "  n=" << n
                      << "  expanded=" << expanded
                      << "  start=" << se.startDir
                      << "  end=" << se.endDir << "):\n";
                trace << "    " << std::left << std::setw(18) << "id"
                      << std::setw(12) << "startFrame"
                      << std::setw(10) << "endFrame"
                      << std::setw(10) << "yA"
                      << "yB\n";

                for (const auto& id : activeIds) {
                    ObjectTiming ot;
                    ot.id         = id;
                    ot.startFrame = startRank[id] * delay;
                    ot.endFrame   = (expanded - 1) - endRank[id] * delay;
                    timingMap[id] = ot;

                    trace << "    " << std::left << std::setw(18) << id
                          << std::setw(12) << ot.startFrame
                          << std::setw(10) << ot.endFrame
                          << std::setw(10) << posMap[id].yA
                          << posMap[id].yB << "\n";
                }
            }

            // Step 2 trace: per-object local t at key frames
            if (!timingMap.empty()) {
                // Sample at ~5 evenly spaced frames across expanded segment
                std::vector<int> sampleFrames;
                int nSamples = std::min(5, expandedFrames);
                for (int s = 0; s < nSamples; ++s)
                    sampleFrames.push_back(s * (expandedFrames - 1) / (nSamples - 1));

                trace << "  per-object t at sample frames:\n";
                trace << "    " << std::left << std::setw(18) << "id";
                for (int sf : sampleFrames)
                    trace << std::setw(8) << ("f=" + std::to_string(sf));
                trace << "\n";

                for (const auto& kv : timingMap) {
                    const ObjectTiming& ot = kv.second;
                    trace << "    " << std::left << std::setw(18) << ot.id;
                    for (int sf : sampleFrames) {
                        int span = ot.endFrame - ot.startFrame;
                        double local = (span <= 0) ? 1.0
                            : std::clamp((double)(sf - ot.startFrame) / span, 0.0, 1.0);
                        double tval = smootherstep(local);
                        std::ostringstream os;
                        os << std::fixed << std::setprecision(3) << tval;
                        trace << std::setw(8) << os.str();
                    }
                    trace << "\n";
                }
            }

            // Phase 4
            int fStart        = prevWasAnimate ? 1 : 0;
            int firstFrameNum = globalFrame;
            int midFrame      = expandedFrames / 2;

            trace << "  expandedFrames=" << expandedFrames
                  << "  midFrame=" << midFrame << "\n";

            for (int f = fStart; f < expandedFrames; ++f) {
                double tLinear = (expandedFrames == 1)
                                 ? 0.0
                                 : (double)f / (double)(expandedFrames - 1);
                double tEased  = smootherstep(tLinear);
                std::string svgOut = generateFrame(svgA, svgB, changes,
                                                   arcEntries, timingMap,
                                                   f, expandedFrames, midFrame,
                                                   tEased);
                writeFrame(output_dir, globalFrame, DIGITS, svgOut);
                ++globalFrame;
            }

            std::string frameRange = "    Frames " + std::to_string(firstFrameNum)
                                   + " – " + std::to_string(globalFrame - 1)
                                   + "  (" + std::to_string(globalFrame - firstFrameNum)
                                   + " written)";
            std::cout  << frameRange << "\n";
            summary    << frameRange << "\n";

            // Accumulate meld entry for this segment
            meldSection += "meld  " + svgA.filename + "  " + svgB.filename + "\n\n";
            for (const auto& id : changedIds)
                meldSection += "#  " + id + "\n";
            meldSection += "\n";

            windowUsed[0] = true;
            windowUsed[1] = true;
            prevWasAnimate = true;
            ++i; continue;
        }

        // ── freeze N ─────────────────────────────────────────────────────────
        if (tok == "freeze") {
            flushObjectIds();
            collectingMode = "";
            if (i + 1 >= scriptTokens.size()) { ++i; continue; }
            int freezeN = 0;
            try { freezeN = std::stoi(scriptTokens[i + 1]); } catch (...) { ++i; continue; }
            if (freezeN <= 0) { i += 2; continue; }
            if (window.empty()) { i += 2; continue; }

            const SvgFile& current = window.back();
            int firstFrameNum = globalFrame;
            std::string frozenSvg;
            for (const auto& ln : current.lines) frozenSvg += ln + '\n';
            for (int f = 0; f < freezeN; ++f) {
                writeFrame(output_dir, globalFrame, DIGITS, frozenSvg);
                ++globalFrame;
            }

            std::string freezeLine = "freeze " + std::to_string(freezeN)
                                   + ": " + current.filename
                                   + "  Frames " + std::to_string(firstFrameNum)
                                   + " – " + std::to_string(globalFrame - 1);
            std::cout  << freezeLine << "\n";
            summary    << freezeLine << "\n";

            prevWasAnimate = false;
            i += 2; continue;
        }

        // ── object-ids ───────────────────────────────────────────────────────
        if (tok == "object-ids") {
            flushObjectIds();
            objectIds.clear();
            collectingMode = "object-ids";
            trace << "object-ids: collecting\n";
            ++i; continue;
        }

        // ── spread-out-start-X-end-Y direction directive ─────────────────────
        // Parses tokens like spread-out-start-top-end-bottom.
        // Start and end must be on the same axis (both vertical or both horizontal).
        // Valid directions: top, bottom (vertical) / left, right (horizontal).
        // Updates currentSpreadStart and currentSpreadEnd for the next spread-out.
        if (tok.substr(0, 17) == "spread-out-start-") {
            // Split token on '-' to extract direction words
            // Expected format: spread-out-start-{dir}-end-{dir}
            // Parts: spread / out / start / {startDir} / end / {endDir}
            std::vector<std::string> parts;
            std::istringstream iss2(tok);
            std::string part;
            while (std::getline(iss2, part, '-'))
                if (!part.empty()) parts.push_back(part);

            bool valid = false;
            if (parts.size() == 6 &&
                parts[0] == "spread" && parts[1] == "out" &&
                parts[2] == "start" && parts[4] == "end")
            {
                std::string sd = parts[3];
                std::string ed = parts[5];
                bool sdVert = (sd == "top" || sd == "bottom");
                bool sdHorz = (sd == "left" || sd == "right");
                bool edVert = (ed == "top" || ed == "bottom");
                bool edHorz = (ed == "left" || ed == "right");
                // Validate same axis
                if ((sdVert && edVert) || (sdHorz && edHorz)) {
                    currentSpreadStart = sd;
                    currentSpreadEnd   = ed;
                    trace   << "spread-out direction: start=" << sd
                            << "  end=" << ed << "\n";
                    summary << "spread-out direction: start=" << sd
                            << "  end=" << ed << "\n";
                    valid = true;
                }
            }
            if (!valid)
                std::cout << "WARNING: invalid spread-out direction token '"
                          << tok << "' — ignored.\n";
            ++i; continue;
        }

        // ── spread-out ────────────────────────────────────────────────────────
        if (tok == "spread-out") {
            collectingMode = "";
            spreadOut = consumeOptionalInt(i);
            if (spreadOut == -1) spreadOut = 1;
            // Snapshot current objectIds into a SpreadEntry
            SpreadEntry se;
            se.ids      = objectIds;
            se.delay    = spreadOut;
            se.startDir = currentSpreadStart;
            se.endDir   = currentSpreadEnd;
            spreadEntries.push_back(se);
            trace   << "spread-out: delay=" << spreadOut
                    << "  ids=" << objectIds.size() << "\n";
            summary << "spread-out    : delay=" << spreadOut << "  ids:";
            for (const auto& id : objectIds) summary << " " << id;
            summary << "\n";
            ++i; continue;
        }

        // ── arc-degrees — shape modifier, updates current trim angle ─────────
        if (tok == "arc-degrees") {
            int val = consumeOptionalInt(i);
            if (val != -1) currentArcDeg = val;
            trace   << "arc-degrees: " << currentArcDeg << "\n";
            summary << "arc-degrees   : " << currentArcDeg << "\n";
            ++i; continue;
        }

        // ── arc-height — triggers arc for current object-ids ──────────────────
        if (tok == "arc-height") {
            collectingMode = "";
            int peakPct = consumeOptionalInt(i);
            if (peakPct == -1) peakPct = 30;  // default 30%
            // Snapshot the current objectIds list into a new ArcEntry
            ArcEntry ae;
            ae.ids         = objectIds;
            ae.peakPercent = peakPct;
            ae.trimDegrees = currentArcDeg;
            arcEntries.push_back(ae);
            // Log
            trace << "arc-height: peak=" << peakPct
                  << "%  trim=" << currentArcDeg << "deg"
                  << "  ids=" << objectIds.size() << "\n";
            summary << "arc-height    : peak=" << peakPct
                    << "%  trim=" << currentArcDeg << "deg"
                    << "  ids:";
            for (const auto& id : objectIds) summary << " " << id;
            summary << "\n";
            // Do NOT flush/clear objectIds — the list may be reused by other directives
            ++i; continue;
        }

        // ── frames-per-step ──────────────────────────────────────────────────
        if (tok == "frames-per-step") {
            flushObjectIds();
            collectingMode = "";
            int val = consumeOptionalInt(i);
            if (val > 0) {
                frames_per_step = val;
                trace   << "frames-per-step: " << frames_per_step << "\n";
                summary << "frames-per-step : " << frames_per_step << "\n";
            } else {
                std::cout << "WARNING: frames-per-step requires a positive integer — ignored.\n";
            }
            ++i; continue;
        }

        // ── output-directory ─────────────────────────────────────────────────
        if (tok == "output-directory") {
            flushObjectIds();
            collectingMode = "";
            if (i + 1 < scriptTokens.size()) {
                const std::string& dir = scriptTokens[i + 1];
                if (firstSvgSeen) {
                    std::cout << "WARNING: output-directory ignored — "
                              << "cannot change after first SVG is loaded.\n";
                    ++i;  // consume the name token
                } else if (dir.find('.') != std::string::npos) {
                    std::cout << "WARNING: output-directory '" << dir
                              << "' contains a period — ignored.\n";
                    ++i;
                } else {
                    output_dir = dir;
                    trace   << "output-directory: " << output_dir << "\n";
                    summary << "output-directory : " << output_dir << "\n";
                    ++i;
                }
            } else {
                std::cout << "WARNING: output-directory requires a name — ignored.\n";
            }
            ++i; continue;
        }

        // ── Unknown token — add to active list, or warn ───────────────────────
        if (collectingMode == "object-ids") {
            objectIds.push_back(tok);
            trace << "  object-id: \"" << tok << "\"\n";
            ++i; continue;
        }

        std::cout << "WARNING: unknown token '" << tok << "' — skipping.\n";
        ++i;
    }

    // ── Flush any remaining directive state ─────────────────────────────────
    flushObjectIds();

    // ── Print settings header now that directives are all processed ─────────
    std::cout << "Frames/step    : " << frames_per_step << "\n"
              << "Easing         : smootherstep\n"
              << "Output dir     : " << output_dir << "/\n\n";

    summary << "Frames/step    : " << frames_per_step << "\n"
            << "Easing         : smootherstep\n"
            << "Output dir     : " << output_dir << "/\n"
            << "Trace file     : " << TRACE_FILE << "\n\n";

    // ── Final ────────────────────────────────────────────────────────────────
    std::string doneMsg = "Done!  " + std::to_string(globalFrame)
                        + " frame(s) written to '" + output_dir + "/'.";
    std::cout  << "\n══════════════════════════════════════════════════\n"
               << doneMsg << "\n\n"
               << "Convert SVG frames to PNG using Inkscape CLI:\n\n"
               << "  mkdir -p frames_png\n"
               << "  for f in " << output_dir << "/frame_*.svg; do\n"
               << "    inkscape \"$f\" --export-type=png \\\n"
               << "      --export-filename=\"frames_png/$(basename \"${f%.svg}\").png\"\n"
               << "  done\n\n"
               << "Then import the frames_png/ image sequence into Shotcut\n"
               << "(or another video editor) to assemble the final movie.\n"
               << "Source code and documentation at:\n"
               << "https://github.com/cpsolver/svg_animation_tool\n"
               << "══════════════════════════════════════════════════\n";
    summary    << "\n" << doneMsg << "\n";

    // Write meld section at the end of the summary
    if (!meldSection.empty())
        summary << "\nMeld diff commands with ID reminders:\n" << meldSection;
    trace      << "\n" << doneMsg << "\n";

    return 0;
}
