# DDO F/X 2.0 coverage

Select **DDO F/X 2.0** under **Graphics** in the BBS entry. The session starts in Lite mode. Plain text and ANSI sequences pass through to the selected emulation.

The inventory follows the 439 host-to-client command definitions in the 2.00 SDK. Line numbers refer to physical lines of `SDK20/docs/DDOFX.DOC` (SHA-256 `8ef566f37ee14cd4e7b349fccca23fd0d05cd5d63dda9f378ec7471a8b1c1e98`). The SDK also describes 45 UHS host commands.

Connected tests cover framing, detection, and ignore/hear across CP437 and UTF-8 sessions. Place MOD, S3M and CMF files in the entry’s `DDOFX/MUSIC` cache directory. The `m` command selects the decoder by file content and plays through SyncTERM’s stereo mixer. A missing file leaves the previous song playing. MOD and S3M use libopenmpt. CMF 1.0 and 1.1 files with an embedded instrument bank use the OPL2 synthesizer and the file’s tick rate. The SDK query `MICMF` reports whether CMF is playing. Tests cover melodic and rhythm playback, loop timing, song replacement, and a connected BBS selecting a CMF by content. In Normal mode, `7DEFBAR` maps each of the first 16 module tracks to a directional volume bar. The bars use libopenmpt’s per-channel level and update at 20 Hz during idle connections and script waits. Disabling the bars freezes them. Clearing restores the exposed areas from the hidden graphics page.

After a ZMODEM batch finishes, SyncTERM installs up to 50 Lite/Normal command ZIPs into that BBS’s DDOFX cache. The package letter selects MUSIC (M), BBSTHEME (T or R), FONTS (F), SCRIPTS (S), or THEMES (P). Stored and deflated members retain their timestamps. A successful install removes the downloaded command ZIP. Failed installs and ordinary downloads keep their files. In BBSTHEME packages, DDOFX.INI can add or delete resource members and request one RealPlay sample per batch. Component updates preserve the container timestamp and write the SDK lock marker. In Normal mode, `7ZMBKGRND,1` keeps the scene running during a ZMODEM download and shows progress on the bottom row through installation. The progress overlay leaves the underlying visual page available for animation. `7ZMSTEALTH,1` marks subsequently created keyboard macros, mouse fields and regions, desktop buttons and timed events for local use during reception. ZMODEM retains the connection until the batch ends. The remaining interactive object families are listed below.

BBS sound themes, resident sound bites and big-sample profiles use signed 8-bit PCM from the entry’s `DDOFX/BBSTHEME` resources. `MTALK` assembles loose RealPlay files from named `.SND` members, supports the four pause lengths, and can append, replace or delete a phrase. RealPlay streams loose sample files from the same directory. `dACARD` reports the SDK’s 16-bit stereo capability `SB 16` when SyncTERM has audio output configured, and `NONE` otherwise. The mixer advances the samples at the requested frequency alongside MOD or S3M playback. A connected peer checks `dSOUNDON` and starts RealPlay from the cache. The test reads the resulting stereo frames from a pull-only audio device.

An online pair of `LIGMODE` commands enters Normal mode on a pixel display. The visible page is 640×480 with 256 palette entries. The work, background and hidden pages store pixel indices. Whole-page copies, rectangular cuts anchored at the lower left, transparent cuts, work-page drawing, vertical scrolling and palette changes are available from a connection. Ordinary ANSI goes to an 80×100 virtual text page with the top 30 rows visible. `2GEXIT` restores the Lite screen, cursor and palette. Basic fade-out saves the active palette, fades to black and clears the visual and hidden pages. Advanced fade-out preserves page pixels and blends an inclusive palette range toward a selected register. Fade-in returns the palette to the saved values. Named text windows and window scrolling remain pending.

Sprites can be captured from any of the four graphics pages. The SDK repeats the background spelling for hidden-page capture. SyncTERM accepts `2SH` for the hidden page. Simple sprite definitions and animation frames share the SDK stack. `2DSS` restores the buffered regions from the background, draws the current frame in stack order, then publishes the regions to the visual page.

