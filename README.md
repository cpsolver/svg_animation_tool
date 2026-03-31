# svg_animation_tool

This **SVG animation tool** transforms your **simple script** and **Inkscape SVG files** into numbered SVG files that Shotcut (or any video editor that accepts an image sequence) can convert into an **animated video**.

This tool features **script-controlled options** including **ease-in and ease-out transitions**, **staggered group motion**, **arc motion paths**, and **robust** ID-based recognition of numeric **differences between keyframes**.

---

## Build

Requires C++17 compiler.

```bash
g++ -std=c++17 -O2 -o svg_animation_tool svg_animation_tool.cpp
```

---

## Quick start

Open Inkscape, draw a rectangle and triangle, save the drawing as `keyframe_start.svg`, move each object to a very different location, save that modified version as `keyframe_end.svg`, then close Inkscape.

Type this text into a file named `script_animation.txt`:

```
keyframe_start.svg
keyframe_end.svg
animate
```

Run it:

```bash
./svg_animation_tool script_animation.txt
```

This command writes 30 SVG frames to `frames_svg/frame_0000.svg` through
`frames_svg/frame_0029.svg`.

Within the `frames_svg` directory, open file `frame_0014.svg` in Inkscape to verify the objects are halfway between their starting and ending positions.

Instructions below explain how to batch-convert the SVG files into PNG files (using Inkscape in its command-line mode), and how to convert the PNG files into a video using a video editor.

---

## How it works

Each keyframe is an SVG file you create in Inkscape.

All SVG files referenced in a script must be in the **current working directory**. No path specifiers can precede the filename.  The filename in the script must include the `.svg` extension.

This tool reads the script-specified SVG files and does this processing:

- Compares pairs of keyframes by object `id`.

- Finds **numeric attribute values** that change between the two keyframes. These changes can include positions, sizes, rotations, opacity, etc.

- Generates new SVG files in which the numeric attributes for those objects **smoothly change** from frame to frame using **smootherstep** easing.

Each animated object must share the **same matching `id` name** between the two keyframe files. Objects that look the same, or similar, between keyframes are not recognized as the same if they have different `id` names.

---

## Tips

- The 'id' object name can be the default identifier assigned by Inkscape. These objects will be animated if they appear in both keyframes and have a numeric difference.

- The `id` names automatically match if one of the two keyframe files was copied from the other and then modified in Inkscape using the object's handles.

- Before you modify a group of objects (or one object) using the `transform` dialog box, first **create a new group**. This step creates an `id` name for this group. Then your keyframe files will have the same group `id` name.

- It can be useful to **supply your own `id` names** using Inkscape's `object properties` tool or `XML editor` tool. The output trace file includes additional information for objects that have `id` names that do not match Inkscape's default naming conventions (such as `rect1234`).

- If your animation has different drawings in **different layers**, and you reveal a different layer in each keyframe, use the `freeze` directive for each keyframe. The `animate` directive does not work with this kind of animation because Inkscape ensures an SVG file does not have more than one object with the same `id` name.

- Some animations may require hiding an object (or group of objects) in one keyframe and showing the object in the other keyframe. This technique requires that both objects must be **of the same type** and only differ by numeric values.

- Objects **can fade in**, or **fade out**, by changing their opacity between `0` (hidden) and `1` (fully visible). These objects must be inserted into a keyframe before they fade in, and cannot be deleted until after they have faded out.

- If an object does not have an **opacity** attribute, you may be able to add it using Inkscape's `XML editor`.

- Objects that move into view from off-screen, and later move off-screen, can be **inserted** and **deleted** while they are off-screen in other non-involved keyframes.

- At the **midpoint** in each animation segment, the non-animated objects switch from the keyframe **A** (*before*) version to the keyframe **B** (*after*) version. This switching can affect the SVG stacking order (which element appears on top of which) but does not interrupt the animation.

- If an object appears or disappears at the **midpoint** of an animation, probably the object is in one keyframe but not the other.

- When you are editing a keyframe in Inkscape and want to see both the object's **starting appearance** and **ending appearance** (such as overlapped, or in different positions), you can use Inkscape's `object properties` tool or `XML editor` tool to change their `id` names. The `id` of the starting version in one keyframe must match the `id` of the ending version in the other keyframe.

- A group of objects that have the same `id` name in two successive keyframe files can be animated as a group. Objects within these groups also can be animated if their `id` names match between the two keyframes. This combination allows zooming or panning at the same time that objects are moving relative to other objects in the group.

- `object-ids` directives (explained below) can appear multiple times in a script with different lists for different directives. Each directive (`arc-height`, `spread-out`, etc.) captures a snapshot of the most recent list at the time it appears.

- `arc-degrees` and `frames-per-step` directives (explained below) persist across `animate` calls until changed again.

- The `spread-out` stagger (explained below) automatically expands the frame count for that segment. The full frame count can be calulated as `frames_per_step + (delay × (n_objects - 1))`.

- The end of the summary file includes `Meld diff commands` that you can copy into a Linux shell script to conveniently compare changes between sequential keyframe SVG files. Also, at this time you can copy specific changes from one keyframe to another keyframe without needing to use Inkscape, and without needing to position or orient the objects to match, which is needed when those objects are not being animated. Using the Meld application also can help you troubleshoot animations that do not work as expected.

---

## Script reference

### Script syntax

The text in a script file consists of tokens that are **whitespace-separated**.

**Line breaks are ignored**, except as whitespace.

### Comments

A token of the pattern `whatever-begin` begins a process of accumulating
the next tokens for storage in a text container categorized by the `whatever` name,
where `whatever-begin` tokens can be text strings such as:

