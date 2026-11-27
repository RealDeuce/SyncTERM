# DDO F/X 2.0 coverage

Select **DDO F/X 2.0** under **Graphics** in the BBS entry. The session starts in Lite mode. Plain text and ANSI sequences pass through to the selected emulation.

The inventory follows the 439 host-to-client command definitions in the 2.00 SDK. Line numbers refer to physical lines of `SDK20/docs/DDOFX.DOC` (SHA-256 `8ef566f37ee14cd4e7b349fccca23fd0d05cd5d63dda9f378ec7471a8b1c1e98`). The SDK also describes 45 UHS host commands.

Connected tests cover framing, detection, and ignore/hear across CP437 and UTF-8 sessions. Place MOD, S3M and CMF files in the entry’s `DDOFX/MUSIC` cache directory. The `m` command selects the decoder by file content and plays through SyncTERM’s stereo mixer. Selecting the current filename again, including with different capitalization, preserves playback position and pause state. A missing file leaves the current song playing. After `mNONE`, selecting the same file starts it from the beginning. MOD and S3M use libopenmpt. Pattern stepping skips S3M separator entries in both directions. Advancing past the song’s stop marker or moving back past its first pattern selects its first playable pattern, including while paused. CMF 1.0 and 1.1 files with an embedded instrument bank use the OPL2 synthesizer and the file’s tick rate. The SDK query `MICMF` reports whether CMF is playing. Tests cover melodic and rhythm playback, loop timing, song replacement, and a connected BBS selecting a CMF by content. In Normal mode, `7DEFBAR` maps each of the first 16 module tracks to a directional volume bar. The bars use libopenmpt’s per-channel level and update at 20 Hz during idle connections and script waits. Disabling the bars freezes them. Clearing restores the exposed areas from the hidden graphics page. The most recent valid bar definition assigns the group to the desktop or the alerted window. Window bars use virtual-page coordinates and pause while that window is inactive.

After a ZMODEM batch finishes, SyncTERM installs up to 50 Lite/Normal command ZIPs into that BBS’s DDOFX cache. The package letter selects MUSIC (M), BBSTHEME (T or R), FONTS (F), SCRIPTS (S), or THEMES (P). Stored and deflated members retain their timestamps. After an automatic foreground download, SyncTERM closes the progress display as soon as the batch is processed so the door can immediately verify its resources. A successful install removes the downloaded command ZIP. Failed installs and ordinary downloads keep their files. In BBSTHEME packages, DDOFX.INI can add or delete resource members and request one RealPlay sample per batch. Component updates preserve the container timestamp and write the SDK lock marker. In Normal mode, `7ZMBKGRND,1` keeps the scene running during a ZMODEM download and shows progress on the bottom row through installation. The progress overlay leaves the underlying visual page available for animation. `7ZMSTEALTH,1` marks subsequently defined interactive controls for local use during reception. Their macros can run and update the scene while ZMODEM retains the connection. Return strings stay suppressed through the end of the batch. Graphic windows remain movable regardless of the stealth setting. The close callback follows the setting at its definition.

BBS sound themes, resident sound bites and big-sample profiles use signed 8-bit PCM from the entry’s `DDOFX/BBSTHEME` resources. `MTALK` assembles loose RealPlay files from named `.SND` members, supports the four pause lengths, and can append, replace or delete a phrase. RealPlay streams loose sample files from the same directory. `dACARD` reports the SDK’s 16-bit stereo capability `SB 16` when SyncTERM has audio output configured, and `NONE` otherwise. The mixer advances the samples at the requested frequency alongside MOD or S3M playback. A local peer checks RealPlay and the stereo samples from resident sound bites and multipart profiles. In Lite and Normal modes it checks the order and rate of playback, the `6CHECKBS` replies during and after playback, and the effect of unloading an active profile. In Normal mode it also assigns a BBS theme sample to an animation frame and checks the audio when that frame is drawn.

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

A BBS can load legacy and four-bank BMF fonts into 32 slots with `2F` and switch between loaded fonts with `2FSELECT`. On the selected desktop or window page, `2X` scales a variable-width string to the specified rectangle, draws an optional one-pixel outline, and rotates it about the rectangle center. Both VGA and bitmap text render variable data through its recorded length, including glyph zero. The bitmap inline colour selector accepts palette index zero. A visual bitmap print refreshes its area in the window. If the scratch image allocation fails, the current VGA font renders inside a colour-10 box.

Desktop pages and the alerted window’s virtual pages support lower-left anchored image scaling, rotation between pages with a transparent index and independent pivots, palette-based anti-aliasing, and single-index outlines. Direct VGA text and image transforms on the visual VGP become visible on refresh. Explicit work and background page destinations remain on the desktop.

In Normal mode the BBS can show or hide the mouse pointer, query its position and three logical buttons, and read separate press and release counts and positions for each button. The BBS can position the DDO pointer with `3MMOV` and confine it to a rectangle with `3MLIM`. SyncTERM accepts both the SDK spelling `MSPD` and the mouse-family spelling `3MSPD`. Relative to the default rates of 8 horizontally and 16 vertically, doubling an axis rate halves its travel. Motion is measured in desktop raster pixels, with fractional movement carried between samples. Up to 300 desktop regions share one table. Overlapping regions run in slot order. Each macro finishes before its return string is sent. Held repeatable regions run at 50 ms intervals during idle input. Non-repeating regions wait for all buttons to be released.

In Lite and Normal, `EEDTEXT` opens a full-screen editor for a CP437 message of up to 65,532 bytes in the entry’s BBSTHEME directory. A new message starts empty. F2 saves and exits, and Escape discards edits and exits. Both send the door’s completion string. Existing line endings are preserved, and Enter inserts a DOS CRLF. The door can then read the saved bytes with `ERTEXT`.

`EPTEXT` can export a BBSTHEME file in both modes. The door supplies a suggested DOS filename and the user selects the local destination in Save As. The output starts with the SDK’s single space, followed by the complete file, including binary printer data. Success, cancellation, a missing source and an unavailable device all send the specified completion string. SyncTERM always treats the SDK’s printer ports (`LPT1`–`LPT3`) and serial ports (`COM1`–`COM4`) as unavailable. A request for any of these seven ports sends only the completion string.

In Lite and Normal, `~INFSAV` and `~INFLOD` use the assigned resource prefix plus `T` in the entry’s BBSTHEME directory. Slot numbers and signed 32-bit passwords follow the SDK. A successful load sends the saved bytes verbatim, including embedded control bytes. Variable capture keeps the first 50 bytes and preserves the destination variable’s type.

In Normal mode, `1OPEN` attaches its text page to the currently alerted graphic window or to the desktop. Characters, the border and both labels use that owner's coordinates. Both `1OPEN` and `1CHANGE` accept script variables for the border, visibility, cursor tracking, manual scrolling, title and help text. A successful `1CHANGE` resets the viewport origin to (1,1) and preserves the virtual page and cursor position. Scrolling the graphic window reveals text drawn outside its original viewport. Changing the alert target leaves subsequent text and VTP scrolling on the original owner. A graphic window covers desktop text while the text continues to update underneath. Moving or closing the graphic window reveals the current text and its blink state. Hiding, moving or closing the text window restores the exposed area from its owner's hidden page.

A marquee reads up to 20,000 bytes from the assigned M resource and uses the selected VGA text font. `5SHOWMQ` paints the initial strip on the work page. Its timer scrolls the strip one pixel to the right per frame on the visual page. A marquee defined in an alerted graphic window scrolls on that window’s virtual page while it is active. The viewport clips the strip. Opaque sprite pixels cover the scrolling text and transparent pixels reveal it. `5STOPMQ` preserves the current position, and `5KILLMQ` releases the slot.

