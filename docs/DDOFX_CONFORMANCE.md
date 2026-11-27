# DDO F/X 2.0 coverage

Select **DDO F/X 2.0** under **Graphics** in the BBS entry. The session starts in Lite mode. Plain text and ANSI sequences pass through to the selected emulation.

The inventory follows the 439 host-to-client command definitions in the 2.00 SDK. Line numbers refer to physical lines of `SDK20/docs/DDOFX.DOC` (SHA-256 `8ef566f37ee14cd4e7b349fccca23fd0d05cd5d63dda9f378ec7471a8b1c1e98`). The SDK also describes 45 UHS host commands.

Connected tests cover framing, detection, and ignore/hear across CP437 and UTF-8 sessions. Place MOD and S3M files in the entry’s `DDOFX/MUSIC` cache directory. The `m` command selects the decoder by file content and plays through SyncTERM’s stereo mixer. A missing file leaves the previous song playing. The headless music test drives DDO commands and checks PCM from both formats. Music bars, CMF, and ZIP installation are pending.

Loose-file queries read the entry’s `DDOFX/MUSIC`, `DDOFX/FONTS`, and `DDOFX/BBSTHEME` directories. Professional queries use `DDOFX/TERMS/<client>`. Names use DOS-style case matching. The timestamp is local modification time with DOS two-second precision. `8EXEC` consumes the command without starting a program.

Resource assignment selects the M container in `DDOFX/BBSTHEME` for local scripts. Scripts up to 40,000 bytes can call other scripts and render DOS text. A resource entry query tests the member name and component ID. Conditional scripts can also read that ID.

Lite scripts can define 32 ten-line macros, 300 return strings, and 1,000 typed variables. Local macro calls, repeats and waits preserve the order of the wire stream. Variable capture redirects automatic query results. Configured return strings can send binary bytes. Conditional macros can compare user variables, identity, music state and loose-file metadata. Additional predefined system operands await their respective query implementations.