- `comment-begin` for general comments

- `not-yet-begin` to comment-out keyframes and animation directives not yet ready for testing

- `caption-begin` or `narration-begin` to hold narration words, which may later become captions

- `animation-reminder-begin` for reminders about what needs to happen at that time in the animation

Three or more hyphens, such as `---` or `----`, terminate the current `whatever-begin` text accumulation.

The output trace file contains the accumulated text within each named category.
For example, all the `caption-begin` words are joined together in the `[caption]` section.
The joined caption (or narration) category is useful while developing the narration script.


### Keyframe tokens

| Token | Description |
|---|---|
| `file.svg` | Specify the next keyframe SVG file. The two most recently specified keyframe files are used by the next `animate` directive. The `.svg` extension must be included. The `file` portion is expected to be named in a way that is meaningful for that part of the animation. |
| `animate` | Interpolate between the two most-recently specified keyframes, writing `frames_per_step` frames. Consecutive `animate` calls share their boundary frame. This directive is what activates animation processing. |
| `freeze N` | Write N identical copies of the most recent keyframe. |

### Object and motion directives

These directives modify animation paths or timing. They do not initiate animation. They take effect when the `animate` directive is encountered.

| Directive | Description |
|---|---|
| `object-ids` | Begin collecting element ids. Subsequent unrecognized tokens are added to the list until another directive is seen. These are the object ids that will be affected by the object and motion directives explained in this section. |
| `arc-height [N]` | Apply an upward arc to the Y movement of the most recent `object-ids`. Peak height is N percent of horizontal travel distance (default 30). Uses the current `arc-degrees` setting for ellipse shape. This is a path modifier; it does not initiate animation by itself. |
| `arc-degrees [N]` | Set the ellipse trim angle used by `arc-height` (default 20). Larger values produce a flatter arc peak. This is a path modifier; it does not initiate animation by itself. |
| `spread-out [N]` | Stagger the start and end times of the most recent `object-ids` by N frames per object (default 1). This is a timing modifier; it does not initiate animation by itself. It delays the sequence in which multiple objects start moving and end their movement. The default sequence, which can be changed as described below, matches what would be expected for objects moving from one or more starting piles to one or more ending piles. Some objects slow down while waiting for other objects lower in the pile to arrive. Under the default sequence, objects with lower Y values (which are higher on the screen) in keyframe A start moving first; objects with lower Y values (which are higher on the screen) in keyframe B finish last. |
| `spread-out-start-...-end-...` | This is a timing modifier that changes the `spread-out` behavior. The directive `spread-out-start-top-end-top` is the default described above. This directive does not initiate animation by itself. The horizontal versions of this directive `spread-out-start-left-end-left` and `spread-out-start-right-end-right` can be useful for animating vehicles moving horizontally from one stoplight to the next stoplight. For completeness the valid variations include: `spread-out-start-top-end-bottom`, `spread-out-start-bottom-end-top`, `spread-out-start-bottom-end-bottom`, `spread-out-start-left-end-right`, `spread-out-start-right-end-left`. A warning is produced if the start and end are not on the same axis, such as `spread-out-start-top-end-left`. This setting persists until changed. |

### Settings directives

| Directive | Description |
|---|---|
| `frames-per-step N` | Frames per `animate` segment. Must be a positive integer. The default is 30. It controls animation speed. Takes effect for subsequent `animate` calls. It can be changed prior to each `animate` directive. If the video frame rate is 30 frames per second, the default value causes each simple animation to last one second. Use of the `spread-out` directive extends the animation duration. |
| `output-directory D` | Directory to write frame files into. The default directory is `frames_svg`. Must not contain a period. Takes effect when the first `animate` or `freeze` directive is encountered, after which this directive has no effect. |

---

## Script example using multiple features

```
comment-begin Settings ----
frames-per-step 60
output-directory frames_svg

comment-begin Delay the start of animation ----
keyframe_a.svg
freeze 10

caption-begin Watch this simple slow motion ----
keyframe_b.svg
animate
freeze 5

caption-begin Now watch this faster curved movement of ballots ----
frames-per-step 30
object-ids
ballot1 ballot2 ballot3 ballo4
arc-degrees 20
arc-height 30
spread-out 2
keyframe_c.svg
animate
freeze 5
```

In this example, during the second `animate` segment, four ballots move with a 2-frame stagger between each ballot, and their motion follows a smooth up-then-down arc path.

---

## Output files

| File | Contents |
|---|---|
| `frames_svg/frame_NNNN.svg` | Output animation frames (zero-padded, sequential) |
| `output_trace_animate.txt` | Verbose trace: element registry, extracted values, change detection detail. Overwritten each run. |
| `output_summary_animate.txt` | Concise run summary: settings, animated ids, frame ranges, 'meld' comparison commands. Overwritten each run. |

Progress is printed to `stdout`. Errors (file not found, etc.) go to `stderr`.

---

## Converting frames to video

**Step 1 — Convert SVG frames to PNG using Inkscape:**

Include these Linux commands in the shell script that runs the animation.

```bash
mkdir -p frames_png
for f in frames_svg/frame_*.svg; do
    inkscape "$f" --export-type=png \
    --export-filename="frames_png/$(basename "${f%.svg}").png"
    printf "%s" "."
done
```


**Step 2 — Import into Shotcut (or any video editor):**

Open Shotcut, create a new project, and import the `frames_png/` folder as an
'image sequence'. Set the frame rate to match your target video (such as 30 fps).

If the animation is short, you can use ImageMagick (or equivalent software) to convert the PNG files into a short animated gif.

---


## License

MIT — Copyright (c) 2025 Richard Fobes at SolutionsCreative.com