A BBS can open up to 300 indexed FLI/FLC profiles from the entry’s BBSTHEME directory. Each opens paused and plays the requested frame count once or continuously. The host can retain the desktop palette or apply the movie’s palette. Frames draw at the specified upper-left corner. A desktop movie continues behind graphic windows. A movie opened in an alerted window plays in its virtual page while the window is active. Sprite animations retain the latest movie frame underneath them, and desktop clocks and marquees remain visible over the movie. `2RESA` changes the rate of running and subsequently opened movies.

Analog and text clocks have 30 slots each. A BBS defines each clock before showing it on the visual page. The hands and the eight-character digital display use local time with signed hour, minute and second offsets. A clock defined with a graphic window alerted uses that window’s virtual coordinates. It runs while its window is active. Scrolling reveals the corresponding part of the clock, and open windows cover desktop clocks. Each text clock loads its own VGA font, defaulting to IBMSTD.FNT. Active marquees draw over clocks.

The BBS can create up to 300 push buttons with `5CPUSHB`. Creation draws the up sprite on the desktop work page or in the alert window. Window buttons keep their owner when a later command changes the alert window. A matching key or mouse combination draws the down sprite on the visual page for three clock ticks, restores the up sprite, then runs the macro and sends the return string. The pass flag controls whether a keyboard activation continues to the next binding and the connection.

Radio buttons and checkboxes each have 300 slots shared by the desktop and graphic windows. A key or left click changes the selection. An explicit execute command runs the selected members of the addressed exclusive or multiple-selection group in slot order. Each macro finishes before its return string is sent.

Push buttons, radio buttons and checkboxes resolve sprite names when the door defines the control. The door can reuse those variables while the control continues to use its original sprites. Editable text fields likewise accept a variable for their masking character.

List boxes hold up to 100 items in each of 300 slots. A list defined in an alerted graphic window retains that window’s coordinates for navigation, selection and display. The BBS can prepare a list on the work page, show it on the visual page, scroll by line or page, move the highlight, and toggle selections. A left click moves the highlight, and a double-click toggles the item. Execution visits selected items in list order. For `5LBADJLST`, positive movement goes toward the end, following the SDK’s direction parameter description. List descriptions, graphic-window titles and both text-window border labels accept a captured variable containing glyph zero. An empty description in `5LBMODLST` preserves the existing item text.

A BBS can define 250 single-line text fields on the desktop or in graphic windows. Creation and `5SHOWTFLD` paint the owner’s virtual page when its window has focus and the desktop work page otherwise. Selecting a field by command, key or mouse edits it on the visual page. Mouse hits track the owner’s viewport and stacking order. Keyboard bindings remain global. Fields support masked input, letter and digit filters, linked navigation and grouped submission in creation order. Each field’s macro finishes before its return is sent. The two `~GTXTFLD` commands take the variable number first and field number second, following the SDK. They preserve the variable’s type and store up to 50 bytes. The short form copies the literal field text. The full form joins the prefix, text and suffix and decodes caret escapes once, as field submission does. Both forms preserve embedded NULs.

Desktop and graphic-window hover fields share a table of 300 slots. Graphic-window fields and regions use virtual-page coordinates and retain their slot association across window recreation. The highest matching slot runs its macro, sends its return string and selects its static or animated pointer. As specified in the SDK, leaving a field for uncovered space preserves the remembered slot. Re-entering it fires again after a different field has won or the BBS has sent `3MFFRS`.

The host can load either MPT pointer format from the assigned M resource. A selected image keeps its own hotspot and AND/XOR masks when the BBS replaces or clears its list entry. The bitmap presenter applies those masks to the displayed palette indices, leaving the graphics pages available for capture and copying under the pointer. The 100 static slots and 100 animation profiles support one-shot, looping and oscillating playback.

The raycaster renders a 244 × 64 world with textured walls, floors, ceilings and three transparent object layers. The BBS can generate a seeded maze, edit individual cells or import the two map sections specified by the SDK’s DEFMAP.TXT from the assigned M resource. The overview uses the visual page alerted at the time of the dump. Later annotations and player marker moves use that same page. Starting the engine binds its viewport to the alerted window or the desktop. A window viewport advances while its owner has focus. A new maze starts the player at (5,5) and returns a reachable endpoint. Map queries report the SDK texture numbers. The initial textures are generated locally. Hosts can replace them with copies of 32 × 32 sprites or animate them from a live range of sprite slots. Movement checks walls and object characteristics, and an interactive encounter stops rendering and reports the object coordinates to the host or capture variable.

Raycaster cells can share any of 3,600 profiles. Instant profiles run on cell entry. Action profiles can require a DOS key, a mouse or joystick button combination, and a facing direction. Each link runs its macro and sends its return before selecting the next link. A macro can change that link or delete the profile. Ordinary keys reach the host before the matching profile's return.

`5SAVE3D` and `5LOAD3D` exchange 1,141,178-byte loose MAP files in BBSTHEME. Loading restores the map, object layers, texture atlases, animation definitions, cell profiles, viewport and heading. The SDK specifies that the player keeps the current X and Y when switching environments. The file format has one collision characteristic per cell, shared by its saved layers. A saved animation interval uses whole centiseconds. A paused texture keeps its saved atlas image. An active texture resumes from the recorded sprite slot using the frames currently in the separate sprite bank.

The raycaster records changed camera positions and headings as six-byte REC records in BBSTHEME. Playback applies one recorded pose per frame through the final record. A recording played from the assigned M resource is limited to 60,000 bytes. Its extracted file is removed on completion, replacement or reset.

The Normal-mode landscape accepts a seed or a 255 × 255 indexed map from the visual or work page. The lower-left map pixel is world coordinate (0,0). The camera has 256 positions on each axis, so an imported map wraps its extra row and column to the opposite edge. The BBS can set or adjust the camera, query its position and direction, and fly over the terrain in a 320 × 200 viewport. Clouds have their own placement. The last frame stays visible when flight stops. Creating the landscape binds it to the current graphic window or desktop. Flight pauses while its owning window is inactive. Sprites can animate over the terrain. The map commands use the currently alerted visual page or the shared work page.

Normal mode supports a tile map of up to 150 × 150 cells. The host can fill either layer, move the viewport, move or remove foreground tiles, and animate consecutive sprite frames. Grid assignments become visible on a draw. Movement redraws immediately. A grid created in an alerted window retains that window as its owner. Its tiles animate while the owner is active, and removing the grid restores the underlying page.

Normal mode starts a ten-minute inactivity timer. `5SLEEPER,minutes` replaces and restarts it. A keystroke, mouse button press or mouse field activation rearms it. A local window counts down the final three minutes over the paused scene. A dismissing key restores the scene and starts the interval again. For short positive intervals the countdown starts after one second of inactivity. Expiry closes the connection. Zero requests immediate expiry. A Normal reset restores ten minutes, and Lite mode has no inactivity timer.

In Normal mode, `5PING,minutes,string` sends up to ten decoded bytes to the host at the configured interval, independently of keyboard activity and variable capture. The door can supply both the interval and the string through variables. Redefining either variable leaves the scheduled ping intact. A zero interval or Normal reset disables it. Lite mode and background file transfers suspend transmission. Resuming an overdue timer sends one ping and starts a fresh interval.

In Lite and Normal mode, `dDRIVE` reports free space available to the user on the BBS asset-cache volume. `dMEM` estimates available memory from the OS. Both report bytes up to the largest signed DDO script integer (2,147,483,647), and return −1 if the measurement is unavailable. When a positive `dVMEM` request exceeds measured memory, SyncTERM reports the shortfall through the connection’s alert handler, ends local script execution, and returns to text mode. It continues receiving host input. `dSPEED` reports a numeric processing-speed estimate taken once per process. SyncTERM samples the string, integer and floating-point workload of the SDK-era MPORT diagnostic over a short interval. The index is an estimate across architectures.

