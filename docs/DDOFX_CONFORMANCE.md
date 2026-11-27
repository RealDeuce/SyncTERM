# DDO F/X 2.0 coverage

Select **DDO F/X 2.0** under **Graphics** in the BBS entry. The session starts in Lite mode. Plain text and ANSI sequences pass through to the selected emulation.

The inventory follows the 439 host-to-client command definitions in the 2.00 SDK. Line numbers refer to physical lines of `SDK20/docs/DDOFX.DOC` (SHA-256 `8ef566f37ee14cd4e7b349fccca23fd0d05cd5d63dda9f378ec7471a8b1c1e98`). The SDK also describes 45 UHS host commands.

Connected tests cover framing, detection, and ignore/hear across CP437 and UTF-8 sessions. Place MOD, S3M and CMF files in the entry’s `DDOFX/MUSIC` cache directory. The `m` command selects the decoder by file content and plays through SyncTERM’s stereo mixer. A missing file leaves the previous song playing. MOD and S3M use libopenmpt. Pattern stepping skips S3M separator entries in both directions. Advancing past the song’s stop marker or moving back past its first pattern selects its first playable pattern, including while paused. CMF 1.0 and 1.1 files with an embedded instrument bank use the OPL2 synthesizer and the file’s tick rate. The SDK query `MICMF` reports whether CMF is playing. Tests cover melodic and rhythm playback, loop timing, song replacement, and a connected BBS selecting a CMF by content. In Normal mode, `7DEFBAR` maps each of the first 16 module tracks to a directional volume bar. The bars use libopenmpt’s per-channel level and update at 20 Hz during idle connections and script waits. Disabling the bars freezes them. Clearing restores the exposed areas from the hidden graphics page. The most recent valid bar definition assigns the group to the desktop or the alerted window. Window bars use virtual-page coordinates and pause while that window is inactive.

After a ZMODEM batch finishes, SyncTERM installs up to 50 Lite/Normal command ZIPs into that BBS’s DDOFX cache. The package letter selects MUSIC (M), BBSTHEME (T or R), FONTS (F), SCRIPTS (S), or THEMES (P). Stored and deflated members retain their timestamps. After an automatic foreground download, SyncTERM closes the progress display as soon as the batch is processed so the door can immediately verify its resources. A successful install removes the downloaded command ZIP. Failed installs and ordinary downloads keep their files. In BBSTHEME packages, DDOFX.INI can add or delete resource members and request one RealPlay sample per batch. Component updates preserve the container timestamp and write the SDK lock marker. In Normal mode, `7ZMBKGRND,1` keeps the scene running during a ZMODEM download and shows progress on the bottom row through installation. The progress overlay leaves the underlying visual page available for animation. `7ZMSTEALTH,1` marks subsequently defined interactive controls for local use during reception. Their macros can run and update the scene while ZMODEM retains the connection. Return strings stay suppressed through the end of the batch. Graphic windows remain movable regardless of the stealth setting. The close callback follows the setting at its definition.

BBS sound themes, resident sound bites and big-sample profiles use signed 8-bit PCM from the entry’s `DDOFX/BBSTHEME` resources. `MTALK` assembles loose RealPlay files from named `.SND` members, supports the four pause lengths, and can append, replace or delete a phrase. RealPlay streams loose sample files from the same directory. `dACARD` reports the SDK’s 16-bit stereo capability `SB 16` when SyncTERM has audio output configured, and `NONE` otherwise. The mixer advances the samples at the requested frequency alongside MOD or S3M playback. A connected peer checks `dSOUNDON` and starts RealPlay from the cache. The test reads the resulting stereo frames from a pull-only audio device.

An online pair of `LIGMODE` commands enters Normal mode on a pixel display. In Lite mode, a local script that attempts the same transition reports an error and unwinds to the connection. In Normal mode the pair leaves the current scene intact. A local script can issue `2GEXIT` and continue writing to the restored Lite screen. The visible page is 640×480 with 256 palette entries. The work, background and hidden pages store pixel indices. Whole-page copies, rectangular cuts anchored at the lower left, transparent cuts, work-page drawing, vertical scrolling and palette changes are available from a connection. Each open text window has a 100-row virtual page, initially 80 columns wide. Ordinary text and ANSI from the connection or a resource script paint only within the active visible text window. Closing the last window restores the desktop. `2GEXIT` restores the Lite screen, cursor and palette. Basic fade-out saves the active palette, fades to black and clears the visual and hidden pages. Advanced fade-out preserves page pixels and blends an inclusive palette range toward a selected register. Fade-in returns the palette to the saved values. Windows retain their virtual text pages and cursor positions across hiding, switching, swapping and changes in placement. Switching between separate windows updates the title and help colours. If visible text windows overlap on the same page, switching restores that page from its hidden image and draws the windows in slot order with the active one on top. Hiding, showing, closing, moving or resizing a window also restores its page. Loading or copying an image to the visible page draws its text windows over the new pixels. Bordered windows use the selected VGA font for their double-line frame and centered title and help. Each window retains its own viewport origin and can follow the cursor independently on each axis. When the window enables manual scrolling, Ctrl+Home and Ctrl+End move one column and Ctrl+PgUp and Ctrl+PgDn move one row. `1SETC` changes the wrap width for open and future windows. An existing window narrows to fit the new limit. At one or two columns a framed window drops its border to leave room for text. Reset restores 80 columns.

With a graphic window alerted, `2aSCROLL` scrolls its visual or hidden virtual page in window coordinates. Visual scrolls update the part inside the viewport. Work and background pages can be scrolled once the door clears the alert.

In Normal mode, `2aCHORG` shifts the displayed page left and up. Pixels wrap horizontally and newly exposed rows are black. Lasting page contents and drawing coordinates stay at their original positions. `2aSHAKE` animates that origin in the background and returns it to zero.

A door can create ten graphic windows with independent visual and hidden pages. The active window paints above the other visible windows. An alert window receives primitive drawing commands in virtual-page coordinates. Its viewport can scroll, zoom, refresh and restore. PCX loads and hidden-to-visual copies publish to the viewport immediately. Copying from the work or background page to the desktop visual page redraws visible graphic windows above the image and desktop text. Rectangular cuts from those pages, transparent cuts to the visual page, desktop PCX loads and desktop snapshot restores also redraw the windows. An opaque desktop hidden-to-visual copy or cut restores the image directly and preserves the hidden source. A copy from the visual page reads the displayed windows. Closing a window reveals the remaining windows and the desktop hidden page. A user can raise a window, drag or resize its frame, zoom, restore and scroll its virtual pages. Close-button clicks and programmed keys execute the configured macro and send its return. A macro can explicitly close the window. An animation can serve as a draggable desktop or window icon. Doors can query its bounds, restore its creation position and associate open, close and drop procedures. A hosted window’s return-to-icon button runs the close procedure.

In Normal mode, `1SCALE` sets a common cell width of 2–8 pixels and height of 2–16 pixels. Font scaling preserves each window’s logical page dimensions. Direct graphics text, text fields, list boxes, marquees and digital clocks take their glyphs from the lower left of each 8×16 font record. The dimensions determine the space each character occupies. The font pixels keep their native size. A scene reset retains the current dimensions.