Work, background and visual pages can load version-5, single-plane, 8-bit PCX artwork up to 640×480. Loading artwork installs its palette across all pages. Each page can save a session-owned PCX file and restore it to any of the three pages. The files survive a return to Lite mode and close at disconnect.

Lite and Normal accept complete 4,096-byte VGA fonts from `DDOFX/FONTS` with lowercase `f`. The selected font applies to console text and all four desktop `2XT` destinations. `dGETFONT` reports its filename. SyncTERM supplies its built-in VGA font for `IBMSTD.FNT` when that file is absent.

A BBS can load legacy and four-bank BMF fonts into 32 slots with `2F` and switch between loaded fonts with `2FSELECT`. On all four desktop pages, `2X` scales a variable-width string to the specified rectangle, draws an optional one-pixel outline, and rotates it about the rectangle center.

The four desktop pages support lower-left anchored image scaling, rotation between pages with a transparent index and independent pivots, palette-based anti-aliasing, and single-index outlines.

In Normal mode the BBS can show or hide the mouse pointer, query its position and three logical buttons, and read separate press and release counts and positions for each button. The BBS can position the DDO pointer with `3MMOV` and confine it to a rectangle with `3MLIM`. SyncTERM accepts both the SDK spelling `MSPD` and the mouse-family spelling `3MSPD`. Relative to the default rates of 8 horizontally and 16 vertically, doubling an axis rate halves its travel. Motion is measured in desktop raster pixels, with fractional movement carried between samples. Up to 300 desktop regions share one table. Overlapping regions run in slot order. Each macro finishes before its return string is sent. Held repeatable regions run at 50 ms intervals during idle input. Non-repeating regions wait for all buttons to be released.

A marquee reads up to 20,000 bytes from the assigned M resource and uses the selected VGA text font. `5SHOWMQ` paints the initial strip on the work page. Its timer scrolls the strip one pixel to the right per frame on the visual page, following the direction stated in the SDK. `5STOPMQ` preserves the current position, and `5KILLMQ` releases the slot.

The BBS can create up to 300 desktop push buttons with `5CPUSHB`. Creation draws the up sprite on the work page. A matching key or mouse combination draws the down sprite on the visual page for three clock ticks, restores the up sprite, then runs the macro and sends the return string. The pass flag controls whether a keyboard activation continues to the next binding and the connection.

Desktop radio buttons and checkboxes each have 300 slots. A key or left click changes the selection. An explicit execute command runs the selected members of the addressed exclusive or multiple-selection group in slot order. Each macro finishes before its return string is sent.

Desktop hover fields use a separate table of 300 slots. The highest matching slot runs its macro, sends its return string and selects its static or animated pointer. As specified in the SDK, leaving a field for uncovered space preserves the remembered slot. Re-entering it fires again after a different field has won or the BBS has sent `3MFFRS`.

The host can load either MPT pointer format from the assigned M resource. A selected image keeps its own hotspot and AND/XOR masks when the BBS replaces or clears its list entry. The bitmap presenter applies those masks to the displayed palette indices, leaving the graphics pages available for capture and copying under the pointer. The 100 static slots and 100 animation profiles support one-shot, looping and oscillating playback.

Normal mode supports a tile map of up to 150 × 150 cells. The host can fill either layer, move the viewport, move or remove foreground tiles, and animate consecutive sprite frames. Grid assignments become visible on a draw. Movement redraws immediately.

Loose-file queries read the entry’s `DDOFX/MUSIC`, `DDOFX/FONTS`, and `DDOFX/BBSTHEME` directories. Professional queries use `DDOFX/TERMS/<client>`. Names use DOS-style case matching. The timestamp is local modification time with DOS two-second precision. `8EXEC` consumes the command without starting a program.

Resource assignment selects the M container in `DDOFX/BBSTHEME` for local scripts and bitmap fonts and the S container for indexed PCX artwork. Scripts up to 40,000 bytes can call other scripts and render DOS text. A resource entry query tests the member name and component ID. Conditional scripts can also read that ID.

In Normal mode, `2STORES` saves the current sprite slots as paired DAT and RAW files in the entry’s BBSTHEME directory. `2LOADS` validates both files before replacing the live slots. Its erase flag removes the pair after a successful load. A bank preserves slot order, indexed pixel rows and opaque allocation tails.