Doors can supply a resource prefix, member or loose filename through a system variable. For example, after `~DEFVAR,62,1,Sample.bin`, the query `dmDEFVAR62` reads the metadata for `Sample.bin` in MUSIC. Names such as `DEFVAR.FNT` are literal filenames.

Loose-file queries read the entry’s `DDOFX/MUSIC`, `DDOFX/FONTS`, and `DDOFX/BBSTHEME` directories. Professional queries use `DDOFX/TERMS/<client>`. Names use DOS-style case matching. The timestamp is local modification time with DOS two-second precision. `8EXEC` consumes the command without starting a program.

In Lite and Normal mode, `EWTEXT` creates or replaces a loose BBSTHEME file with flag 0 and appends with flag 1. When a variable supplies the line, the command writes its full byte count, including embedded NULs, then adds the carriage return specified by the SDK. `ERTEXT` sends the file bytes followed by the supplied return string. A missing file still sends the return string.

Resource assignment selects the M container in `DDOFX/BBSTHEME` for local scripts and bitmap fonts and the S container for indexed PCX artwork. Scripts up to 40,000 bytes can call other scripts and render DOS text. A resource entry query tests the member name and component ID. Conditional scripts can also read that ID.

In Normal mode, `2STORES` saves the current sprite slots as paired DAT and RAW files in the entry’s BBSTHEME directory. `2LOADS` validates both files before replacing the live slots. Its erase flag removes the pair after a successful load. A bank preserves slot order, indexed pixel rows and opaque allocation tails.

In Lite and Normal mode, `2RESET` unlocks the keyboard and clears key bindings, macro definitions, variables, return strings, query capture, joystick bindings, timed events and big-sample profiles. The current script continues after the command. In Normal mode it also clears all four graphics pages and releases sprites, simple animations, bitmap fonts and music bars. The assigned resource prefix, music, theme sounds, palette and saved page files survive a scene reset.

In Lite and Normal, `4MKEY` binds DOS scan/character pairs or the all-keys wildcard. Matching slots run in order, with each macro completing before its return bytes and any passed keystroke. The DDO key lock admits programmed bindings and consumes ordinary keyboard input. SyncTERM processes its local shortcuts before graphics input.

Lite and Normal expose two DOS joystick slots through SyncTERM’s controller mappings. The left stick, first hat and D-pad provide eight directions. The south/east buttons or joystick trigger/thumb supply buttons 1 and 2. Analog movement has a dead zone of one quarter of the axis range from center to an endpoint. The host can detect and calibrate each joystick, bind directions for each button combination, bind the three centered button combinations, and activate or clear each slot. Held actions repeat.

Lite scripts can define 32 ten-line macros, 300 return strings, and 1,000 typed variables. Local macro calls, repeats and waits preserve the order of the wire stream. Named macro lines, conditional and repeated bodies, and control callbacks use the full byte count of a captured variable. Commands after a NUL still execute, including after a raycaster profile is saved and loaded. Variable capture redirects automatic query results. Configured return strings can send binary bytes. Key and control definitions copy the return bytes from the variable when the door defines the action. A leading or embedded NUL is preserved through activation and through saving and loading a raycaster profile. Conditional macros compare literal text, typed user variables and the SDK’s predefined master operands. These cover identity, memory, storage, speed, graphics and audio capabilities, the mouse, the current music, theme and font, loose-file metadata and resource member IDs. `~REPVAR` runs its body before testing, including when the body defines the variable being tested.

Integer parameters accept signed decimal and `$`-prefixed hexadecimal values. When a parameter reads a defined variable, empty or nonnumeric content supplies zero. The stored bytes remain available to text commands. Numeric comparisons and arithmetic use the full variable value, including any embedded NUL. Left and right padding use the first byte of the supplied character, including NUL. `~RNDVAR` draws from 1 through the requested maximum. A scene reset clears the variables and subsequent draws continue the session’s random sequence.

## Command inventory

Send each command body between the two opening bytes `1D 7B` and the closing byte `7D` (hex). For example, a door can send `1D 7B 64 4C 49 54 45 52 4D 7D` to query `dLITERM`. SyncTERM replies with the ASCII text `LITERM Ver. 2.00` followed by `FF`. The command column gives the prefix before any arguments. Case matters for commands such as `t` and `T`.