Sprites can be captured from any of the four graphics pages. The SDK repeats the background spelling for hidden-page capture. SyncTERM accepts `2SH` for the hidden page. Simple sprite definitions and animation frames share the SDK stack. Doors can reserve animation slots and append ordinary sprites above them. If clearing ordinary sprites leaves a hole below an animation, the next draw reports an error and returns to Lite. Clearing the entire stack removes the pending error. `2DSS` restores the buffered regions from the background, draws the current frame in stack order, then publishes the regions to the visual page.

Animation profiles draw in play order within their owning desktop or graphic window. Window profiles advance while their window is active. A moving sprite updates its virtual page and publishes the part inside the viewport. Desktop animations remain behind visible windows. Collision boxes match profiles in the same coordinate space. A door can play a sprite range once, loop or oscillate it, or select frames at knots along chained cubic Bézier paths. Individual `2STOPA` removes the image and retains the current frame for resuming, as specified by the SDK. If a sprite range contains frames of different dimensions, defining the profile or changing a playing profile reports an error and restores Lite mode. Frame and path samples can play together. Collision boxes test the full sprite rectangle, including offscreen portions, and can report the animation number to a macro before sending their return string. If that macro stops the colliding animation, directly or through a nested script, DDO F/X reports an error, ends the script and returns to Lite mode.

Polygon objects can load triangle meshes from the current M resource or build them face by face. Each object has its own camera, base colours and solid or wireframe setting. Desktop playback draws on the work page, publishes to the visual page and restores the old footprint from the background page. Initializing an object in an alerted graphic window binds it to that window. While the window has focus, the object draws on its visual page and restores its old footprint from the window’s hidden page. Each owner presents only the changed pixels visible through its viewport. Morphing changes geometry and face colours during playback, including when the target has a different number of faces or vertices. The connected tests use known camera coordinates and check both display planes.

Work, background and visual pages can load version-5, single-plane, 8-bit PCX artwork up to 640×480. Loading artwork installs its palette across all pages. Each page can save a session-owned PCX file and restore it to any of the three pages. The files survive a return to Lite mode and close at disconnect.

Lite and Normal accept complete 4,096-byte VGA fonts from `DDOFX/FONTS` with lowercase `f`. The selected font applies to console text and all four desktop `2XT` destinations and an alerted window’s visual and hidden pages. `dGETFONT` reports its filename. SyncTERM supplies its built-in VGA font for `IBMSTD.FNT` when that file is absent.

Lite uses the SyncTERM status row for its full-screen commands. `0SETFUL,1` shows the row and `0SETFUL,2` releases it for terminal output. Switching clears the text area and homes the cursor. `0RSETFUL` and disconnect restore the layout in effect before the first switch.

Lite slides and bounces move the displayed text page vertically. ANSI output, cursor reports and scrollback continue to use the original coordinates while the page is out of sight. Bitmap backends display scanline offsets and text backends use whole rows. Moving down works from either starting position. A bounce makes successively smaller rebounds and can be interrupted with a key. Normal mode ignores all three commands.

Uppercase `Tfilename` loads terminal effects from `DDOFX/THEMES`. Each BBS starts with `DEFAULT.THE` when present. The last successfully selected filename is saved in that BBS’s cache and used on later connections. `TNONE` releases the current effects. Entering Normal mode with less than 2 MiB available also unloads the terminal pack. A door can reload it with `Tfilename`. Lowercase `tfilename` still controls the separate BBS sample bank. The SDK assigns terminal samples 20, 19 and 15 to slide up, slide down and bounce impact respectively. Playback requires enabled sound and the corresponding sample in the selected pack.

Lite accepts 4,000-byte RAW ANSI screens from the assigned M resource. `!filename` places the 80-column VGA character/attribute image starting on physical row 2 and clips it to the terminal area. Ordinary text resumes at its existing cursor. `0TFADEOUT` and `0TFADEIN` fade the 256-entry text palette over the SDK’s requested duration in milliseconds. The BBS can build the next page while the palette is black.

A BBS can load legacy and four-bank BMF fonts into 32 slots with `2F` and switch between loaded fonts with `2FSELECT`. On the selected desktop or window page, `2X` scales a variable-width string to the specified rectangle, draws an optional one-pixel outline, and rotates it about the rectangle center. A visual bitmap print refreshes its area in the window. If the scratch image allocation fails, the current VGA font renders inside a colour-10 box.

Desktop pages and the alerted window’s virtual pages support lower-left anchored image scaling, rotation between pages with a transparent index and independent pivots, palette-based anti-aliasing, and single-index outlines. Direct VGA text and image transforms on the visual VGP become visible on refresh. Explicit work and background page destinations remain on the desktop.

In Normal mode the BBS can show or hide the mouse pointer, query its position and three logical buttons, and read separate press and release counts and positions for each button. The BBS can position the DDO pointer with `3MMOV` and confine it to a rectangle with `3MLIM`. SyncTERM accepts both the SDK spelling `MSPD` and the mouse-family spelling `3MSPD`. Relative to the default rates of 8 horizontally and 16 vertically, doubling an axis rate halves its travel. Motion is measured in desktop raster pixels, with fractional movement carried between samples. Up to 300 desktop regions share one table. Overlapping regions run in slot order. Each macro finishes before its return string is sent. Held repeatable regions run at 50 ms intervals during idle input. Non-repeating regions wait for all buttons to be released.

In Lite and Normal, `EEDTEXT` opens a full-screen editor for a CP437 message of up to 65,532 bytes in the entry’s BBSTHEME directory. A new message starts empty. F2 saves and exits, and Escape discards edits and exits. Both send the door’s completion string. Existing line endings are preserved, and Enter inserts a DOS CRLF. The door can then read the saved bytes with `ERTEXT`.

`EPTEXT` can export a BBSTHEME file in both modes. The door supplies a suggested DOS filename and the user selects the local destination in Save As. The output starts with the SDK’s single space, followed by the complete file, including binary printer data. Success, cancellation, a missing source and an unavailable device all send the specified completion string. SyncTERM currently treats DOS printer and serial ports as unavailable.

In Lite and Normal, `~INFSAV` and `~INFLOD` use the assigned resource prefix plus `T` in the entry’s BBSTHEME directory. Slot numbers and signed 32-bit passwords follow the SDK. A successful load sends the saved bytes verbatim, including embedded control bytes. Variable capture keeps the first 50 bytes and preserves the destination variable’s type.

In Normal mode, `1OPEN` attaches its text page to the currently alerted graphic window or to the desktop. Characters, the border and both labels use that owner's coordinates. Scrolling the graphic window reveals text drawn outside its original viewport. Changing the alert target leaves subsequent text and VTP scrolling on the original owner. A graphic window covers desktop text while the text continues to update underneath. Moving or closing the graphic window reveals the current text and its blink state. Hiding, moving or closing the text window restores the exposed area from its owner's hidden page.