In Lite and Normal mode, `2RESET` unlocks the keyboard and clears key bindings, macro definitions, variables, return strings, query capture, timed events and big-sample profiles. The current script continues after the command. In Normal mode it also clears all four graphics pages and releases sprites, simple animations, bitmap fonts and music bars. The assigned resource prefix, music, theme sounds, palette and saved page files survive a scene reset.

In Lite and Normal, `4MKEY` binds DOS scan/character pairs or the all-keys wildcard. Matching slots run in order, with each macro completing before its return bytes and any passed keystroke. The DDO key lock admits programmed bindings and consumes ordinary keyboard input. SyncTERM processes its local shortcuts before graphics input.

Lite scripts can define 32 ten-line macros, 300 return strings, and 1,000 typed variables. Local macro calls, repeats and waits preserve the order of the wire stream. Variable capture redirects automatic query results. Configured return strings can send binary bytes. Conditional macros can compare user variables, identity, music state and loose-file metadata. Additional predefined system operands await their respective query implementations.

| SDK line | Definition | Domain | Coverage |
| ---: | --- | --- | --- |
| 575 | LOAD FONT | DDO-FX LITE | Connected |
| 616 | LOAD MUSIC | DDO-FX LITE | MOD, S3M, CMF with embedded instruments |
| 635 | LOAD BBS THEME | DDO-FX LITE | Connected |
| 693 | LOAD SOUND BITES LIBRARY | DDO-FX LITE | Connected |
| 767 | LOAD TERM THEME | DDO-FX LITE | Pending |
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
| 1611 | DETECT USER MEMORY | DDO-FX LITE | Pending |
| 1634 | DETECT USER MEMORY & REACT | DDO-FX LITE | Pending |
| 1729 | DETECT USER DRIVE SIZE | DDO-FX LITE | Pending |
| 1755 | DETECT GRAPHICS ADAPTER TYPE | DDO-FX LITE | Connected |
| 1770 | DETECT AUDIO CARD | DDO-FX LITE | Connected |
| 1798 | DETECT Machine Speed | DDO-FX LITE | Pending |
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
| 2874 | TEST FOR JOYSTICK | DDO-LITE | Pending |
| 2893 | CALIBRATE JOYSTICK | DDO-LITE | Pending |
| 2913 | DEFINE JOYSTICK | DDO-LITE | Pending |
| 3067 | CREATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3161 | ACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3174 | DEACTIVATE BACKGROUND EVENT | DDO-LITE | Connected |
| 3187 | DEFINE RETURN STRING | DDO-LITE | Connected |
| 3226 | SEND RETURN STRING | DDO-LITE | Connected |
| 3237 | TEXT FADE OUT | DDO-FX LITE | Pending |
| 3264 | TEXT FADE IN | DDO-FX LITE | Pending |
| 3273 | SLIDE SCREEN UP | DDO-FX LITE | Pending |
| 3294 | SLIDE SCREEN DOWN | DDO-FX LITE | Pending |
| 3307 | BOUNCE SCREEN DOWN | DDO-FX LITE | Pending |
| 3323 | DISPLAY RAW ANSI | DDO-FX LITE | Pending |
| 3365 | SET FULL SCREEN MODE | DDO-FX LITE | Pending |
| 3400 | RESTORE FULL SCREEN MODE | DDO-FX LITE | Pending |
| 3416 | RUN LOCAL SCRIPT | DDO-FX LITE | Connected |
| 3501 | DEFINE SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3604 | LOCK KEYS | DDO-FX LITE | Connected |
| 3650 | UNLOCK KEYS | DDO-FX LITE | Connected |
| 3661 | PAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events |
| 3710 | UNPAUSE BACKGROUND TASKS | DDO-FX LITE | Connected for timed events |
| 3720 | IGNORE COMMANDS | DDO-FX LITE | Connected |
| 3767 | HEAR COMMANDS | DDO-FX LITE | Connected |
| 3782 | ALL CAPS SYSTEM VARIABLE | DDO-FX LITE | Connected |
| 3796 | GET TEXT FIELD INFO FULL | DDO-FX LITE | Pending |
| 3824 | GET TEXT FIELD INFO | DDO-FX LITE | Pending |
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
| 4322 | IF VAR DO MACRO | DDO-FX LITE | Variables and implemented system operands |
| 4547 | REPEAT MACRO UNTIL VAR | DDO-FX LITE | Variables and implemented system operands |
| 4604 | REPEAT MACRO | DDO-FX LITE | Connected |
| 4654 | INFO SAVE | DDO-FX LITE | Pending |
| 4728 | INFO LOAD | DDO-FX LITE | Pending |
| 4777 | LAUNCH TEXT EDITOR | DDO-FX LITE | Pending |
| 4821 | RETRIEVE TEXT FILE | DDO-FX LITE | Pending |
| 4847 | WRITE TEXT FILE | DDO-FX LITE | Pending |
| 4886 | PRINT TEXT FILE | DDO-FX LITE | Pending |
| 4940 | LABEL | DDO-FX LITE | Connected |
| 4955 | RESET ENVIRONMENT | DDO-FX LITE | Connected for implemented scene objects |
| 5436 | DETECT MOUSE | DDO-FX (NM) | Connected |
| 5457 | OPEN GRAPHIC TEXT WINDOW | DDO-FX (NM) | Pending |
| 5780 | GOTO CURSOR | DDO-FX (NM) | Pending |
| 5819 | CLOSE WINDOW | DDO-FX (NM) | Pending |
| 5855 | HIDE WINDOW | DDO-FX (NM) | Pending |
| 5866 | UNHIDE WINDOW | DDO-FX (NM) | Pending |
| 5874 | ACTIVATE WINDOW | DDO-FX (NM) | Pending |
| 5884 | SWAP WINDOWS | DDO-FX (NM) | Pending |
| 5924 | POSITION VTP ORIGIN | DDO-FX (NM) | Pending |
| 5956 | POSITION VTP ORIGIN RELATIVE | DDO-FX (NM) | Pending |
| 5982 | GETPOS OF GRAPHIC TEXT WINDOW | DDO-FX (NM) | Pending |
| 6006 | CHANGE GRAPHIC TEXT WINDOW | DDO-FX (NM) | Pending |
| 6035 | SCALE GRAPHIC TEXT FONTS | DDO-FX (NM) | Pending |
| 6100 | SET GRAPHIC TEXT WINDOW COLUMNS | DDO-FX (NM) | Pending |
| 6131 | LOAD PCX TO BACKGROUND PAGE | DDO-FX (NM) | Connected |
| 6163 | LOAD PCX TO WORK PAGE | DDO-FX (NM) | Connected |
| 6179 | LOAD PCX TO VISUAL PAGE | DDO-FX (NM) | Desktop pages; graphic window refresh pending |
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
| 6544 | LOAD VISUAL PAGE PCX | DDO-FX (NM) | Desktop pages; graphic window refresh pending |
| 6556 | LOAD WORK PAGE PCX | DDO-FX (NM) | Desktop pages; graphic window refresh pending |
| 6568 | LOAD BACKGROUND PAGE PCX | DDO-FX (NM) | Desktop pages; graphic window refresh pending |
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
| 6939 | DEFINE ANIM PROFILE | DDO-FX (NM) | Pending |
| 7151 | DEFINE ANIM SOUND | DDO-FX (NM) | Pending |
| 7186 | PLAY ANIM PROFILE | DDO-FX (NM) | Pending |
| 7219 | STOP ANIM PROFILE | DDO-FX (NM) | Pending |
| 7235 | CHANGE ANIM PROFILE ON THE FLY | DDO-FX (NM) | Pending |
| 7246 | ANIM COLLISION PROFILE | DDO-FX (NM) | Pending |
| 7339 | RESOLUTION ANIMATOR | DDO-FX (NM) | Pending |
| 7383 | DEFINE MOTION PROFILE | DDO-FX (NM) | Pending |
| 7466 | DEFINE MOTION FRAME CONTROL | DDO-FX (NM) | Pending |
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
| 7919 | DO PRINT | DDO-FX (NM) | Desktop pages; low-memory text fallback and virtual windows pending |
| 8058 | DO PRINT TEXT FONT | DDO-FX (NM) | Connected on the desktop |
| 8110 | DO PRINT TEXT FONT WP | DDO-FX (NM) | Connected on the desktop |
| 8118 | DO PRINT TEXT FONT BP | DDO-FX (NM) | Connected on the desktop |
| 8127 | DO PRINT TEXT FONT HP | DDO-FX (NM) | Connected on the desktop |
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
| 8374 | CHANGE VIEW PORT ORIGIN | DDO-FX (NM) | Pending |
| 8394 | SHAKE SCREEN | DDO-FX (NM) | Pending |
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
| 8656 | VERTICAL SCROLL | DDO-FX (NM) | Connected |
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
| 8968 | MOUSE REGION | DDO-FX (NM) | Desktop regions |
| 9189 | MOUSE REGION NON REPEAT | DDO-FX (NM) | Desktop regions |
| 9211 | MOUSE FIELD | DDO-FX (NM) | Desktop hover fields |
| 9292 | MOUSE FIELD SWAP | DDO-FX (NM) | Desktop hover fields |
| 9302 | MOUSE FIELD FRESHEN | DDO-FX (NM) | Desktop hover fields |
| 9317 | CREATE RADIO BUTTON | DDO-FX (NM) | Desktop controls |
| 9481 | EXECUTE RADIO BUTTON | DDO-FX (NM) | Desktop controls |
| 9548 | SWAP RADIO BUTTON | DDO-FX (NM) | Desktop controls |
| 9560 | DISABLE/ENABLE RADIO BUTTON | DDO-FX (NM) | Desktop controls |
| 9576 | CREATE CHECK BOX | DDO-FX (NM) | Desktop controls |
| 9679 | EXECUTE CHECK BOX | DDO-FX (NM) | Desktop controls |
| 9688 | SWAP CHECK BOX | DDO-FX (NM) | Desktop controls |
| 9697 | DISABLE/ENABLE CHECK BOX | DDO-FX (NM) | Desktop controls |
| 9706 | CREATE PUSH BUTTON | DDO-FX (NM) | Desktop mouse and keyboard |
| 9792 | CREATE TEXT FIELD | DDO-FX (NM) | Pending |
| 9973 | SHOW TEXT FIELD | DDO-FX (NM) | Pending |
| 9981 | ACTIVATE TEXT FIELD | DDO-FX (NM) | Pending |
| 9992 | EXECUTE TEXT FIELD | DDO-FX (NM) | Pending |
| 10008 | SWAP TEXT FIELD | DDO-FX (NM) | Pending |
| 10016 | CREATE LIST BOX | DDO-FX (NM) | Pending |
| 10143 | MODIFY LIST ITEM | DDO-FX (NM) | Pending |
| 10197 | DRAW LIST BOX | DDO-FX (NM) | Pending |
| 10211 | SHOW LIST BOX | DDO-FX (NM) | Pending |
| 10224 | EXECUTE LIST BOX | DDO-FX (NM) | Pending |
| 10237 | KILL LIST BOX | DDO-FX (NM) | Pending |
| 10248 | DEACTIVATE LIST BOX | DDO-FX (NM) | Pending |
| 10262 | REACTIVATE LIST BOX | DDO-FX (NM) | Pending |
| 10274 | ADJUST LIST | DDO-FX (NM) | Pending |
| 10341 | ADJUST HIGHLIGHT | DDO-FX (NM) | Pending |
| 10378 | TOGGLE SELECTION | DDO-FX (NM) | Pending |
| 10399 | CREATE MARQUEE | DDO-FX (NM) | Connected |
| 10471 | SHOW MARQUEE | DDO-FX (NM) | Connected |
| 10485 | STOP MARQUEE | DDO-FX (NM) | Connected |
| 10498 | KILL MARQUEE | DDO-FX (NM) | Connected |
| 10511 | OPEN FLI/FLC | DDO-FX (NM) | Pending |
| 10600 | PAUSE FLI/FLC | DDO-FX (NM) | Pending |
| 10620 | UNPAUSE FLI/FLC | DDO-FX (NM) | Pending |
| 10632 | STOP FLI/FLC | DDO-FX (NM) | Pending |
| 10648 | START ANALOG CLOCK | DDO-FX (NM) | Pending |
| 10703 | SHOW ANALOG CLOCK | DDO-FX (NM) | Pending |
| 10715 | STOP ANALOG CLOCK | DDO-FX (NM) | Pending |
| 10725 | START TEXT CLOCK | DDO-FX (NM) | Pending |
| 10772 | PAUSE TEXT CLOCK | DDO-FX (NM) | Pending |
| 10782 | UNPAUSE TEXT CLOCK | DDO-FX (NM) | Pending |
| 10794 | KILL TEXT CLOCK | DDO-FX (NM) | Pending |
| 10806 | CREATE VIRTUAL LANDSCAPE | DDO-FX (NM) | Pending |
| 10890 | VR CLOUDS | DDO-FX (NM) | Pending |
| 10911 | VR SKY | DDO-FX (NM) | Pending |
| 10925 | VR LAND POSITION | DDO-FX (NM) | Pending |
| 10935 | VR CLOUD POSITION | DDO-FX (NM) | Pending |
| 10942 | VR DRAW MAP | DDO-FX (NM) | Pending |
| 10976 | VR DRAW MAP ON WORK PAGE | DDO-FX (NM) | Pending |
| 10984 | VR GET MAP | DDO-FX (NM) | Pending |
| 11005 | VR GET MAP ON WORK PAGE | DDO-FX (NM) | Pending |
| 11012 | VR CHANGE SPEED | DDO-FX (NM) | Pending |
| 11026 | VR CHANGE DIRECTION | DDO-FX (NM) | Pending |
| 11036 | VR CHANGE X | DDO-FX (NM) | Pending |
| 11046 | VR CHANGE Y | DDO-FX (NM) | Pending |
| 11056 | VR CHANGE Z | DDO-FX (NM) | Pending |
| 11066 | VR CHANGE DETAIL | DDO-FX (NM) | Pending |
| 11079 | VR MODIFY SPEED | DDO-FX (NM) | Pending |
| 11093 | VR MODIFY DIRECTION | DDO-FX (NM) | Pending |
| 11100 | VR MODIFY X | DDO-FX (NM) | Pending |
| 11107 | VR MODIFY Y | DDO-FX (NM) | Pending |
| 11114 | VR MODIFY Z | DDO-FX (NM) | Pending |
| 11121 | VR MODIFY DETAIL | DDO-FX (NM) | Pending |
| 11128 | VR GET USER X | DDO-FX (NM) | Pending |
| 11138 | VR GET USER Y | DDO-FX (NM) | Pending |
| 11148 | VR GET USER Z | DDO-FX (NM) | Pending |
| 11158 | VR GET USER DIRECTION | DDO-FX (NM) | Pending |
| 11167 | VR ANIMATE | DDO-FX (NM) | Pending |
| 11176 | RC 3D RAYCASTING ENGINE | DDO-FX (NM) | Pending |
| 11262 | RC 3D SCALE | DDO-FX (NM) | Pending |
| 11291 | RC 3D MAZE GENERATOR | DDO-FX (NM) | Pending |
| 11331 | RC LOAD 3D MAP DEFINITION | DDO-FX (NM) | Pending |
| 11351 | RC ROTATE PERSPECTIVE | DDO-FX (NM) | Pending |
| 11381 | RC MOVE 3D FORWARD/BACKWARDS | DDO-FX (NM) | Pending |
| 11409 | RC DUMP MAP | DDO-FX (NM) | Pending |
| 11440 | RC MODIFY DUMP MAP | DDO-FX (NM) | Pending |
| 11466 | RC CHANGE TEXTURE LIBRARY | DDO-FX (NM) | Pending |
| 11516 | RC CHANGE OBJ TEXTURE LIBRARY | DDO-FX (NM) | Pending |
| 11574 | RC SET USER X POSITION | DDO-FX (NM) | Pending |
| 11595 | RC SET USER Y POSITION | DDO-FX (NM) | Pending |
| 11613 | RC SET USER ANGLE | DDO-FX (NM) | Pending |
| 11635 | RC GET USER ANGLE | DDO-FX (NM) | Pending |
| 11657 | RC GET USER X POSITION | DDO-FX (NM) | Pending |
| 11671 | RC GET USER Y POSITION | DDO-FX (NM) | Pending |
| 11685 | RC DEFINE ANIMATED TEXTURE | DDO-FX (NM) | Pending |
| 11761 | RC PLAY ANIMATED TEXTURE | DDO-FX (NM) | Pending |
| 11776 | RC STOP ANIMATED TEXTURE | DDO-FX (NM) | Pending |
| 11792 | RC CLEAR MAP | DDO-FX (NM) | Pending |
| 11811 | RC CHANGE MAP | DDO-FX (NM) | Pending |
| 11901 | RC CHANGE OBJECT MAP | DDO-FX (NM) | Pending |
| 12046 | RC GET MAP INFO | DDO-FX (NM) | Pending |
| 12101 | RC SET MAP PROFILE | DDO-FX (NM) | Pending |
| 12392 | RC COPY MAP PROFILE | DDO-FX (NM) | Pending |
| 12404 | RC FILL MAP PROFILE | DDO-FX (NM) | Pending |
| 12414 | RC DEL MAP PROFILE | DDO-FX (NM) | Pending |
| 12433 | RC SAVE 3D ENVIRONMENT | DDO-FX (NM) | Pending |
| 12453 | RC LOAD 3D ENVIRONMENT | DDO-FX (NM) | Pending |
| 12466 | RC RECORD 3D MOVEMENTS | DDO-FX (NM) | Pending |
| 12510 | RC PLAY 3D MOVEMENTS | DDO-FX (NM) | Pending |
| 12521 | RC PLAY 3D MOVEMENTS FROM RESOURCE | DDO-FX (NM) | Pending |
| 12539 | RC DEFINE OBJECTS LAYER | DDO-FX (NM) | Pending |
| 12570 | 3D OBJECT INIT | DDO-FX (NM) | Pending |
| 12829 | 3D OBJECT INIT LITE | DDO-FX (NM) | Pending |
| 12844 | 3D DELETE OBJECT | DDO-FX (NM) | Pending |
| 12856 | 3D ADJUST YAW | DDO-FX (NM) | Pending |
| 12888 | 3D ADJUST ROLL | DDO-FX (NM) | Pending |
| 12920 | 3D ADJUST PITCH | DDO-FX (NM) | Pending |
| 12952 | 3D ADJUST LOCATION | DDO-FX (NM) | Pending |
| 12981 | 3D ADJUST DISTANCE ZOOM | DDO-FX (NM) | Pending |
| 13003 | 3D ADD FACE | DDO-FX (NM) | Pending |
| 13026 | 3D PAINT OBJECT | DDO-FX (NM) | Pending |
| 13059 | 3D RENDER TYPE | DDO-FX (NM) | Pending |
| 13075 | 3D SET OBJECT | DDO-FX (NM) | Pending |
| 13094 | 3D MERGE OBJECT | DDO-FX (NM) | Pending |
| 13111 | 3D MORPH OBJECT | DDO-FX (NM) | Pending |
| 13144 | 3D PLAY OBJECT | DDO-FX (NM) | Pending |
| 13158 | GD DEFINE GRID | DDO-FX (NM) | Connected on the desktop |
| 13236 | GD CHANGE GRID | DDO-FX (NM) | Connected on the desktop |
| 13286 | GD FILL GRID | DDO-FX (NM) | Connected on the desktop |
| 13322 | GD KILL GRID | DDO-FX (NM) | Connected on the desktop |
| 13330 | GD DRAW GRID | DDO-FX (NM) | Connected on the desktop |
| 13382 | GD POS GRID | DDO-FX (NM) | Connected on the desktop |
| 13410 | GD MOVE GRID | DDO-FX (NM) | Connected on the desktop |
| 13432 | GD POS TILE | DDO-FX (NM) | Connected on the desktop |
| 13451 | GD MOVE TILE | DDO-FX (NM) | Connected on the desktop |
| 13473 | GD KILL TILE | DDO-FX (NM) | Connected on the desktop |
| 13488 | SET SLEEP DISCONNECT | DDO-FX (NM) | Pending |
| 13544 | SET PING | DDO-FX (NM) | Pending |
| 13620 | ROTATE IMAGE | DDO-FX (NM) | Connected on the desktop |
| 13703 | SCALE IMAGE | DDO-FX (NM) | Connected on the desktop |
| 13819 | ANTI-ALIAS IMAGE | DDO-FX (NM) | Connected on the desktop |
| 13917 | OUTLINE IMAGE | DDO-FX (NM) | Connected on the desktop |
| 13975 | ZMODEM BACKGROUND TRANSFERS | DDO-FX (NM) | Implemented |
| 14035 | ZMODEM STEALTH MODE | DDO-FX (NM) | Partial: keyboard macros, fields, regions, push/radio/check buttons and timed events. Icons, pick lists, text fields, joystick and window close pending |
| 14120 | DEFINE MUSIC BAR | DDO-FX (NM) | Desktop pages; virtual windows pending |
| 14292 | ACTIVATE/DEACTIVATE MUSIC BAR | DDO-FX (NM) | Desktop pages; virtual windows pending |
| 14308 | CLEAR MUSIC BAR | DDO-FX (NM) | Desktop pages; virtual windows pending |
| 14318 | EXIT TO TEXT MODE | DDO-FX (NM) | Connected |
| 14638 | CREATE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 14837 | DEFINE CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 14898 | CLOSE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 14916 | LOAD PCX (X,Y) Graphic Window VP | DDO-FX (NM) | Pending |
| 14936 | FIND ACTIVE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 14946 | IS GRAPHIC WINDOW HIDDEN | DDO-FX (NM) | Pending |
| 14954 | IS GRAPHIC WINDOW ACTIVE | DDO-FX (NM) | Pending |
| 14962 | IS GRAPHIC WINDOW CREATED | DDO-FX (NM) | Pending |
| 14970 | GET GRAPHIC WINDOW STATS | DDO-FX (NM) | Pending |
| 14980 | ACTIVATE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 14995 | CHANGE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 15008 | CHANGE GRAPHIC WINDOW VGP REL | DDO-FX (NM) | Pending |
| 15024 | CHANGE GRAPHIC WINDOW VGP | DDO-FX (NM) | Pending |
| 15034 | ZOOM/SHRINK GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 15042 | RESTORE GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 15049 | RESTORE ALL GRAPHIC WINDOWS | DDO-FX (NM) | Pending |
| 15056 | REFRESH ALL GRAPHIC WINDOWS | DDO-FX (NM) | Pending |
| 15062 | REFRESH GRAPHIC WINDOWS INTERIOR | DDO-FX (NM) | Pending |
| 15074 | ALERT GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 15094 | CALM GRAPHIC WINDOW | DDO-FX (NM) | Pending |
| 15101 | CREATE ICON | DDO-FX (NM) | Pending |
| 15135 | ICON MOUSE POINTER | DDO-FX (NM) | Pending |
| 15161 | DEFINE ICON OPEN PROCEDURE | DDO-FX (NM) | Pending |
| 15174 | DEFINE ICON CLOSE PROCEDURE | DDO-FX (NM) | Pending |
| 15185 | DEFINE ICON DROP PROCEDURE | DDO-FX (NM) | Pending |
| 15203 | EXECUTE OPEN ICON PROCEDURE | DDO-FX (NM) | Pending |
| 15215 | EXECUTE CLOSE ICON PROCEDURE | DDO-FX (NM) | Pending |
| 15227 | EXECUTE DROP ICON PROCEDURE | DDO-FX (NM) | Pending |
| 15235 | KILL ICON | DDO-FX (NM) | Pending |
| 15243 | MOVE ICON | DDO-FX (NM) | Pending |
| 15249 | GET ICON STATS | DDO-FX (NM) | Pending |
| 15265 | RESTORE ICON | DDO-FX (NM) | Pending |
| 15273 | RESTORE ALL ICONS | DDO-FX (NM) | Pending |
| 15578 | CHECK FOR DIRECTORY | DDO-FX Pro. | Connected |
| 15594 | CHECK FOR FILE | DDO-FX Pro. | Connected |
| 15614 | EXECUTE CLIENT | DDO-FX Pro. | Consume without execution |