| SDK line | Command | Definition | Domain | Coverage |
| ---: | --- | --- | --- | --- |
| 575 | `f` | LOAD FONT | DDO-FX LITE | Connected |
| 616 | `m` | LOAD MUSIC | DDO-FX LITE | MOD, S3M, CMF with embedded instruments |
| 635 | `t` | LOAD BBS THEME | DDO-FX LITE | Connected |
| 693 | `tb` | LOAD SOUND BITES LIBRARY | DDO-FX LITE | Connected |
| 767 | `T` | LOAD TERM THEME | DDO-FX LITE | Connected |
| 828 | `p` | PLAY SAMPLE | DDO-FX LITE | Connected |
| 858 | `pb` | PLAY SOUND BITE SAMPLE | DDO-FX LITE | Connected |
| 868 | `P` | PLAY SAMPLE At FREQUENCY | DDO-FX LITE | Connected |
| 882 | `PB` | PLAY SOUND BITE SAMPLE At FREQUENCY | DDO-FX LITE | Connected |
| 894 | `6LBSOUND` | LOAD BIG SAMPLE | DDO-LITE | Connected |
| 993 | `6DEFBSND` | DEFINE BIG SAMPLE | DDO-LITE | Connected |
| 1055 | `6ADDBSND` | ADD BIG SAMPLE PART | DDO-LITE | Connected |
| 1086 | `6UNLOADBS` | UNLOAD BIG SAMPLE | DDO-LITE | Connected |
| 1102 | `6PLAYBS` | PLAY BIG SAMPLE | DDO-LITE | Connected |
| 1124 | `6CHECKBS` | CHECK BIG SAMPLE | DDO-LITE | Connected |
| 1167 | `MPREAL` | PLAY REALPLAY SAMPLE | DDO-FX LITE | Connected |
| 1207 | `MTALK` | TEXT TO SPEECH | DDO-FX LITE | Connected |
| 1417 | `MMPAT` | MODIFY MUSIC PATTERN | DDO-FX LITE | Connected |
| 1447 | `MMPR` | PAUSE RESUME MOD/S3M | DDO-FX LITE | Connected |
| 1459 | `MSVOL` | SET VOLUME | DDO-FX LITE | Connected |
| 1476 | `MMVOL` | MODIFY VOLUME | DDO-FX LITE | Connected |
| 1491 | `MGVOL` | GET VOLUME | DDO-FX LITE | Connected |
| 1509 | `MICMF` | IS CMF PLAYING | DDO-FX LITE | Connected |
| 1528 | `w` | WAIT | DDO-FX LITE | Connected |
| 1544 | `LIGMODE` | GOTO DDO-FX (NORMAL MODE) | DDO-FX LITE | Connected |
| 1573 | `dLITERM` | DETECT LITERM | DDO-FX LITE | Connected |
| 1611 | `dMEM` | DETECT USER MEMORY | DDO-FX LITE | Connected |
| 1634 | `dVMEM` | DETECT USER MEMORY & REACT | DDO-FX LITE | Connected |
| 1729 | `dDRIVE` | DETECT USER DRIVE SIZE | DDO-FX LITE | Connected |
| 1755 | `dVCARD` | DETECT GRAPHICS ADAPTER TYPE | DDO-FX LITE | Connected |
| 1770 | `dACARD` | DETECT AUDIO CARD | DDO-FX LITE | Connected |
| 1798 | `dSPEED` | DETECT Machine Speed | DDO-FX LITE | Connected |
| 1851 | `dVIDMODE` | DETECT VIDEO MODE | DDO-FX LITE | Connected |
| 1880 | `dm` | DETECT FOR MUSIC FILE | DDO-FX LITE | Connected |
| 1921 | `dt` | DETECT FOR THEME FILE | DDO-FX LITE | Connected |
| 1929 | `2f` | ASSIGN RESOURCE FILE | DDO-FX LITE | Connected |
| 2249 | `dr` | DETECT FOR RESOURCE FILE | DDO-FX LITE | Connected |
| 2262 | `dRESOURCE` | DETECT FOR RESOURCE ENTRY | DDO-FX LITE | Connected |
| 2320 | `df` | DETECT FOR FONT FILE | DDO-FX LITE | Connected |
| 2328 | `dMUSICON` | DETECT MUSIC ON/OFF | DDO-FX LITE | Connected |
| 2345 | `dSOUNDON` | DETECT SOUND F/X ON/OFF | DDO-FX LITE | Connected |
| 2367 | `dGETMUSIC` | DETECT MUSIC PLAYING | DDO-FX LITE | Connected |
| 2389 | `dGETTHEME` | DETECT THEME LOADED | DDO-FX LITE | Connected |
| 2411 | `dGETFONT` | DETECT FONT LOADED | DDO-FX LITE | Connected |
| 2431 | `4DEFM` | DEFINE A MACRO | DDO-FX LITE | Connected |
| 2590 | `4CLRM` | CLEAR A MACRO | DDO-FX LITE | Connected |
| 2602 | `4RUNM` | RUN A MACRO | DDO-FX LITE | Connected |
| 2615 | `4EXCM` | EXECUTE A MACRO LINE | DDO-FX LITE | Connected |
| 2662 | `4MKEY` | CREATE KEY MACRO | DDO-FX LITE | Connected |
| 2874 | `6TESTJOY` | TEST FOR JOYSTICK | DDO-LITE | Connected |
| 2893 | `6CALJOY` | CALIBRATE JOYSTICK | DDO-LITE | Connected |
| 2913 | `6DEFJOY` | DEFINE JOYSTICK | DDO-LITE | Connected |
| 3067 | `7CEVENT` | CREATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3161 | `7AEVENT` | ACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3174 | `7DEVENT` | DEACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3187 | `7DEFRET` | DEFINE RETURN STRING | DDO-LITE | Connected |
| 3226 | `7SRET` | SEND RETURN STRING | DDO-LITE | Connected |
| 3237 | `0TFADEOUT` | TEXT FADE OUT | DDO-FX LITE | Connected |
| 3264 | `0TFADEIN` | TEXT FADE IN | DDO-FX LITE | Connected |
| 3273 | `0SLIDEU` | SLIDE SCREEN UP | DDO-FX LITE | Connected |
| 3294 | `0SLIDED` | SLIDE SCREEN DOWN | DDO-FX LITE | Connected |
| 3307 | `0BOUNCED` | BOUNCE SCREEN DOWN | DDO-FX LITE | Connected |
| 3323 | `!` | DISPLAY RAW ANSI | DDO-FX LITE | Connected |
| 3365 | `0SETFUL` | SET FULL SCREEN MODE | DDO-FX LITE | Connected |
| 3400 | `0RSETFUL` | RESTORE FULL SCREEN MODE | DDO-FX LITE | Connected |
| 3416 | `*` | RUN LOCAL SCRIPT | DDO-FX LITE | Connected |
| 3501 | `~DEFVAR` | DEFINE SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3604 | `~LOCKKEYS` | LOCK KEYS | DDO-FX LITE | Connected |
| 3650 | `~UNLOCKKEYS` | UNLOCK KEYS | DDO-FX LITE | Connected |
| 3661 | `~PAUSEBAK` | PAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events and background graphics, including clocks, marquees, sprite animations, tile maps, polygon objects and the raycaster |
| 3710 | `~UNPAUSEBAK` | UNPAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events and background graphics, including clocks, marquees, sprite animations, tile maps, polygon objects and the raycaster |
| 3720 | `~IGNORCMD` | IGNORE COMMANDS | DDO-FX LITE | Connected |
| 3767 | `~HEARCMD` | HEAR COMMANDS | DDO-FX LITE | Connected |
| 3782 | `~CAPVAR` | ALL CAPS SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3796 | `~GTXTFLD+` | GET TEXT FIELD INFO FULL | DDO-FX LITE | Connected on the desktop and in graphic windows |
| 3824 | `~GTXTFLD-` | GET TEXT FIELD INFO | DDO-FX LITE | Connected on the desktop and in graphic windows |
| 3841 | `~INCVAR` | INCREMENT SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3855 | `~DECVAR` | DECREMENT SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3870 | `~RNDVAR` | RANDOM SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3893 | `~VARCPY` | VARIABLE COPY | DDO-FX LITE | Connected |
| 3941 | `~VARINS` | VARIABLE INSERT | DDO-FX LITE | Connected |
| 3989 | `~PADVARL` | PAD VARIABLE LEFT | DDO-FX LITE | Connected |
| 4041 | `~PADVARR` | PAD VARIABLE RIGHT | DDO-FX LITE | Connected |
| 4063 | `~VARDEL` | VARIABLE DELETE | DDO-FX LITE | Connected |
| 4096 | `~VARPOS` | VARIABLE POSITION | DDO-FX LITE | Connected |
| 4132 | `~VARLEN` | VARIABLE LENGTH | DDO-FX LITE | Connected |
| 4157 | `~VAREQU` | VARIABLE EQUAL | DDO-FX LITE | Connected |
| 4186 | `~VARTYP` | VARIABLE TYPING | DDO-FX LITE | Connected |
| 4209 | `~VARADD` | VARIABLE ADD | DDO-FX LITE | Connected |
| 4224 | `~VARSUB` | VARIABLE SUBTRACT | DDO-FX LITE | Connected |
| 4239 | `~VARMLT` | VARIABLE MULTIPLY | DDO-FX LITE | Connected |
| 4254 | `~VARDIV` | VARIABLE DIVIDE | DDO-FX LITE | Connected |
| 4270 | `~VARCAP` | VARIABLE CAPTURE | DDO-FX LITE | Connected |
| 4322 | `~IFVAR` | IF VAR DO MACRO | DDO-FX LITE | Connected for literals, typed variables and all SDK master operands |
| 4547 | `~REPVAR` | REPEAT MACRO UNTIL VAR | DDO-FX LITE | Connected for literals, typed variables and all SDK master operands |
| 4604 | `~REPMAC` | REPEAT MACRO | DDO-FX LITE | Connected |
| 4654 | `~INFSAV` | INFO SAVE | DDO-FX LITE | Connected |
| 4728 | `~INFLOD` | INFO LOAD | DDO-FX LITE | Connected |
| 4777 | `EEDTEXT` | LAUNCH TEXT EDITOR | DDO-FX LITE | Connected |
| 4821 | `ERTEXT` | RETRIEVE TEXT FILE | DDO-FX LITE | Connected |
| 4847 | `EWTEXT` | WRITE TEXT FILE | DDO-FX LITE | Connected |
| 4886 | `EPTEXT` | PRINT TEXT FILE | DDO-FX LITE | Connected. File export and completion for all seven reserved ports in Lite and Normal |
| 4940 | `:` | LABEL | DDO-FX LITE | Connected |
| 4955 | `2RESET` | RESET ENVIRONMENT | DDO-FX LITE | Connected for implemented scene objects |
| 5436 | `dMOUSE` | DETECT MOUSE | DDO-FX (NM) | Connected |
| 5457 | `1OPEN` | OPEN GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5780 | `1GOTOC` | GOTO CURSOR | DDO-FX (NM) | Desktop and attached text windows |
| 5819 | `1CLOSE` | CLOSE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5855 | `1HIDE` | HIDE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5866 | `1UNHIDE` | UNHIDE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5874 | `1WIN` | ACTIVATE WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 5884 | `1SWAP` | SWAP WINDOWS | DDO-FX (NM) | Desktop and attached text windows |
| 5924 | `1POS` | POSITION VTP ORIGIN | DDO-FX (NM) | Desktop and attached text windows |
| 5956 | `1POSR` | POSITION VTP ORIGIN RELATIVE | DDO-FX (NM) | Desktop and attached text windows |
| 5982 | `1GETPOS` | GETPOS OF GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 6006 | `1CHANGE` | CHANGE GRAPHIC TEXT WINDOW | DDO-FX (NM) | Desktop and attached text windows |
| 6035 | `1SCALE` | SCALE GRAPHIC TEXT FONTS | DDO-FX (NM) | Desktop and attached text windows |
| 6100 | `1SETC` | SET GRAPHIC TEXT WINDOW COLUMNS | DDO-FX (NM) | Desktop and attached text windows |
| 6131 | `2b` | LOAD PCX TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6163 | `2w` | LOAD PCX TO WORK PAGE | DDO-FX (NM) | Connected |
| 6179 | `2v` | LOAD PCX TO VISUAL PAGE | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6199 | `2F` | LOAD A BITMAP FONT | DDO-FX (NM) | Connected |
| 6237 | `2FSELECT` | BITMAP FONT SELECT | DDO-FX (NM) | Connected |
| 6246 | `2CPYBP2WP` | COPY BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6254 | `2CPYBP2VP` | COPY BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6261 | `2CPYWP2BP` | COPY WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6268 | `2CPYWP2VP` | COPY WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6275 | `2CPYVP2BP` | COPY VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6282 | `2CPYVP2WP` | COPY VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6289 | `2CUTBP2WP` | CUT BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6320 | `2CUTBP2VP` | CUT BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6328 | `2CUTWP2BP` | CUT WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6336 | `2CUTWP2VP` | CUT WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 6344 | `2CUTHP2VPT` | CUT HIDDEN PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6355 | `2CUTHP2WPT` | CUT HIDDEN PAGE TO WORK PAGE (T) | DDO-FX (NM) | Connected |
| 6366 | `2CUTHP2BPT` | CUT HIDDEN PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Connected |
| 6377 | `2CUTWP2VPT` | CUT WORK PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6388 | `2CUTWP2HPT` | CUT WORK PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Connected |
| 6399 | `2CUTWP2BPT` | CUT WORK PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Connected |
| 6410 | `2CUTBP2HPT` | CUT BACKGROUND PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Connected |
| 6421 | `2CUTBP2WPT` | CUT BACKGROUND PAGE TO WORK PAGE (T) | DDO-FX (NM) | Connected |
| 6432 | `2CUTBP2VPT` | CUT BACKGROUND PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Connected |
| 6443 | `2CUTVP2BP` | CUT VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6453 | `2CUTVP2WP` | CUT VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 6463 | `2CCOL` | DEFINE TRANSPARENT COLORS | DDO-FX (NM) | Connected |
| 6514 | `2BP2PCX` | SAVE BACKGROUND PAGE TO PCX | DDO-FX (NM) | Connected |
| 6528 | `2WP2PCX` | SAVE WORK PAGE TO PCX | DDO-FX (NM) | Connected |
| 6536 | `2VP2PCX` | SAVE VISUAL PAGE TO PCX | DDO-FX (NM) | Connected |
| 6544 | `2LVPPCX` | LOAD VISUAL PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6556 | `2LWPPCX` | LOAD WORK PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6568 | `2LBPPCX` | LOAD BACKGROUND PAGE PCX | DDO-FX (NM) | Connected on the desktop and alert visual page |
| 6580 | `2S` | GET SPRITE | DDO-FX (NM) | Connected |
| 6669 | `2SV` | GET SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6679 | `2SB` | GET SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6689 | `2SH` | GET SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6699 | `2sD` | DRAW SPRITE | DDO-FX (NM) | Connected |
| 6725 | `2sDV` | DRAW SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6733 | `2sDB` | DRAW SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6741 | `2sDH` | DRAW SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6749 | `2sC` | CLIP SPRITE | DDO-FX (NM) | Connected |
| 6762 | `2sCV` | CLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6769 | `2sCB` | CLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6776 | `2sCH` | CLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6783 | `2sF` | FLIP SPRITE | DDO-FX (NM) | Connected |
| 6799 | `2sFV` | FLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6806 | `2sFB` | FLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6813 | `2sFH` | FLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6820 | `2sR` | REVERSE SPRITE | DDO-FX (NM) | Connected |
| 6831 | `2sRV` | REVERSE SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6838 | `2sRB` | REVERSE SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6845 | `2sRH` | REVERSE SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6852 | `2sP` | PUT SPRITE | DDO-FX (NM) | Connected |
| 6861 | `2sPV` | PUT SPRITE VISUAL PAGE | DDO-FX (NM) | Connected |
| 6868 | `2sPB` | PUT SPRITE BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6875 | `2sPH` | PUT SPRITE HIDDEN PAGE | DDO-FX (NM) | Connected |
| 6882 | `2sI` | INVERT SPRITE | DDO-FX (NM) | Connected |
| 6895 | `2sFF` | FREE ALL SPRITES | DDO-FX (NM) | Connected |
| 6904 | `2STORES` | STORE ALL SPRITES | DDO-FX (NM) | Connected |
| 6919 | `2LOADS` | LOAD ALL SPRITES | DDO-FX (NM) | Connected |
| 6939 | `2ANIM` | DEFINE ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7151 | `2ANIMSOUND` | DEFINE ANIM SOUND | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7186 | `2PLAYA` | PLAY ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7219 | `2STOPA` | STOP ANIM PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7235 | `2AFLY` | CHANGE ANIM PROFILE ON THE FLY | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7246 | `2COLL` | ANIM COLLISION PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7339 | `2RESA` | RESOLUTION ANIMATOR | DDO-FX (NM) | Connected for FLI/FLC and animation profiles |
| 7383 | `2MOTION` | DEFINE MOTION PROFILE | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7466 | `2MFRAME` | DEFINE MOTION FRAME CONTROL | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 7534 | `2SS` | DEFINE SIMPLE SPRITE | DDO-FX (NM) | Connected |
| 7652 | `2SSA` | DEFINE SIMPLE SPRITE ANIMATION | DDO-FX (NM) | Connected |
| 7733 | `2CSS` | CLEAR SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7747 | `2ACSS` | CLEAR ALL SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7754 | `2DSS` | DRAW SIMPLE SPRITES | DDO-FX (NM) | Connected |
| 7765 | `2CPYBP2HP` | COPY BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7824 | `2CPYHP2BP` | COPY HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 7831 | `2CPYWP2HP` | COPY WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7838 | `2CPYHP2WP` | COPY HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 7845 | `2CPYVP2HP` | COPY VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7852 | `2CPYHP2VP` | COPY HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 7859 | `2CUTBP2HP` | CUT BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7869 | `2CUTHP2BP` | CUT HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 7879 | `2CUTWP2HP` | CUT WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7889 | `2CUTHP2WP` | CUT HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Connected |
| 7899 | `2CUTVP2HP` | CUT VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Connected |
| 7909 | `2CUTHP2VP` | CUT HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Connected |
| 7919 | `2X` | DO PRINT | DDO-FX (NM) | Connected, including window pages and the low-memory text box |
| 8058 | `2XT` | DO PRINT TEXT FONT | DDO-FX (NM) | Connected on desktop and window pages |
| 8110 | `2XTWP` | DO PRINT TEXT FONT WP | DDO-FX (NM) | Connected on the desktop |
| 8118 | `2XTBP` | DO PRINT TEXT FONT BP | DDO-FX (NM) | Connected on the desktop |
| 8127 | `2XTHP` | DO PRINT TEXT FONT HP | DDO-FX (NM) | Connected on desktop and window pages |
| 8136 | `2C` | SET COLOR | DDO-FX (NM) | Connected |
| 8146 | `2aP` | DRAW POINT | DDO-FX (NM) | Connected |
| 8156 | `2aPX` | DRAW POINT XOR | DDO-FX (NM) | Connected |
| 8163 | `2aERASE` | ERASE PAGE | DDO-FX (NM) | Connected |
| 8172 | `2aG` | Get Pixel | DDO-FX (NM) | Connected |
| 8182 | `2aM` | MOVE GRAPHICS CURSOR | DDO-FX (NM) | Connected |
| 8192 | `2aPAINT` | PAINT | DDO-FX (NM) | Connected |
| 8209 | `2aSRGB` | SET RGB | DDO-FX (NM) | Connected |
| 8221 | `2aGRGB` | GET RGB | DDO-FX (NM) | Connected |
| 8234 | `2aSTOREPAL` | STORE PALETTE | DDO-FX (NM) | Connected |
| 8245 | `2aRESTOREPAL` | RESTORE PALETTE | DDO-FX (NM) | Connected |
| 8253 | `2aZEROPAL` | ZERO PALETTE | DDO-FX (NM) | Connected |
| 8260 | `2aFPALOUT` | FADE PALETTE OUT | DDO-FX (NM) | Connected |
| 8299 | `2aFPALOUTA` | FADE PALETTE OUT ADVANCED | DDO-FX (NM) | Connected |
| 8350 | `2aFPALIN` | FADE PALETTE IN | DDO-FX (NM) | Connected |
| 8361 | `2aDRAWREL` | DRAW LINE RELATIVE | DDO-FX (NM) | Connected |
| 8374 | `2aCHORG` | CHANGE VIEW PORT ORIGIN | DDO-FX (NM) | Connected |
| 8394 | `2aSHAKE` | SHAKE SCREEN | DDO-FX (NM) | Connected |
| 8404 | `2aRECT` | DRAW FILLED RECTANGLE | DDO-FX (NM) | Connected |
| 8420 | `2aCLIP` | SET CLIPPING REGION | DDO-FX (NM) | Connected |
| 8439 | `2aCIRC` | DRAW CIRCLE | DDO-FX (NM) | Connected |
| 8451 | `2aCIRCF` | DRAW FILLED CIRCLE | DDO-FX (NM) | Connected |
| 8463 | `2aDRAW` | DRAW LINE | DDO-FX (NM) | Connected |
| 8474 | `2aDRAWX` | DRAW LINE XOR | DDO-FX (NM) | Connected |
| 8481 | `2aDEFPAL` | DEFAULT 16 COLOR PALETTE | DDO-FX (NM) | Connected |
| 8492 | `2aDRWBOX` | DRAW BOX | DDO-FX (NM) | Connected |
| 8508 | `2aDRWBXX` | DRAW BOX XOR | DDO-FX (NM) | Connected |
| 8524 | `2aBOXLNS` | EDIT BOX LINES | DDO-FX (NM) | Connected |
| 8533 | `2aCLPRCT` | DRAW CLIPPED RECTANGLE | DDO-FX (NM) | Connected |
| 8548 | `2aDRDASH` | DRAW DASHED LINE | DDO-FX (NM) | Connected |
| 8564 | `2aDRDSHRL` | DRAW DASHED LINE RELATIVE | DDO-FX (NM) | Connected |
| 8580 | `2aELIPS` | DRAW ELLIPSE | DDO-FX (NM) | Connected |
| 8592 | `2aELIPSF` | DRAW FILLED ELLIPSE | DDO-FX (NM) | Connected |
| 8599 | `2aFILL` | FILL PAGE | DDO-FX (NM) | Connected |
| 8607 | `2aFLOOD` | FLOOD | DDO-FX (NM) | Connected |
| 8616 | `2aMVREL` | MOVEIT RELATIVE | DDO-FX (NM) | Connected |
| 8624 | `2aBCURV` | DRAW BEZIER CURVE | DDO-FX (NM) | Connected |
| 8646 | `2aBCURVX` | DRAW BEZIER CURVE XOR | DDO-FX (NM) | Connected |
| 8656 | `2aSCROLL` | VERTICAL SCROLL | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 8694 | `3MACT` | MOUSE ACTIVE | DDO-FX (NM) | Connected |
| 8713 | `3MPT` | CHANGE MOUSE POINTER | DDO-FX (NM) | Connected |
| 8750 | `3MPL` | LOAD MOUSE POINTER | DDO-FX (NM) | Connected |
| 8776 | `3SMP` | SELECT MOUSE POINTER | DDO-FX (NM) | Connected |
| 8786 | `3AMP` | ANIMATED MOUSE POINTER | DDO-FX (NM) | Connected |
| 8837 | `3AAM` | ACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Connected |
| 8846 | `3DAM` | DEACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Connected |
| 8855 | `3MVIS` | MOUSE VISIBILITY | DDO-FX (NM) | Connected |
| 8866 | `3MLIM` | MOUSE LIMITS | DDO-FX (NM) | Connected |
| 8875 | `3MMOV` | MOUSE MOVE | DDO-FX (NM) | Connected |
| 8890 | `3MINF` | MOUSE INFO | DDO-FX (NM) | Connected |
| 8928 | `3MIOLD` | MOUSE INFO OLD | DDO-FX (NM) | Connected |
| 8958 | `MSPD` | MOUSE SPEED | DDO-FX (NM) | Connected |
| 8968 | `3MREG` | MOUSE REGION | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9189 | `3MREGN` | MOUSE REGION NON REPEAT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9211 | `3MFLD` | MOUSE FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9292 | `3MFSWP` | MOUSE FIELD SWAP | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9302 | `3MFFRS` | MOUSE FIELD FRESHEN | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9317 | `5CRBUTTON` | CREATE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9481 | `5EXRBTN` | EXECUTE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9548 | `5SWPRBTN` | SWAP RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9560 | `5DERBTN` | DISABLE/ENABLE RADIO BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9576 | `5CCHKBOX` | CREATE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9679 | `5EXCBOX` | EXECUTE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9688 | `5SWPCBOX` | SWAP CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9697 | `5DECBOX` | DISABLE/ENABLE CHECK BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9706 | `5CPUSHB` | CREATE PUSH BUTTON | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9792 | `5CTXTFLD` | CREATE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9973 | `5SHOWTFLD` | SHOW TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9981 | `5ACTTFLD` | ACTIVATE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 9992 | `5EXTFLD` | EXECUTE TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10008 | `5SWPTFLD` | SWAP TEXT FIELD | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10016 | `5CLBOX` | CREATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10143 | `5LBMODLST` | MODIFY LIST ITEM | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10197 | `5LBDRWLST` | DRAW LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10211 | `5LBSHWLST` | SHOW LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10224 | `5LBEXCLST` | EXECUTE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10237 | `5LBCLSLST` | KILL LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10248 | `5LBDEACT` | DEACTIVATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10262 | `5LBREACT` | REACTIVATE LIST BOX | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10274 | `5LBADJLST` | ADJUST LIST | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10341 | `5LBADJHLT` | ADJUST HIGHLIGHT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10378 | `5LBTGLSEL` | TOGGLE SELECTION | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 10399 | `5CMARQEE` | CREATE MARQUEE | DDO-FX (NM) | Connected |
| 10471 | `5SHOWMQ` | SHOW MARQUEE | DDO-FX (NM) | Connected |
| 10485 | `5STOPMQ` | STOP MARQUEE | DDO-FX (NM) | Connected |
| 10498 | `5KILLMQ` | KILL MARQUEE | DDO-FX (NM) | Connected |
| 10511 | `5OFLIC` | OPEN FLI/FLC | DDO-FX (NM) | Connected |
| 10600 | `5PFLIC` | PAUSE FLI/FLC | DDO-FX (NM) | Connected |
| 10620 | `5UFLIC` | UNPAUSE FLI/FLC | DDO-FX (NM) | Connected |
| 10632 | `5SFLIC` | STOP FLI/FLC | DDO-FX (NM) | Connected |
| 10648 | `5SCLOCK` | START ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10703 | `5SHWCLK` | SHOW ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10715 | `5STPCLK` | STOP ANALOG CLOCK | DDO-FX (NM) | Connected |
| 10725 | `5SOCLOCK` | START TEXT CLOCK | DDO-FX (NM) | Connected |
| 10772 | `5POCLOCK` | PAUSE TEXT CLOCK | DDO-FX (NM) | Connected |
| 10782 | `5UOCLOCK` | UNPAUSE TEXT CLOCK | DDO-FX (NM) | Connected |
| 10794 | `5KOCLOCK` | KILL TEXT CLOCK | DDO-FX (NM) | Connected |
| 10806 | `5CLAND` | CREATE VIRTUAL LANDSCAPE | DDO-FX (NM) | Connected |
| 10890 | `5LCLOUD` | VR CLOUDS | DDO-FX (NM) | Connected |
| 10911 | `5LSKY` | VR SKY | DDO-FX (NM) | Connected |
| 10925 | `5LLPOS` | VR LAND POSITION | DDO-FX (NM) | Connected |
| 10935 | `5LCPOS` | VR CLOUD POSITION | DDO-FX (NM) | Connected |
| 10942 | `5LDMAP` | VR DRAW MAP | DDO-FX (NM) | Connected |
| 10976 | `5LDMAPWP` | VR DRAW MAP ON WORK PAGE | DDO-FX (NM) | Connected |
| 10984 | `5LGMAP` | VR GET MAP | DDO-FX (NM) | Connected |
| 11005 | `5LGMAPWP` | VR GET MAP ON WORK PAGE | DDO-FX (NM) | Connected |
| 11012 | `5LCSPD` | VR CHANGE SPEED | DDO-FX (NM) | Connected |
| 11026 | `5LCDIR` | VR CHANGE DIRECTION | DDO-FX (NM) | Connected |
| 11036 | `5LCX` | VR CHANGE X | DDO-FX (NM) | Connected |
| 11046 | `5LCY` | VR CHANGE Y | DDO-FX (NM) | Connected |
| 11056 | `5LCZ` | VR CHANGE Z | DDO-FX (NM) | Connected |
| 11066 | `5LCD` | VR CHANGE DETAIL | DDO-FX (NM) | Connected |
| 11079 | `5LMSPD` | VR MODIFY SPEED | DDO-FX (NM) | Connected |
| 11093 | `5LMDIR` | VR MODIFY DIRECTION | DDO-FX (NM) | Connected |
| 11100 | `5LMX` | VR MODIFY X | DDO-FX (NM) | Connected |
| 11107 | `5LMY` | VR MODIFY Y | DDO-FX (NM) | Connected |
| 11114 | `5LMZ` | VR MODIFY Z | DDO-FX (NM) | Connected |
| 11121 | `5LMD` | VR MODIFY DETAIL | DDO-FX (NM) | Connected |
| 11128 | `5LGX` | VR GET USER X | DDO-FX (NM) | Connected |
| 11138 | `5LGY` | VR GET USER Y | DDO-FX (NM) | Connected |
| 11148 | `5LGZ` | VR GET USER Z | DDO-FX (NM) | Connected |
| 11158 | `5LGD` | VR GET USER DIRECTION | DDO-FX (NM) | Connected |
| 11167 | `5LANIM` | VR ANIMATE | DDO-FX (NM) | Connected |
| 11176 | `5DANIM` | RC 3D RAYCASTING ENGINE | DDO-FX (NM) | Connected |
| 11262 | `5DSCALE` | RC 3D SCALE | DDO-FX (NM) | Connected |
| 11291 | `5CMAZE` | RC 3D MAZE GENERATOR | DDO-FX (NM) | Connected |
| 11331 | `5LOADMAP` | RC LOAD 3D MAP DEFINITION | DDO-FX (NM) | Connected |
| 11351 | `5VD` | RC ROTATE PERSPECTIVE | DDO-FX (NM) | Connected |
| 11381 | `5VM` | RC MOVE 3D FORWARD/BACKWARDS | DDO-FX (NM) | Connected |
| 11409 | `52DMAP` | RC DUMP MAP | DDO-FX (NM) | Connected |
| 11440 | `52DMMAP` | RC MODIFY DUMP MAP | DDO-FX (NM) | Connected |
| 11466 | `5CTEXTURE` | RC CHANGE TEXTURE LIBRARY | DDO-FX (NM) | Connected |
| 11516 | `5COBJECT` | RC CHANGE OBJ TEXTURE LIBRARY | DDO-FX (NM) | Connected |
| 11574 | `5SETXPOS` | RC SET USER X POSITION | DDO-FX (NM) | Connected |
| 11595 | `5SETYPOS` | RC SET USER Y POSITION | DDO-FX (NM) | Connected |
| 11613 | `5SETANGL` | RC SET USER ANGLE | DDO-FX (NM) | Connected |
| 11635 | `5GETANGL` | RC GET USER ANGLE | DDO-FX (NM) | Connected |
| 11657 | `5GETXPOS` | RC GET USER X POSITION | DDO-FX (NM) | Connected |
| 11671 | `5GETYPOS` | RC GET USER Y POSITION | DDO-FX (NM) | Connected |
| 11685 | `5VANIM` | RC DEFINE ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11761 | `5PLAYV` | RC PLAY ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11776 | `5STOPV` | RC STOP ANIMATED TEXTURE | DDO-FX (NM) | Connected |
| 11792 | `5CLSMAP` | RC CLEAR MAP | DDO-FX (NM) | Connected |
| 11811 | `5CHMAP` | RC CHANGE MAP | DDO-FX (NM) | Connected |
| 11901 | `5CHOBJ` | RC CHANGE OBJECT MAP | DDO-FX (NM) | Connected |
| 12046 | `5GETMAP` | RC GET MAP INFO | DDO-FX (NM) | Connected |
| 12101 | `5MAPPRO` | RC SET MAP PROFILE | DDO-FX (NM) | Connected |
| 12392 | `5CPYPRO` | RC COPY MAP PROFILE | DDO-FX (NM) | Connected |
| 12404 | `5FILPRO` | RC FILL MAP PROFILE | DDO-FX (NM) | Connected |
| 12414 | `5DELMAPPRO` | RC DEL MAP PROFILE | DDO-FX (NM) | Connected |
| 12433 | `5SAVE3D` | RC SAVE 3D ENVIRONMENT | DDO-FX (NM) | Connected. Saves the native MAP layout, texture atlas and animation intervals |
| 12453 | `5LOAD3D` | RC LOAD 3D ENVIRONMENT | DDO-FX (NM) | Connected. Restores the environment, renders imported textures and resumes active sprite sequences |
| 12466 | `5DREC` | RC RECORD 3D MOVEMENTS | DDO-FX (NM) | Connected |
| 12510 | `5DPLY` | RC PLAY 3D MOVEMENTS | DDO-FX (NM) | Connected |
| 12521 | `5DPLYR` | RC PLAY 3D MOVEMENTS FROM RESOURCE | DDO-FX (NM) | Connected |
| 12539 | `5DLYR` | RC DEFINE OBJECTS LAYER | DDO-FX (NM) | Connected. The desktop and window raycaster views draw the three shared object layers in order |
| 12570 | `23DINIT` | 3D OBJECT INIT | DDO-FX (NM) | Connected |
| 12829 | `23DINITL` | 3D OBJECT INIT LITE | DDO-FX (NM) | Connected |
| 12844 | `23DDEL` | 3D DELETE OBJECT | DDO-FX (NM) | Connected |
| 12856 | `23DYAW` | 3D ADJUST YAW | DDO-FX (NM) | Connected |
| 12888 | `23DROLL` | 3D ADJUST ROLL | DDO-FX (NM) | Connected |
| 12920 | `23DPITCH` | 3D ADJUST PITCH | DDO-FX (NM) | Connected |
| 12952 | `23DMOVE` | 3D ADJUST LOCATION | DDO-FX (NM) | Connected |
| 12981 | `23DDIST` | 3D ADJUST DISTANCE ZOOM | DDO-FX (NM) | Connected |
| 13003 | `23DFACE` | 3D ADD FACE | DDO-FX (NM) | Connected |
| 13026 | `23DPAINT` | 3D PAINT OBJECT | DDO-FX (NM) | Connected |
| 13059 | `23DWIRE` | 3D RENDER TYPE | DDO-FX (NM) | Connected |
| 13075 | `23DEQU` | 3D SET OBJECT | DDO-FX (NM) | Connected |
| 13094 | `23DMERGE` | 3D MERGE OBJECT | DDO-FX (NM) | Connected |
| 13111 | `23DMORPH` | 3D MORPH OBJECT | DDO-FX (NM) | Connected |
| 13144 | `23DPLAY` | 3D PLAY OBJECT | DDO-FX (NM) | Connected on the desktop and in graphic windows |
| 13158 | `5DEFGRID` | GD DEFINE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13236 | `5CHGGRID` | GD CHANGE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13286 | `5FILGRID` | GD FILL GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13322 | `5KILGRID` | GD KILL GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13330 | `5DRWGRID` | GD DRAW GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13382 | `5POSGRID` | GD POS GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13410 | `5MOVGRID` | GD MOVE GRID | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13432 | `5POSTILE` | GD POS TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13451 | `5MOVTILE` | GD MOVE TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13473 | `5KILTILE` | GD KILL TILE | DDO-FX (NM) | Connected on the desktop and window virtual pages |
| 13488 | `5SLEEPER` | SET SLEEP DISCONNECT | DDO-FX (NM) | Connected |
| 13544 | `5PING` | SET PING | DDO-FX (NM) | Connected |
| 13620 | `5ROTATE` | ROTATE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13703 | `5SCALE` | SCALE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13819 | `5ALIAS` | ANTI-ALIAS IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13917 | `5OUTLN` | OUTLINE IMAGE | DDO-FX (NM) | Connected on desktop and window pages |
| 13975 | `7ZMBKGRND` | ZMODEM BACKGROUND TRANSFERS | DDO-FX (NM) | Implemented |
| 14035 | `7ZMSTEALTH` | ZMODEM STEALTH MODE | DDO-FX (NM) | Connected for all 12 SDK families. Transfer tests exercise mouse fields, icons, list boxes, text fields, radio buttons, checkboxes, push buttons, mouse regions, timers, joystick input, keyboard macros and window close callbacks |
| 14120 | `7DEFBAR` | DEFINE MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14292 | `7MUSBAR` | ACTIVATE/DEACTIVATE MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14308 | `7CLRBAR` | CLEAR MUSIC BAR | DDO-FX (NM) | Connected on desktop and window pages |
| 14318 | `2GEXIT` | EXIT TO TEXT MODE | DDO-FX (NM) | Connected |
| 14638 | `2GWIN` | CREATE GRAPHIC WINDOW | DDO-FX (NM) | Scripted creation, frames, pages and stacking connected. Frame controls and linked icons connected |
| 14837 | `2DCGWIN` | DEFINE CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14898 | `2CGWIN` | CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14916 | `2VXY` | LOAD PCX (X,Y) Graphic Window VP | DDO-FX (NM) | Connected |
| 14936 | `2FGWIN` | FIND ACTIVE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14946 | `2HGWIN?` | IS GRAPHIC WINDOW HIDDEN | DDO-FX (NM) | Connected |
| 14954 | `2AGWIN?` | IS GRAPHIC WINDOW ACTIVE | DDO-FX (NM) | Connected |
| 14962 | `2VGWIN?` | IS GRAPHIC WINDOW CREATED | DDO-FX (NM) | Connected |
| 14970 | `2SGWIN?` | GET GRAPHIC WINDOW STATS | DDO-FX (NM) | Connected |
| 14980 | `2AGWIN` | ACTIVATE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 14995 | `2CHGWIN` | CHANGE GRAPHIC WINDOW | DDO-FX (NM) | Scripted geometry, visibility and title connected. Mouse drag, resize, zoom, restore and scroll enable flags connected. Return-to-icon connected |
| 15008 | `2CHVGPR` | CHANGE GRAPHIC WINDOW VGP REL | DDO-FX (NM) | Connected |
| 15024 | `2CHVGP` | CHANGE GRAPHIC WINDOW VGP | DDO-FX (NM) | Connected |
| 15034 | `2ZGWIN` | ZOOM/SHRINK GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15042 | `2RGWIN,WN` | RESTORE GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15049 | `2RAGWIN` | RESTORE ALL GRAPHIC WINDOWS | DDO-FX (NM) | Connected |
| 15056 | `2RGWIN` | REFRESH ALL GRAPHIC WINDOWS | DDO-FX (NM) | Connected |
| 15062 | `2RGWINI` | REFRESH GRAPHIC WINDOWS INTERIOR | DDO-FX (NM) | Connected |
| 15074 | `2ALERTGWIN` | ALERT GRAPHIC WINDOW | DDO-FX (NM) | Connected for primitives, PCX, visual/hidden page copies and scrolling, sprites, mouse fields and regions, push buttons, radio buttons, checkboxes, animations, collisions, icons, bitmap and VGA text, image transforms, tile grids, clocks, marquees, music bars, FLI/FLC playback, list boxes, editable text fields and graphic text windows |
| 15094 | `2CALMGWIN` | CALM GRAPHIC WINDOW | DDO-FX (NM) | Connected |
| 15101 | `2ICON` | CREATE ICON | DDO-FX (NM) | Connected |
| 15135 | `2MPICON` | ICON MOUSE POINTER | DDO-FX (NM) | Connected |
| 15161 | `2ICONO` | DEFINE ICON OPEN PROCEDURE | DDO-FX (NM) | Connected |
| 15174 | `2ICONC` | DEFINE ICON CLOSE PROCEDURE | DDO-FX (NM) | Connected |
| 15185 | `2ICOND` | DEFINE ICON DROP PROCEDURE | DDO-FX (NM) | Connected |
| 15203 | `2OICON` | EXECUTE OPEN ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15215 | `2CICON` | EXECUTE CLOSE ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15227 | `2DICON` | EXECUTE DROP ICON PROCEDURE | DDO-FX (NM) | Connected |
| 15235 | `2KICON` | KILL ICON | DDO-FX (NM) | Connected |
| 15243 | `2MICON` | MOVE ICON | DDO-FX (NM) | Connected |
| 15249 | `2GICON?` | GET ICON STATS | DDO-FX (NM) | Connected |
| 15265 | `2RICON` | RESTORE ICON | DDO-FX (NM) | Connected |
| 15273 | `2RAICON` | RESTORE ALL ICONS | DDO-FX (NM) | Connected |
| 15578 | `8CDIR` | CHECK FOR DIRECTORY | DDO-FX Pro. | Connected in Lite and Normal. A local peer checks existing and missing client directories |
| 15594 | `8CFILE` | CHECK FOR FILE | DDO-FX Pro. | Connected in Lite and Normal. A local peer checks file size, DOS timestamp, missing files and missing directories |
| 15614 | `8EXEC` | EXECUTE CLIENT | DDO-FX Pro. | Consume without execution |