A marquee reads up to 20,000 bytes from the assigned M resource and uses the selected VGA text font. `5SHOWMQ` paints the initial strip on the work page. Its timer scrolls the strip one pixel to the right per frame on the visual page. A marquee defined in an alerted graphic window scrolls on that window’s virtual page while it is active. The viewport clips the strip. Opaque sprite pixels cover the scrolling text and transparent pixels reveal it. `5STOPMQ` preserves the current position, and `5KILLMQ` releases the slot.

A BBS can open up to 300 indexed FLI/FLC profiles from the entry’s BBSTHEME directory. Each opens paused and plays the requested frame count once or continuously. The host can retain the desktop palette or apply the movie’s palette. Frames draw at the specified upper-left corner. A desktop movie continues behind graphic windows. A movie opened in an alerted window plays in its virtual page while the window is active. Sprite animations retain the latest movie frame underneath them, and desktop clocks and marquees remain visible over the movie. `2RESA` changes the rate of running and subsequently opened movies.

Analog and text clocks have 30 slots each. A BBS defines each clock before showing it on the visual page. The hands and the eight-character digital display use local time with signed hour, minute and second offsets. A clock defined with a graphic window alerted uses that window’s virtual coordinates. It runs while its window is active. Scrolling reveals the corresponding part of the clock, and open windows cover desktop clocks. Each text clock loads its own VGA font, defaulting to IBMSTD.FNT. Active marquees draw over clocks.

The BBS can create up to 300 push buttons with `5CPUSHB`. Creation draws the up sprite on the desktop work page or in the alert window. Window buttons keep their owner when a later command changes the alert window. A matching key or mouse combination draws the down sprite on the visual page for three clock ticks, restores the up sprite, then runs the macro and sends the return string. The pass flag controls whether a keyboard activation continues to the next binding and the connection.

Radio buttons and checkboxes each have 300 slots shared by the desktop and graphic windows. A key or left click changes the selection. An explicit execute command runs the selected members of the addressed exclusive or multiple-selection group in slot order. Each macro finishes before its return string is sent.

List boxes hold up to 100 items in each of 300 slots. A list defined in an alerted graphic window retains that window’s coordinates for navigation, selection and display. The BBS can prepare a list on the work page, show it on the visual page, scroll by line or page, move the highlight, and toggle selections. A left click moves the highlight, and a double-click toggles the item. Execution visits selected items in list order. For `5LBADJLST`, positive movement goes toward the end, following the SDK’s direction parameter description.

A BBS can define 250 single-line text fields on the desktop or in graphic windows. Creation and `5SHOWTFLD` paint the owner’s virtual page when its window has focus and the desktop work page otherwise. Selecting a field by command, key or mouse edits it on the visual page. Mouse hits track the owner’s viewport and stacking order. Keyboard bindings remain global. Fields support masked input, letter and digit filters, linked navigation and grouped submission in creation order. Each field’s macro finishes before its return is sent. The two `~GTXTFLD` commands take the variable number first and field number second, following the SDK. They preserve the variable’s type.

Desktop and graphic-window hover fields share a table of 300 slots. Graphic-window fields and regions use virtual-page coordinates and retain their slot association across window recreation. The highest matching slot runs its macro, sends its return string and selects its static or animated pointer. As specified in the SDK, leaving a field for uncovered space preserves the remembered slot. Re-entering it fires again after a different field has won or the BBS has sent `3MFFRS`.

The host can load either MPT pointer format from the assigned M resource. A selected image keeps its own hotspot and AND/XOR masks when the BBS replaces or clears its list entry. The bitmap presenter applies those masks to the displayed palette indices, leaving the graphics pages available for capture and copying under the pointer. The 100 static slots and 100 animation profiles support one-shot, looping and oscillating playback.

The raycaster renders a 244 × 64 world with textured walls, floors, ceilings and three transparent object layers. The BBS can generate a seeded maze, edit individual cells or import the two map sections specified by the SDK’s DEFMAP.TXT from the assigned M resource. The overview uses the visual page alerted at the time of the dump. Later annotations and player marker moves use that same page. Starting the engine binds its viewport to the alerted window or the desktop. A window viewport advances while its owner has focus. A new maze starts the player at (5,5) and returns a reachable endpoint. Map queries report the SDK texture numbers. The initial textures are generated locally. Hosts can replace them with copies of 32 × 32 sprites or animate them from a live range of sprite slots. Movement checks walls and object characteristics, and an interactive encounter stops rendering and reports the object coordinates to the host or capture variable.

Raycaster cells can share any of 3,600 profiles. Instant profiles run on cell entry. Action profiles can require a DOS key, a mouse or joystick button combination, and a facing direction. Each link runs its macro and sends its return before selecting the next link. A macro can change that link or delete the profile. Ordinary keys reach the host before the matching profile's return.

`5SAVE3D` and `5LOAD3D` exchange 1,141,178-byte loose MAP files in BBSTHEME. Loading restores the map, object layers, texture atlases, animation definitions, cell profiles, viewport and heading. The SDK specifies that the player keeps the current X and Y when switching environments. The file format has one collision characteristic per cell, shared by its saved layers. A saved animation interval uses whole centiseconds. A paused texture keeps its saved atlas image. An active texture resumes from the recorded sprite slot using the frames currently in the separate sprite bank.

The raycaster records changed camera positions and headings as six-byte REC records in BBSTHEME. Playback applies one recorded pose per frame through the final record. A recording played from the assigned M resource is limited to 60,000 bytes. Its extracted file is removed on completion, replacement or reset.

The Normal-mode landscape accepts a seed or a 255 × 255 indexed map from the visual or work page. The lower-left map pixel is world coordinate (0,0). The camera has 256 positions on each axis, so an imported map wraps its extra row and column to the opposite edge. The BBS can set or adjust the camera, query its position and direction, and fly over the terrain in a 320 × 200 viewport. Clouds have their own placement. The last frame stays visible when flight stops. Creating the landscape binds it to the current graphic window or desktop. Flight pauses while its owning window is inactive. Sprites can animate over the terrain. The map commands use the currently alerted visual page or the shared work page.

Normal mode supports a tile map of up to 150 × 150 cells. The host can fill either layer, move the viewport, move or remove foreground tiles, and animate consecutive sprite frames. Grid assignments become visible on a draw. Movement redraws immediately. A grid created in an alerted window retains that window as its owner. Its tiles animate while the owner is active, and removing the grid restores the underlying page.

Normal mode starts a ten-minute inactivity timer. `5SLEEPER,minutes` replaces and restarts it. A keystroke, mouse button press or mouse field activation rearms it. A local window counts down the final three minutes over the paused scene. A dismissing key restores the scene and starts the interval again. For short positive intervals the countdown starts after one second of inactivity. Expiry closes the connection. Zero requests immediate expiry. A Normal reset restores ten minutes, and Lite mode has no inactivity timer.

In Normal mode, `5PING,minutes,string` sends up to ten decoded bytes to the host at the configured interval, independently of keyboard activity and variable capture. A zero interval or Normal reset disables it. Lite mode and background file transfers suspend transmission. Resuming an overdue timer sends one ping and starts a fresh interval.