| SDK line | Definition | Domain | Coverage |
| ---: | --- | --- | --- |
| 575 | LOAD FONT | DDO-FX LITE | Pending |
| 616 | LOAD MUSIC | DDO-FX LITE | MOD and S3M |
| 635 | LOAD BBS THEME | DDO-FX LITE | Pending |
| 693 | LOAD SOUND BITES LIBRARY | DDO-FX LITE | Pending |
| 767 | LOAD TERM THEME | DDO-FX LITE | Pending |
| 828 | PLAY SAMPLE | DDO-FX LITE | Pending |
| 858 | PLAY SOUND BITE SAMPLE | DDO-FX LITE | Pending |
| 868 | PLAY SAMPLE At FREQUENCY | DDO-FX LITE | Pending |
| 882 | PLAY SOUND BITE SAMPLE At FREQUENCY | DDO-FX LITE | Pending |
| 894 | LOAD BIG SAMPLE | DDO-LITE | Pending |
| 993 | DEFINE BIG SAMPLE | DDO-LITE | Pending |
| 1055 | ADD BIG SAMPLE PART | DDO-LITE | Pending |
| 1086 | UNLOAD BIG SAMPLE | DDO-LITE | Pending |
| 1102 | PLAY BIG SAMPLE | DDO-LITE | Pending |
| 1124 | CHECK BIG SAMPLE | DDO-LITE | Pending |
| 1167 | PLAY REALPLAY SAMPLE | DDO-FX LITE | Pending |
| 1207 | TEXT TO SPEECH | DDO-FX LITE | Pending |
| 1417 | MODIFY MUSIC PATTERN | DDO-FX LITE | Connected |
| 1447 | PAUSE RESUME MOD/S3M | DDO-FX LITE | Connected |
| 1459 | SET VOLUME | DDO-FX LITE | Connected |
| 1476 | MODIFY VOLUME | DDO-FX LITE | Connected |
| 1491 | GET VOLUME | DDO-FX LITE | Connected |
| 1509 | IS CMF PLAYING | DDO-FX LITE | Pending |
| 1528 | WAIT | DDO-FX LITE | Connected |
| 1544 | GOTO DDO-FX (NORMAL MODE) | DDO-FX LITE | Pending |
| 1573 | DETECT LITERM | DDO-FX LITE | Connected |
| 1611 | DETECT USER MEMORY | DDO-FX LITE | Pending |
| 1634 | DETECT USER MEMORY & REACT | DDO-FX LITE | Pending |
| 1729 | DETECT USER DRIVE SIZE | DDO-FX LITE | Pending |
| 1755 | DETECT GRAPHICS ADAPTER TYPE | DDO-FX LITE | Pending |
| 1770 | DETECT AUDIO CARD | DDO-FX LITE | Pending |
| 1798 | DETECT Machine Speed | DDO-FX LITE | Pending |
| 1851 | DETECT VIDEO MODE | DDO-FX LITE | Pending |
| 1880 | DETECT FOR MUSIC FILE | DDO-FX LITE | Connected |
| 1921 | DETECT FOR THEME FILE | DDO-FX LITE | Connected |
| 1929 | ASSIGN RESOURCE FILE | DDO-FX LITE | Connected |
| 2249 | DETECT FOR RESOURCE FILE | DDO-FX LITE | Connected |
| 2262 | DETECT FOR RESOURCE ENTRY | DDO-FX LITE | Connected |
| 2320 | DETECT FOR FONT FILE | DDO-FX LITE | Connected |
| 2328 | DETECT MUSIC ON/OFF | DDO-FX LITE | Connected |
| 2345 | DETECT SOUND F/X ON/OFF | DDO-FX LITE | Pending |
| 2367 | DETECT MUSIC PLAYING | DDO-FX LITE | MOD and S3M |
| 2389 | DETECT THEME LOADED | DDO-FX LITE | Pending |
| 2411 | DETECT FONT LOADED | DDO-FX LITE | Pending |
| 2431 | DEFINE A MACRO | DDO-FX LITE | Connected |
| 2590 | CLEAR A MACRO | DDO-FX LITE | Connected |
| 2602 | RUN A MACRO | DDO-FX LITE | Connected |
| 2615 | EXECUTE A MACRO LINE | DDO-FX LITE | Connected |
| 2662 | CREATE KEY MACRO | DDO-FX LITE | Pending |
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
| 3604 | LOCK KEYS | DDO-FX LITE | Pending |
| 3650 | UNLOCK KEYS | DDO-FX LITE | Pending |
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
| 4940 | LABEL | DDO-FX LITE | Pending |
| 4955 | RESET ENVIRONMENT | DDO-FX LITE | Pending |
| 5436 | DETECT MOUSE | DDO-FX (NM) | Pending |
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
| 6131 | LOAD PCX TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6163 | LOAD PCX TO WORK PAGE | DDO-FX (NM) | Pending |
| 6179 | LOAD PCX TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 6199 | LOAD A BITMAP FONT | DDO-FX (NM) | Pending |
| 6237 | BITMAP FONT SELECT | DDO-FX (NM) | Pending |
| 6246 | COPY BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 6254 | COPY BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 6261 | COPY WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6268 | COPY WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 6275 | COPY VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6282 | COPY VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 6289 | CUT BACKGROUND PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 6320 | CUT BACKGROUND PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 6328 | CUT WORK PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6336 | CUT WORK PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 6344 | CUT HIDDEN PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Pending |
| 6355 | CUT HIDDEN PAGE TO WORK PAGE (T) | DDO-FX (NM) | Pending |
| 6366 | CUT HIDDEN PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Pending |
| 6377 | CUT WORK PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Pending |
| 6388 | CUT WORK PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Pending |
| 6399 | CUT WORK PAGE TO BACKGROUND PAGE (T) | DDO-FX (NM) | Pending |
| 6410 | CUT BACKGROUND PAGE TO HIDDEN PAGE (T) | DDO-FX (NM) | Pending |
| 6421 | CUT BACKGROUND PAGE TO WORK PAGE (T) | DDO-FX (NM) | Pending |
| 6432 | CUT BACKGROUND PAGE TO VISUAL PAGE (T) | DDO-FX (NM) | Pending |
| 6443 | CUT VISUAL PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6453 | CUT VISUAL PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 6463 | DEFINE TRANSPARENT COLORS | DDO-FX (NM) | Pending |
| 6514 | SAVE BACKGROUND PAGE TO PCX | DDO-FX (NM) | Pending |
| 6528 | SAVE WORK PAGE TO PCX | DDO-FX (NM) | Pending |
| 6536 | SAVE VISUAL PAGE TO PCX | DDO-FX (NM) | Pending |
| 6544 | LOAD VISUAL PAGE PCX | DDO-FX (NM) | Pending |
| 6556 | LOAD WORK PAGE PCX | DDO-FX (NM) | Pending |
| 6568 | LOAD BACKGROUND PAGE PCX | DDO-FX (NM) | Pending |
| 6580 | GET SPRITE | DDO-FX (NM) | Pending |
| 6669 | GET SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6679 | GET SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6689 | GET SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6699 | DRAW SPRITE | DDO-FX (NM) | Pending |
| 6725 | DRAW SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6733 | DRAW SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6741 | DRAW SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6749 | CLIP SPRITE | DDO-FX (NM) | Pending |
| 6762 | CLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6769 | CLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6776 | CLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6783 | FLIP SPRITE | DDO-FX (NM) | Pending |
| 6799 | FLIP SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6806 | FLIP SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6813 | FLIP SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6820 | REVERSE SPRITE | DDO-FX (NM) | Pending |
| 6831 | REVERSE SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6838 | REVERSE SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6845 | REVERSE SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6852 | PUT SPRITE | DDO-FX (NM) | Pending |
| 6861 | PUT SPRITE VISUAL PAGE | DDO-FX (NM) | Pending |
| 6868 | PUT SPRITE BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 6875 | PUT SPRITE HIDDEN PAGE | DDO-FX (NM) | Pending |
| 6882 | INVERT SPRITE | DDO-FX (NM) | Pending |
| 6895 | FREE ALL SPRITES | DDO-FX (NM) | Pending |
| 6904 | STORE ALL SPRITES | DDO-FX (NM) | Pending |
| 6919 | LOAD ALL SPRITES | DDO-FX (NM) | Pending |
| 6939 | DEFINE ANIM PROFILE | DDO-FX (NM) | Pending |
| 7151 | DEFINE ANIM SOUND | DDO-FX (NM) | Pending |
| 7186 | PLAY ANIM PROFILE | DDO-FX (NM) | Pending |
| 7219 | STOP ANIM PROFILE | DDO-FX (NM) | Pending |
| 7235 | CHANGE ANIM PROFILE ON THE FLY | DDO-FX (NM) | Pending |
| 7246 | ANIM COLLISION PROFILE | DDO-FX (NM) | Pending |
| 7339 | RESOLUTION ANIMATOR | DDO-FX (NM) | Pending |
| 7383 | DEFINE MOTION PROFILE | DDO-FX (NM) | Pending |
| 7466 | DEFINE MOTION FRAME CONTROL | DDO-FX (NM) | Pending |
| 7534 | DEFINE SIMPLE SPRITE | DDO-FX (NM) | Pending |
| 7652 | DEFINE SIMPLE SPRITE ANIMATION | DDO-FX (NM) | Pending |
| 7733 | CLEAR SIMPLE SPRITES | DDO-FX (NM) | Pending |
| 7747 | CLEAR ALL SIMPLE SPRITES | DDO-FX (NM) | Pending |
| 7754 | DRAW SIMPLE SPRITES | DDO-FX (NM) | Pending |
| 7765 | COPY BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7824 | COPY HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 7831 | COPY WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7838 | COPY HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 7845 | COPY VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7852 | COPY HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 7859 | CUT BACKGROUND PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7869 | CUT HIDDEN PAGE TO BACKGROUND PAGE | DDO-FX (NM) | Pending |
| 7879 | CUT WORK PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7889 | CUT HIDDEN PAGE TO WORK PAGE | DDO-FX (NM) | Pending |
| 7899 | CUT VISUAL PAGE TO HIDDEN PAGE | DDO-FX (NM) | Pending |
| 7909 | CUT HIDDEN PAGE TO VISUAL PAGE | DDO-FX (NM) | Pending |
| 7919 | DO PRINT | DDO-FX (NM) | Pending |
| 8058 | DO PRINT TEXT FONT | DDO-FX (NM) | Pending |
| 8110 | DO PRINT TEXT FONT WP | DDO-FX (NM) | Pending |
| 8118 | DO PRINT TEXT FONT BP | DDO-FX (NM) | Pending |
| 8127 | DO PRINT TEXT FONT HP | DDO-FX (NM) | Pending |
| 8136 | SET COLOR | DDO-FX (NM) | Pending |
| 8146 | DRAW POINT | DDO-FX (NM) | Pending |
| 8156 | DRAW POINT XOR | DDO-FX (NM) | Pending |
| 8163 | ERASE PAGE | DDO-FX (NM) | Pending |
| 8172 | Get Pixel | DDO-FX (NM) | Pending |
| 8182 | MOVE GRAPHICS CURSOR | DDO-FX (NM) | Pending |
| 8192 | PAINT | DDO-FX (NM) | Pending |
| 8209 | SET RGB | DDO-FX (NM) | Pending |
| 8221 | GET RGB | DDO-FX (NM) | Pending |
| 8234 | STORE PALETTE | DDO-FX (NM) | Pending |
| 8245 | RESTORE PALETTE | DDO-FX (NM) | Pending |
| 8253 | ZERO PALETTE | DDO-FX (NM) | Pending |
| 8260 | FADE PALETTE OUT | DDO-FX (NM) | Pending |
| 8299 | FADE PALETTE OUT ADVANCED | DDO-FX (NM) | Pending |
| 8350 | FADE PALETTE IN | DDO-FX (NM) | Pending |
| 8361 | DRAW LINE RELATIVE | DDO-FX (NM) | Pending |
| 8374 | CHANGE VIEW PORT ORIGIN | DDO-FX (NM) | Pending |
| 8394 | SHAKE SCREEN | DDO-FX (NM) | Pending |
| 8404 | DRAW FILLED RECTANGLE | DDO-FX (NM) | Pending |
| 8420 | SET CLIPPING REGION | DDO-FX (NM) | Pending |
| 8439 | DRAW CIRCLE | DDO-FX (NM) | Pending |
| 8451 | DRAW FILLED CIRCLE | DDO-FX (NM) | Pending |
| 8463 | DRAW LINE | DDO-FX (NM) | Pending |
| 8474 | DRAW LINE XOR | DDO-FX (NM) | Pending |
| 8481 | DEFAULT 16 COLOR PALETTE | DDO-FX (NM) | Pending |
| 8492 | DRAW BOX | DDO-FX (NM) | Pending |
| 8508 | DRAW BOX XOR | DDO-FX (NM) | Pending |
| 8524 | EDIT BOX LINES | DDO-FX (NM) | Pending |
| 8533 | DRAW CLIPPED RECTANGLE | DDO-FX (NM) | Pending |
| 8548 | DRAW DASHED LINE | DDO-FX (NM) | Pending |
| 8564 | DRAW DASHED LINE RELATIVE | DDO-FX (NM) | Pending |
| 8580 | DRAW ELLIPSE | DDO-FX (NM) | Pending |
| 8592 | DRAW FILLED ELLIPSE | DDO-FX (NM) | Pending |
| 8599 | FILL PAGE | DDO-FX (NM) | Pending |
| 8607 | FLOOD | DDO-FX (NM) | Pending |
| 8616 | MOVEIT RELATIVE | DDO-FX (NM) | Pending |
| 8624 | DRAW BEZIER CURVE | DDO-FX (NM) | Pending |
| 8646 | DRAW BEZIER CURVE XOR | DDO-FX (NM) | Pending |
| 8656 | VERTICAL SCROLL | DDO-FX (NM) | Pending |
| 8694 | MOUSE ACTIVE | DDO-FX (NM) | Pending |
| 8713 | CHANGE MOUSE POINTER | DDO-FX (NM) | Pending |
| 8750 | LOAD MOUSE POINTER | DDO-FX (NM) | Pending |
| 8776 | SELECT MOUSE POINTER | DDO-FX (NM) | Pending |
| 8786 | ANIMATED MOUSE POINTER | DDO-FX (NM) | Pending |
| 8837 | ACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Pending |
| 8846 | DEACTIVATE ANIMATED MOUSE | DDO-FX (NM) | Pending |
| 8855 | MOUSE VISIBILITY | DDO-FX (NM) | Pending |
| 8866 | MOUSE LIMITS | DDO-FX (NM) | Pending |
| 8875 | MOUSE MOVE | DDO-FX (NM) | Pending |
| 8890 | MOUSE INFO | DDO-FX (NM) | Pending |
| 8928 | MOUSE INFO OLD | DDO-FX (NM) | Pending |
| 8958 | MOUSE SPEED | DDO-FX (NM) | Pending |
| 8968 | MOUSE REGION | DDO-FX (NM) | Pending |
| 9189 | MOUSE REGION NON REPEAT | DDO-FX (NM) | Pending |
| 9211 | MOUSE FIELD | DDO-FX (NM) | Pending |
| 9292 | MOUSE FIELD SWAP | DDO-FX (NM) | Pending |
| 9302 | MOUSE FIELD FRESHEN | DDO-FX (NM) | Pending |
| 9317 | CREATE RADIO BUTTON | DDO-FX (NM) | Pending |
| 9481 | EXECUTE RADIO BUTTON | DDO-FX (NM) | Pending |
| 9548 | SWAP RADIO BUTTON | DDO-FX (NM) | Pending |
| 9560 | DISABLE/ENABLE RADIO BUTTON | DDO-FX (NM) | Pending |
| 9576 | CREATE CHECK BOX | DDO-FX (NM) | Pending |
| 9679 | EXECUTE CHECK BOX | DDO-FX (NM) | Pending |
| 9688 | SWAP CHECK BOX | DDO-FX (NM) | Pending |
| 9697 | DISABLE/ENABLE CHECK BOX | DDO-FX (NM) | Pending |
| 9706 | CREATE PUSH BUTTON | DDO-FX (NM) | Pending |
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
| 10399 | CREATE MARQUEE | DDO-FX (NM) | Pending |
| 10471 | SHOW MARQUEE | DDO-FX (NM) | Pending |
| 10485 | STOP MARQUEE | DDO-FX (NM) | Pending |
| 10498 | KILL MARQUEE | DDO-FX (NM) | Pending |
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
| 13158 | GD DEFINE GRID | DDO-FX (NM) | Pending |
| 13236 | GD CHANGE GRID | DDO-FX (NM) | Pending |
| 13286 | GD FILL GRID | DDO-FX (NM) | Pending |
| 13322 | GD KILL GRID | DDO-FX (NM) | Pending |
| 13330 | GD DRAW GRID | DDO-FX (NM) | Pending |
| 13382 | GD POS GRID | DDO-FX (NM) | Pending |
| 13410 | GD MOVE GRID | DDO-FX (NM) | Pending |
| 13432 | GD POS TILE | DDO-FX (NM) | Pending |
| 13451 | GD MOVE TILE | DDO-FX (NM) | Pending |
| 13473 | GD KILL TILE | DDO-FX (NM) | Pending |
| 13488 | SET SLEEP DISCONNECT | DDO-FX (NM) | Pending |
| 13544 | SET PING | DDO-FX (NM) | Pending |
| 13620 | ROTATE IMAGE | DDO-FX (NM) | Pending |
| 13703 | SCALE IMAGE | DDO-FX (NM) | Pending |
| 13819 | ANTI-ALIAS IMAGE | DDO-FX (NM) | Pending |
| 13917 | OUTLINE IMAGE | DDO-FX (NM) | Pending |
| 13975 | ZMODEM BACKGROUND TRANSFERS | DDO-FX (NM) | Pending |
| 14035 | ZMODEM STEALTH MODE | DDO-FX (NM) | Pending |
| 14120 | DEFINE MUSIC BAR | DDO-FX (NM) | Pending |
| 14292 | ACTIVATE/DEACTIVATE MUSIC BAR | DDO-FX (NM) | Pending |
| 14308 | CLEAR MUSIC BAR | DDO-FX (NM) | Pending |
| 14318 | EXIT TO TEXT MODE | DDO-FX (NM) | Pending |
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
