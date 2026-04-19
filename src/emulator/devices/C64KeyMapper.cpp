#include "emulator/devices/C64KeyMapper.h"
#include <SDL.h>

// ---------------------------------------------------------------------------
// C64 keyboard matrix (standard KERNAL layout)
//
//        col0  col1   col2    col3   col4    col5   col6   col7
// row0:  DEL   RET  CUR-RT    F7     F1      F3     F5   CUR-DN
// row1:   3     W     A        4      Z       S      E   LSHIFT
// row2:   5     R     D        6      X       T      F      C
// row3:   7     Y     G        8      V       H      U      B
// row4:   9     I     J        0      M       K      O      N
// row5:   +     P     L        -      .       :      @      ,
// row6:   £     *     ;      HOME  RSHIFT     =      ↑      /
// row7:   1     ←    CTRL      2   SPACE     C=      Q    STOP
// ---------------------------------------------------------------------------

void C64StandardKeyMapper::keyEvent(int sdlSym, bool pressed) {
    int col = -1, row = -1;

    switch (sdlSym) {
        // --- Column 0 ---
        case SDLK_DELETE:
        case SDLK_BACKSPACE:    col=0; row=0; break;
        case SDLK_3:            col=0; row=1; break;
        case SDLK_5:            col=0; row=2; break;
        case SDLK_7:            col=0; row=3; break;
        case SDLK_9:            col=0; row=4; break;
        case SDLK_KP_PLUS:      col=0; row=5; break;
        case SDLK_1:            col=0; row=7; break;

        // --- Column 1 ---
        case SDLK_RETURN:       col=1; row=0; break;
        case SDLK_w:            col=1; row=1; break;
        case SDLK_r:            col=1; row=2; break;
        case SDLK_y:            col=1; row=3; break;
        case SDLK_i:            col=1; row=4; break;
        case SDLK_p:            col=1; row=5; break;
        case SDLK_KP_MULTIPLY:
        case SDLK_RIGHTBRACKET: col=1; row=6; break;  // C64 *
        case SDLK_BACKQUOTE:
        case SDLK_LEFTBRACKET:  col=1; row=7; break;  // C64 ← (left arrow)

        // --- Column 2 ---
        case SDLK_RIGHT:        col=2; row=0; break;  // cursor right
        case SDLK_a:            col=2; row=1; break;
        case SDLK_d:            col=2; row=2; break;
        case SDLK_g:            col=2; row=3; break;
        case SDLK_j:            col=2; row=4; break;
        case SDLK_l:            col=2; row=5; break;
        case SDLK_SEMICOLON:    col=2; row=6; break;
        case SDLK_LCTRL:        col=2; row=7; break;

        // --- Column 3 ---
        case SDLK_F7:           col=3; row=0; break;
        case SDLK_4:            col=3; row=1; break;
        case SDLK_6:            col=3; row=2; break;
        case SDLK_8:            col=3; row=3; break;
        case SDLK_0:            col=3; row=4; break;
        case SDLK_MINUS:        col=3; row=5; break;
        case SDLK_HOME:         col=3; row=6; break;
        case SDLK_2:            col=3; row=7; break;

        // --- Column 4 ---
        case SDLK_F1:           col=4; row=0; break;
        case SDLK_z:            col=4; row=1; break;
        case SDLK_x:            col=4; row=2; break;
        case SDLK_v:            col=4; row=3; break;
        case SDLK_m:            col=4; row=4; break;
        case SDLK_PERIOD:       col=4; row=5; break;
        case SDLK_RSHIFT:       col=4; row=6; break;  // C64 RShift
        case SDLK_SPACE:        col=4; row=7; break;

        // --- Column 5 ---
        case SDLK_F3:           col=5; row=0; break;
        case SDLK_s:            col=5; row=1; break;
        case SDLK_t:            col=5; row=2; break;
        case SDLK_h:            col=5; row=3; break;
        case SDLK_k:            col=5; row=4; break;
        case SDLK_COLON:        col=5; row=5; break;  // C64 :
        case SDLK_EQUALS:       col=5; row=6; break;
        case SDLK_LALT:         col=5; row=7; break;  // C= Commodore key

        // --- Column 6 ---
        case SDLK_F5:           col=6; row=0; break;
        case SDLK_e:            col=6; row=1; break;
        case SDLK_f:            col=6; row=2; break;
        case SDLK_u:            col=6; row=3; break;
        case SDLK_o:            col=6; row=4; break;
        case SDLK_q:            col=6; row=7; break;

        // --- Column 7 ---
        case SDLK_DOWN:         col=7; row=0; break;  // cursor down
        case SDLK_LSHIFT:       col=7; row=1; break;
        case SDLK_c:            col=7; row=2; break;
        case SDLK_b:            col=7; row=3; break;
        case SDLK_n:            col=7; row=4; break;
        case SDLK_COMMA:        col=7; row=5; break;
        case SDLK_SLASH:        col=7; row=6; break;
        case SDLK_ESCAPE:       col=7; row=7; break;  // RUN/STOP

        default: break;
    }

    if (col >= 0)
        applyKey(col, row, pressed);
}