In Lite and Normal mode, `dDRIVE` reports free space available to the user on the BBS asset-cache volume. `dMEM` estimates available memory from the OS. Both report bytes up to the largest signed DDO script integer (2,147,483,647), and return −1 if the measurement is unavailable. When a positive `dVMEM` request exceeds measured memory, SyncTERM reports the shortfall through the connection’s alert handler, ends local script execution, and returns to text mode. It continues receiving host input. `dSPEED` reports a numeric processing-speed estimate taken once per process. SyncTERM samples the string, integer and floating-point workload of the SDK-era MPORT diagnostic over a short interval. The index is an estimate across architectures.

Doors can supply a resource prefix, member or loose filename through a system variable. For example, after `~DEFVAR,62,1,Sample.bin`, the query `dmDEFVAR62` reads the metadata for `Sample.bin` in MUSIC. Names such as `DEFVAR.FNT` are literal filenames.

Loose-file queries read the entry’s `DDOFX/MUSIC`, `DDOFX/FONTS`, and `DDOFX/BBSTHEME` directories. Professional queries use `DDOFX/TERMS/<client>`. Names use DOS-style case matching. The timestamp is local modification time with DOS two-second precision. `8EXEC` consumes the command without starting a program.

In Lite and Normal mode, `EWTEXT` creates or replaces a loose BBSTHEME file with flag 0 and appends with flag 1. It adds the carriage return specified by the SDK to each line. `ERTEXT` sends the file bytes followed by the supplied return string. A missing file still sends the return string.

Resource assignment selects the M container in `DDOFX/BBSTHEME` for local scripts and bitmap fonts and the S container for indexed PCX artwork. Scripts up to 40,000 bytes can call other scripts and render DOS text. A resource entry query tests the member name and component ID. Conditional scripts can also read that ID.

In Normal mode, `2STORES` saves the current sprite slots as paired DAT and RAW files in the entry’s BBSTHEME directory. `2LOADS` validates both files before replacing the live slots. Its erase flag removes the pair after a successful load. A bank preserves slot order, indexed pixel rows and opaque allocation tails.

In Lite and Normal mode, `2RESET` unlocks the keyboard and clears key bindings, macro definitions, variables, return strings, query capture, joystick bindings, timed events and big-sample profiles. The current script continues after the command. In Normal mode it also clears all four graphics pages and releases sprites, simple animations, bitmap fonts and music bars. The assigned resource prefix, music, theme sounds, palette and saved page files survive a scene reset.

In Lite and Normal, `4MKEY` binds DOS scan/character pairs or the all-keys wildcard. Matching slots run in order, with each macro completing before its return bytes and any passed keystroke. The DDO key lock admits programmed bindings and consumes ordinary keyboard input. SyncTERM processes its local shortcuts before graphics input.

Lite and Normal expose two DOS joystick slots through SyncTERM’s controller mappings. The left stick, first hat and D-pad provide eight directions. The south/east buttons or joystick trigger/thumb supply buttons 1 and 2. Analog movement has a dead zone of one quarter of the axis range from center to an endpoint. The host can detect and calibrate each joystick, bind directions for each button combination, bind the three centered button combinations, and activate or clear each slot. Held actions repeat.

Lite scripts can define 32 ten-line macros, 300 return strings, and 1,000 typed variables. Local macro calls, repeats and waits preserve the order of the wire stream. Variable capture redirects automatic query results. Configured return strings can send binary bytes. Conditional macros compare literal text, typed user variables and the SDK’s predefined master operands. These cover identity, memory, storage, speed, graphics and audio capabilities, the mouse, the current music, theme and font, loose-file metadata and resource member IDs. `~REPVAR` runs its body before testing, including when the body defines the variable being tested.

| SDK line | Definition | Domain | Coverage |
| ---: | --- | --- | --- |
| 575 | LOAD FONT | DDO-FX LITE | Connected |
| 616 | LOAD MUSIC | DDO-FX LITE | MOD, S3M, CMF with embedded instruments |
| 635 | LOAD BBS THEME | DDO-FX LITE | Connected |
| 693 | LOAD SOUND BITES LIBRARY | DDO-FX LITE | Connected |
| 767 | LOAD TERM THEME | DDO-FX LITE | Connected |
| 828 | PLAY SAMPLE | DDO-FX LITE | Connected |
| 858 | PLAY SOUND BITE SAMPLE | DDO-FX LITE | Connected |
| 868 | PLAY SAMPLE At FREQUENCY | DDO-FX LITE | Connected |
| 882 | PLAY SOUND BITE SAMPLE At FREQUENCY | DDO-FX LITE | Connected |
| 894 | LOAD BIG SAMPLE | DDO-LITE | Connected |
| 993 | DEFINE BIG SAMPLE | DDO-LITE | Connected |
| 1055 | ADD BIG SAMPLE PART | DDO-LITE | Connected |
| 1086 | UNLOAD BIG SAMPLE | DDO-LITE | Connected |
| 1102 | PLAY BIG SAMPLE | DDO-LITE | Connected |
| 1124 | CHECK BIG SAMPLE | DDO-LITE | Connected |
| 1167 | PLAY REALPLAY SAMPLE | DDO-FX LITE | Connected |
| 1207 | TEXT TO SPEECH | DDO-FX LITE | Connected |
| 1417 | MODIFY MUSIC PATTERN | DDO-FX LITE | Connected |
| 1447 | PAUSE RESUME MOD/S3M | DDO-FX LITE | Connected |
| 1459 | SET VOLUME | DDO-FX LITE | Connected |
| 1476 | MODIFY VOLUME | DDO-FX LITE | Connected |
| 1491 | GET VOLUME | DDO-FX LITE | Connected |
| 1509 | IS CMF PLAYING | DDO-FX LITE | Connected |
| 1528 | WAIT | DDO-FX LITE | Connected |
| 1544 | GOTO DDO-FX (NORMAL MODE) | DDO-FX LITE | Connected |
| 1573 | DETECT LITERM | DDO-FX LITE | Connected |
| 1611 | DETECT USER MEMORY | DDO-FX LITE | Connected |
| 1634 | DETECT USER MEMORY & REACT | DDO-FX LITE | Connected |
| 1729 | DETECT USER DRIVE SIZE | DDO-FX LITE | Connected |
| 1755 | DETECT GRAPHICS ADAPTER TYPE | DDO-FX LITE | Connected |
| 1770 | DETECT AUDIO CARD | DDO-FX LITE | Connected |
| 1798 | DETECT Machine Speed | DDO-FX LITE | Connected |
| 1851 | DETECT VIDEO MODE | DDO-FX LITE | Connected |
| 1880 | DETECT FOR MUSIC FILE | DDO-FX LITE | Connected |
| 1921 | DETECT FOR THEME FILE | DDO-FX LITE | Connected |
| 1929 | ASSIGN RESOURCE FILE | DDO-FX LITE | Connected |
| 2249 | DETECT FOR RESOURCE FILE | DDO-FX LITE | Connected |
| 2262 | DETECT FOR RESOURCE ENTRY | DDO-FX LITE | Connected |
| 2320 | DETECT FOR FONT FILE | DDO-FX LITE | Connected |
| 2328 | DETECT MUSIC ON/OFF | DDO-FX LITE | Connected |
| 2345 | DETECT SOUND F/X ON/OFF | DDO-FX LITE | Connected |
| 2367 | DETECT MUSIC PLAYING | DDO-FX LITE | Connected |
| 2389 | DETECT THEME LOADED | DDO-FX LITE | Connected |
| 2411 | DETECT FONT LOADED | DDO-FX LITE | Connected |
| 2431 | DEFINE A MACRO | DDO-FX LITE | Connected |
| 2590 | CLEAR A MACRO | DDO-FX LITE | Connected |
| 2602 | RUN A MACRO | DDO-FX LITE | Connected |
| 2615 | EXECUTE A MACRO LINE | DDO-FX LITE | Connected |
| 2662 | CREATE KEY MACRO | DDO-FX LITE | Connected |
| 2874 | TEST FOR JOYSTICK | DDO-LITE | Connected |
| 2893 | CALIBRATE JOYSTICK | DDO-LITE | Connected |
| 2913 | DEFINE JOYSTICK | DDO-LITE | Connected |
| 3067 | CREATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3161 | ACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3174 | DEACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3187 | DEFINE RETURN STRING | DDO-LITE | Connected |
| 3226 | SEND RETURN STRING | DDO-LITE | Connected |
| 3237 | TEXT FADE OUT | DDO-FX LITE | Connected |
| 3264 | TEXT FADE IN | DDO-FX LITE | Connected |
| 3273 | SLIDE SCREEN UP | DDO-FX LITE | Connected |
| 3294 | SLIDE SCREEN DOWN | DDO-FX LITE | Connected |
| 3307 | BOUNCE SCREEN DOWN | DDO-FX LITE | Connected |
| 3323 | DISPLAY RAW ANSI | DDO-FX LITE | Connected |
| 3365 | SET FULL SCREEN MODE | DDO-FX LITE | Connected |
| 3400 | RESTORE FULL SCREEN MODE | DDO-FX LITE | Connected |
| 3416 | RUN LOCAL SCRIPT | DDO-FX LITE | Connected |
| 3501 | DEFINE SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3604 | LOCK KEYS | DDO-FX LITE | Connected |
| 3650 | UNLOCK KEYS | DDO-FX LITE | Connected |
| 3661 | PAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events and background graphics, including clocks, marquees, sprite animations, tile maps, polygon objects and the raycaster |
| 3710 | UNPAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events and background graphics, including clocks, marquees, sprite animations, tile maps, polygon objects and the raycaster |
| 3720 | IGNORE COMMANDS | DDO-FX LITE | Connected |
| 3767 | HEAR COMMANDS | DDO-FX LITE | Connected |
| 3782 | ALL CAPS SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3796 | GET TEXT FIELD INFO FULL | DDO-FX LITE | Connected on the desktop and in graphic windows |
| 3824 | GET TEXT FIELD INFO | DDO-FX LITE | Connected on the desktop and in graphic windows |
| 3841 | INCREMENT SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3855 | DECREMENT SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3870 | RANDOM SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3893 | VARIABLE COPY | DDO-FX LITE | Connected |
| 3941 | VARIABLE INSERT | DDO-FX LITE | Connected |
| 3989 | PAD VARIABLE LEFT | DDO-FX LITE | Connected |
| 4041 | PAD VARIABLE RIGHT | DDO-FX LITE | Connected |
| 4063 | VARIABLE DELETE | DDO-FX LITE | Connected |
| 4096 | VARIABLE POSITION | DDO-FX LITE | Connected |
| 4132 | VARIABLE LENGTH | DDO-FX LITE | Connected |
| 4157 | VARIABLE EQUAL | DDO-FX LITE | Connected |
| 4186 | VARIABLE TYPING | DDO-FX LITE | Connected |
| 4209 | VARIABLE ADD | DDO-FX LITE | Connected |
| 4224 | VARIABLE SUBTRACT | DDO-FX LITE | Connected |
| 4239 | VARIABLE MULTIPLY | DDO-FX LITE | Connected |
| 4254 | VARIABLE DIVIDE | DDO-FX LITE | Connected |
| 4270 | VARIABLE CAPTURE | DDO-FX LITE | Connected |
| 4322 | IF VAR DO MACRO | DDO-FX LITE | Connected for literals, typed variables and all SDK master operands |
| 4547 | REPEAT MACRO UNTIL VAR | DDO-FX LITE | Connected for literals, typed variables and all SDK master operands |
| 4604 | REPEAT MACRO | DDO-FX LITE | Connected |
| 4654 | INFO SAVE | DDO-FX LITE | Connected |
| 4728 | INFO LOAD | DDO-FX LITE | Connected |
| 4777 | LAUNCH TEXT EDITOR | DDO-FX LITE | Connected |
| 4821 | RETRIEVE TEXT FILE | DDO-FX LITE | Connected |
| 4847 | WRITE TEXT FILE | DDO-FX LITE | Connected |
| 4886 | PRINT TEXT FILE | DDO-FX LITE | Connected for file output and unavailable-device completion. Printer and serial output unavailable |
| 4940 | LABEL | DDO-FX LITE | Connected |
| 4955 | RESET ENVIRONMENT | DDO-FX LITE | Connected for implemented scene objects |
| 5436 | DETECT MOUSE | DDO-FX (NM) | Connected |
| 5457 | OPEN GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5780 | GOTO CURSOR | DDO-FX (NM) | Desktop and attached text windows |
| 5819 | CLOSE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5855 | HIDE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5866 | UNHIDE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5874 | ACTIVATE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5884 | SWAP WINDOWS | DDO-FX (NM) | Desktop and attached text windows |
| 5924 | POSITION VTP ORIGIN | DDO-FX (NM) | Desktop and attached text windows |
| 5956 | POSITION VTP ORIGIN RELATIVE | DDO-FX (NM) | Desktop and attached text windows |
| 5982 | GETPOS OF GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 6006 | CHANGE GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 6035 | SCALE GRAPHIC TEXT FONTS | DDO-FX (NM) | Desktop and attached text windows |
| 6100 | SET GRAPHIC TEXT WINDOW COLUMNS | DDO-FX (NM) | Desktop and attached text windows |
| 6131 | LOAD PCX TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6163 | LOAD PCX TO WORK PAGE | DDO-FX (NM) | Connected |
| 6179 | LOAD PCX TO VISUAL PAGE | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6199 | LOAD A BITMAP FONT | DDO-FX (NM) | Connected |
| 6237 | BITMAP FONT SELECT | DDO-FX (NM) | Connected |
| 6246 | COPY BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6254 | COPY BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6261 | COPY WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6268 | COPY WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6275 | COPY VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6282 | COPY VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6289 | CUT BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6320 | CUT BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6328 | CUT WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6336 | CUT WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6344 | CUT HIDDEN PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6355 | CUT HIDDEN PAGE TO WORK PAGE (T) | DDO-FX (NM) | Connected |
| 6366 | CUT HIDDEN PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Connected |
| 6377 | CUT WORK PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6388 | CUT WORK PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Connected |
| 6399 | CUT WORK PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Connected |
| 6410 | CUT BACKGROUND PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Connected |
| 6421 | CUT BACKGROUND PAGE TO WORK PAGE (T) | DDO-FX (NM) | Connected |
| 6432 | CUT BACKGROUND PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6443 | CUT VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6453 | CUT VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6463 | DEFINE TRANSPARENT COLORS | DDO-FX (NM) | Connected |
| 6514 | SAVE BACKGROUND PAGE TO PCX | DDO-FX (NM) | Connected |
| 6528 | SAVE WORK PAGE TO PCX | DDO-FX (NM) | Connected |
| 6536 | SAVE VISUAL PAGE TO PCX | DDO-FX (NM) | Connected |
| 6544 | LOAD VISUAL PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6556 | LOAD WORK PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6568 | LOAD BACKGROUND PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6580 | GET SPRITE | DDO-FX (NM) | Connected |
| 6669 | GET SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6679 | GET SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6689 | GET SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6699 | DRAW SPRITE | DDO-FX (NM) | Connected |
| 6725 | DRAW SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6733 | DRAW SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6741 | DRAW SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6749 | CLIP SPRITE | DDO-FX (NM) | Connected |
| 6762 | CLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6769 | CLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6776 | CLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6783 | FLIP SPRITE | DDO-FX (NM) | Connected |
| 6799 | FLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6806 | FLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6813 | FLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6820 | REVERSE SPRITE | DDO-FX (NM) | Connected |
| 6831 | REVERSE SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6838 | REVERSE SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6845 | REVERSE SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6852 | PUT SPRITE | DDO-FX (NM) | Connected |
| 6861 | PUT SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6868 | PUT SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6875 | PUT SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6882 | INVERT SPRITE | DDO-FX (NM) | Connected |
| 6895 | FREE ALL SPRITES | DDO-FX (NM) | Connected |
| 6904 | STORE ALL SPRITES | DDO-FX (NM) | Connected |
| 6919 | LOAD ALL SPRITES | DDO-FX (NM) | Connected |
| 6939 | DEFINE ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7151 | DEFINE ANIM SOUND | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7186 | PLAY ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7219 | STOP ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7235 | CHANGE ANIM PROFILE ON THE FLY | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7246 | ANIM COLLISION PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7339 | RESOLUTION ANIMATOR | DDO-FX (NM) | Connected for FLI/FLC and animation profiles |
| 7383 | DEFINE MOTION PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7466 | DEFINE MOTION FRAME CONTROL | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7534 | DEFINE SIMPLE SPRITE | DDO-FX (NM) | Connected |
| 7652 | DEFINE SIMPLE SPRITE ANIMATION | DDO-FX (NM) | Connected |
| 7733 | CLEAR SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7747 | CLEAR ALL SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7754 | DRAW SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7765 | COPY BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7824 | COPY HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 7831 | COPY WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7838 | COPY HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 7845 | COPY VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7852 | COPY HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 7859 | CUT BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7869 | CUT HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 7879 | CUT WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7889 | CUT HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 7899 | CUT VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7909 | CUT HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 7919 | DO PRINT | DDO-FX (NM) | Connected, including window pages and the low-memory text box |
| 8058 | DO PRINT TEXT FONT | DDO-FX (NM) | Connected on desktop and window pages |
| 8110 | DO PRINT TEXT FONT WP | DDO-FX (NM) | Connected on the desktop |
| 8118 | DO PRINT TEXT FONT BP | DDO-FX (NM) | Connected on the desktop |
| 8127 | DO PRINT TEXT FONT HP | DDO-FX (NM) | Connected on desktop and window pages |
| 8136 | SET COLOR | DDO-FX (NM) | Connected |
| 8146 | DRAW POINT | DDO-FX (NM) | Connected |
| 8156 | DRAW POINT XOR | DDO-FX (NM) | Connected |
| 8163 | ERASE PAGE | DDO-FX (NM) | Connected |
| 8172 | Get Pixel | DDO-FX (NM) | Connected |
| 8182 | MOVE GRAPHICS CURSOR | DDO-FX (NM) | Connected |
| 8192 | PAINT | DDO-FX (NM) | Connected |
| 8209 | SET RGB | DDO-FX (NM) | Connected |
| 8221 | GET RGB | DDO-FX (NM) | Connected |
| 8234 | STORE PALETTE | DDO-FX (NM) | Connected |
| 8245 | RESTORE PALETTE | DDO-FX (NM) | Connected |
| 8253 | ZERO PALETTE | DDO-FX (NM) | Connected |
| 8260 | FADE PALETTE OUT | DDO-FX (NM) | Connected |
| 8299 | FADE PALETTE OUT ADVANCED | DDO-FX (NM) | Connected |
| 8350 | FADE PALETTE IN | DDO-FX (NM) | Connected |
| 8361 | DRAW LINE RELATIVE | DDO-FX (NM) | Connected |
| 8374 | CHANGE VIEW PORT ORIGIN | DDO-FX (NM) | Connected |
| 8394 | SHAKE SCREEN | DDO-FX (NM) | Connected |
| 8404 | DRAW FILLED RECTANGLE | DDO-FX (NM) | Connected |
| 8420 | SET CLIPPING REGION | DDO-FX (NM) | Connected |
| 8439 | DRAW CIRCLE | DDO-FX (NM) | Connected |
| 8451 | DRAW FILLED CIRCLE | DDO-FX (NM) | Connected |
| 8463 | DRAW LINE | DDO-FX (NM) | Connected |
| 8474 | DRAW LINE XOR | DDO-FX (NM) | Connected |
| 8481 | DEFAULT 16 COLOR PALETTE | DDO-FX (NM) | Connected |
| 8492 | DRAW BOX | DDO-FX (NM) | Connected |
| 8508 | DRAW BOX XOR | DDO-FX (NM) | Connected |
| 8524 | EDIT BOX LINES | DDO-FX (NM) | Connected |
| 8533 | DRAW CLIPPED RECTANGLE | DDO-FX (NM) | Connected |
| 8548 | DRAW DASHED LINE | DDO-FX (NM) | Connected |
| 8564 | DRAW DASHED LINE RELATIVE | DDO-FX (NM) | Connected |
| 8580 | DRAW ELLIPSE | DDO-FX (NM) | Connected |
| 8592 | DRAW FILLED ELLIPSE | DDO-FX (NM) | Connected |
| 8599 | FILL PAGE | DDO-FX (NM) | Connected |
| 8607 | FLOOD | DDO-FX (NM) | Connected |
| 8616 | MOVEIT RELATIVE | DDO-FX (NM) | Connected |
| 8624 | DRAW BEZIER CURVE | DDO-FX (NM) | Connected |
| 8646 | DRAW BEZIER CURVE XOR | DDO-FX (NM) | Connected |
| 8656 | VERTICAL SCROLL | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 8694 | MOUSE ACTIVE | DDO-FX (NM) | Connected |
| 8713 | CHANGE MOUSE POINTER | DDO-FX (NM) | Connected |
| 8750 | LOAD MOUSE POINTER | DDO-FX (NM) | Connected |
| 8776 | SELECT MOUSE POINTER | DDO-FX (NM) | Connected |
| 8786 | ANIMATED MOUSE POINTER | DDO-FX (NM) | Connected |
| 8837 | ACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Connected |
| 8846 | DEACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Connected |
| 8855 | MOUSE VISIBILITY | DDO-FX (NM) | Connected |
| 8866 | MOUSE LIMITS | DDO-FX (NM) | Connected |
| 8875 | MOUSE MOVE | DDO-FX (NM) | Connected |
| 8890 | MOUSE INFO | DDO-FX (NM) | Connected |
| 8928 | MOUSE INFO OLD | DDO-FX (NM) | Connected |
| 8958 | MOUSE SPEED | DDO-FX (NM) | Connected |
| 8968 | MOUSE REGION | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9189 | MOUSE REGION NON REPEAT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9211 | MOUSE FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9292 | MOUSE FIELD SWAP | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9302 | MOUSE FIELD FRESHEN | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9317 | CREATE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9481 | EXECUTE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9548 | SWAP RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9560 | DISABLE/ENABLE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9576 | CREATE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9679 | EXECUTE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9688 | SWAP CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9697 | DISABLE/ENABLE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9706 | CREATE PUSH BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9792 | CREATE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9973 | SHOW TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9981 | ACTIVATE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9992 | EXECUTE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10008 | SWAP TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10016 | CREATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10143 | MODIFY LIST ITEM | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10197 | DRAW LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10211 | SHOW LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10224 | EXECUTE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10237 | KILL LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10248 | DEACTIVATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10262 | REACTIVATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10274 | ADJUST LIST | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10341 | ADJUST HIGHLIGHT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10378 | TOGGLE SELECTION | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10399 | CREATE MARQUEE | DDO-FX (NM) | Connected |
| 10471 | SHOW MARQUEE | DDO-FX (NM) | Connected |
| 10485 | STOP MARQUEE | DDO-FX (NM) | Connected |
| 10498 | KILL MARQUEE | DDO-FX (NM) | Connected |
| 10511 | OPEN FLI/FLC | DDO-FX (NM) | Connected |
| 10600 | PAUSE FLI/FLC | DDO-FX (NM) | Connected |
| 10620 | UNPAUSE FLI/FLC | DDO-FX (NM) | Connected |
| 10632 | STOP FLI/FLC | DDO-FX (NM) | Connected |
| 10648 | START ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10703 | SHOW ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10715 | STOP ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10725 | START TEXT CLOCK | DDO-FX (NM) | Connected |
| 10772 | PAUSE TEXT CLOCK | DDO-FX (NM) | Connected |
| 10782 | UNPAUSE TEXT CLOCK | DDO-FX (NM) | Connected |
| 10794 | KILL TEXT CLOCK | DDO-FX (NM) | Connected |
| 10806 | CREATE VIRTUAL LANDSCAPE | DDO-FX (NM) | Connected |
| 10890 | VR CLOUDS | DDO-FX (NM) | Connected |
| 10911 | VR SKY | DDO-FX (NM) | Connected |
| 10925 | VR LAND POSITION | DDO-FX (NM) | Connected |
| 10935 | VR CLOUD POSITION | DDO-FX (NM) | Connected |
| 10942 | VR DRAW MAP | DDO-FX (NM) | Connected |
| 10976 | VR DRAW MAP ON WORK PAGE | DDO-FX (NM) | Connected |
| 10984 | VR GET MAP | DDO-FX (NM) | Connected |
| 11005 | VR GET MAP ON WORK PAGE | DDO-FX (NM) | Connected |
| 11012 | VR CHANGE SPEED | DDO-FX (NM) | Connected |
| 11026 | VR CHANGE DIRECTION | DDO-FX (NM) | Connected |
| 11036 | VR CHANGE X | DDO-FX (NM) | Connected |
| 11046 | VR CHANGE Y | DDO-FX (NM) | Connected |
| 11056 | VR CHANGE Z | DDO-FX (NM) | Connected |
| 11066 | VR CHANGE DETAIL | DDO-FX (NM) | Connected |
| 11079 | VR MODIFY SPEED | DDO-FX (NM) | Connected |
| 11093 | VR MODIFY DIRECTION | DDO-FX (NM) | Connected |
| 11100 | VR MODIFY X | DDO-FX (NM) | Connected |
| 11107 | VR MODIFY Y | DDO-FX (NM) | Connected |
| 11114 | VR MODIFY Z | DDO-FX (NM) | Connected |
| 11121 | VR MODIFY DETAIL | DDO-FX (NM) | Connected |
| 11128 | VR GET USER X | DDO-FX (NM) | Connected |
| 11138 | VR GET USER Y | DDO-FX (NM) | Connected |
| 11148 | VR GET USER Z | DDO-FX (NM) | Connected |
| 11158 | VR GET USER DIRECTION | DDO-FX (NM) | Connected |
| 11167 | VR ANIMATE | DDO-FX (NM) | Connected |
| 11176 | RC 3D RAYCASTING ENGINE | DDO-FX (NM) | Connected |
| 11262 | RC 3D SCALE | DDO-FX (NM) | Connected |
| 11291 | RC 3D MAZE GENERATOR | DDO-FX (NM) | Connected |
| 11331 | RC LOAD 3D MAP DEFINITION | DDO-FX (NM) | Connected |
| 11351 | RC ROTATE PERSPECTIVE | DDO-FX (NM) | Connected |
| 11381 | RC MOVE 3D FORWARD/BACKWARDS | DDO-FX (NM) | Connected |
| 11409 | RC DUMP MAP | DDO-FX (NM) | Connected |
| 11440 | RC MODIFY DUMP MAP | DDO-FX (NM) | Connected |
| 11466 | RC CHANGE TEXTURE LIBRARY | DDO-FX (NM) | Connected |
| 11516 | RC CHANGE OBJ TEXTURE LIBRARY | DDO-FX (NM) | Connected |
| 11574 | RC SET USER X POSITION | DDO-FX (NM) | Connected |
| 11595 | RC SET USER Y POSITION | DDO-FX (NM) | Connected |
| 11613 | RC SET USER ANGLE | DDO-FX (NM) | Connected |
| 11635 | RC GET USER ANGLE | DDO-FX (NM) | Connected |
| 11657 | RC GET USER X POSITION | DDO-FX (NM) | Connected |
| 11671 | RC GET USER Y POSITION | DDO-FX (NM) | Connected |
| 11685 | RC DEFINE ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11761 | RC PLAY ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11776 | RC STOP ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11792 | RC CLEAR MAP | DDO-FX (NM) | Connected |
| 11811 | RC CHANGE MAP | DDO-FX (NM) | Connected |
| 11901 | RC CHANGE OBJECT MAP | DDO-FX (NM) | Connected |
| 12046 | RC GET MAP INFO | DDO-FX (NM) | Connected |
| 12101 | RC SET MAP PROFILE | DDO-FX (NM) | Connected |
| 12392 | RC COPY MAP PROFILE | DDO-FX (NM) | Connected |
| 12404 | RC FILL MAP PROFILE | DDO-FX (NM) | Connected |
| 12414 | RC DEL MAP PROFILE | DDO-FX (NM) | Connected |
| 12433 | RC SAVE 3D ENVIRONMENT | DDO-FX (NM) | Connected. Saves the native MAP layout, texture atlas and animation intervals |
| 12453 | RC LOAD 3D ENVIRONMENT | DDO-FX (NM) | Connected. Restores the environment, renders imported textures and resumes active sprite sequences |
| 12466 | RC RECORD 3D MOVEMENTS | DDO-FX (NM) | Connected |
| 12510 | RC PLAY 3D MOVEMENTS | DDO-FX (NM) | Connected |
| 12521 | RC PLAY 3D MOVEMENTS FROM RESOURCE | DDO-FX (NM) | Connected |
| 12539 | RC DEFINE OBJECTS LAYER | DDO-FX (NM) | Connected. The desktop and window raycaster views draw the three shared object layers in order |
| 12570 | 3D OBJECT INIT | DDO-FX (NM) | Connected |
| 12829 | 3D OBJECT INIT LITE | DDO-FX (NM) | Connected |
| 12844 | 3D DELETE OBJECT | DDO-FX (NM) | Connected |
| 12856 | 3D ADJUST YAW | DDO-FX (NM) | Connected |
| 12888 | 3D ADJUST ROLL | DDO-FX (NM) | Connected |
| 12920 | 3D ADJUST PITCH | DDO-FX (NM) | Connected |
| 12952 | 3D ADJUST LOCATION | DDO-FX (NM) | Connected |
| 12981 | 3D ADJUST DISTANCE ZOOM | DDO-FX (NM) | Connected |
| 13003 | 3D ADD FACE | DDO-FX (NM) | Connected |
| 13026 | 3D PAINT OBJECT | DDO-FX (NM) | Connected |
| 13059 | 3D RENDER TYPE | DDO-FX (NM) | Connected |
| 13075 | 3D SET OBJECT | DDO-FX (NM) | Connected |
| 13094 | 3D MERGE OBJECT | DDO-FX (NM) | Connected |
| 13111 | 3D MORPH OBJECT | DDO-FX (NM) | Connected |
| 13144 | 3D PLAY OBJECT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 13158 | GD DEFINE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13236 | GD CHANGE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13286 | GD FILL GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13322 | GD KILL GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13330 | GD DRAW GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13382 | GD POS GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13410 | GD MOVE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13432 | GD POS TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13451 | GD MOVE TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13473 | GD KILL TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13488 | SET SLEEP DISCONNECT | DDO-FX (NM) | Connected |
| 13544 | SET PING | DDO-FX (NM) | Connected |
| 13620 | ROTATE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13703 | SCALE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13819 | ANTI-ALIAS IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13917 | OUTLINE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13975 | ZMODEM BACKGROUND TRANSFERS | DDO-FX (NM) | Implemented |
| 14035 | ZMODEM STEALTH MODE | DDO-FX (NM) | Connected for all 12 SDK families. Transfer tests exercise mouse fields, icons, list boxes, text fields, radio buttons, checkboxes, push buttons, mouse regions, timers, joystick input, keyboard macros and window close callbacks |
| 14120 | DEFINE MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14292 | ACTIVATE/DEACTIVATE MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14308 | CLEAR MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14318 | EXIT TO TEXT MODE | DDO-FX (NM) | Connected |
| 14638 | CREATE GRAPHIC WINDOW | DDO-FX (NM) | Scripted creation, frames, pages and stacking connected. Frame controls and linked icons connected |
| 14837 | DEFINE CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14898 | CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14916 | LOAD PCX (X,Y) Graphic Window VP | DDO-FX (NM) | Connected |
| 14936 | FIND ACTIVE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14946 | IS GRAPHIC WINDOW HIDDEN | DDO-FX (NM) | Connected |
| 14954 | IS GRAPHIC WINDOW ACTIVE | DDO-FX (NM) | Connected |
| 14962 | IS GRAPHIC WINDOW CREATED | DDO-FX (NM) | Connected |
| 14970 | GET GRAPHIC WINDOW STATS | DDO-FX (NM) | Connected |
| 14980 | ACTIVATE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14995 | CHANGE GRAPHIC WINDOW | DDO-FX (NM) | Scripted geometry, visibility and title connected. Mouse drag, resize, zoom, restore and scroll enable flags connected. Return-to-icon connected |
| 15008 | CHANGE GRAPHIC WINDOW VGP REL | DDO-FX (NM) | Connected |
| 15024 | CHANGE GRAPHIC WINDOW VGP | DDO-FX (NM) | Connected |
| 15034 | ZOOM/SHRINK GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15042 | RESTORE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15049 | RESTORE ALL GRAPHIC WINDOWS | DDO-FX (NM) | Connected |
| 15056 | REFRESH ALL GRAPHIC WINDOWS | DDO-FX (NM) | Connected |
| 15062 | REFRESH GRAPHIC WINDOWS INTERIOR | DDO-FX (NM) | Connected |
| 15074 | ALERT GRAPHIC WINDOW | DDO-FX (NM) | Connected for primitives, PCX, visual/hidden page copies and scrolling, sprites, mouse fields and regions, push buttons, radio buttons, checkboxes, animations, collisions, icons, bitmap and VGA text, image transforms, tile grids, clocks, marquees, music bars, FLI/FLC playback, list boxes, editable text fields and graphic text windows |
| 15094 | CALM GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15101 | CREATE ICON | DDO-FX (NM) | Connected |
| 15135 | ICON MOUSE POINTER | DDO-FX (NM) | Connected |
| 15161 | DEFINE ICON OPEN PROCEDURE | DDO-FX (NM) | Connected |
| 15174 | DEFINE ICON CLOSE PROCEDURE | DDO-FX (NM) | Connected |
| 15185 | DEFINE ICON DROP PROCEDURE | DDO-FX (NM) | Connected |
| 15203 | EXECUTE OPEN ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15215 | EXECUTE CLOSE ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15227 | EXECUTE DROP ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15235 | KILL ICON | DDO-FX (NM) | Connected |
| 15243 | MOVE ICON | DDO-FX (NM) | Connected |
| 15249 | GET ICON STATS | DDO-FX (NM) | Connected |
| 15265 | RESTORE ICON | DDO-FX (NM) | Connected |
| 15273 | RESTORE ALL ICONS | DDO-FX (NM) | Connected |
| 15578 | CHECK FOR DIRECTORY | DDO-FX Pro. | Connected |
| 15594 | CHECK FOR FILE | DDO-FX Pro. | Connected |
| 15614 | EXECUTE CLIENT | DDO-FX Pro. | Consume without execution |
