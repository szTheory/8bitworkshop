
#include <stdlib.h>
#include <string.h>
#include <nes.h>
#include <joystick.h>

#include "neslib.h"

#pragma bss-name (push,"ZEROPAGE")
unsigned char oam_off;
#pragma bss-name (pop)

#pragma data-name (push,"CHARS")
#pragma data-name(pop)

//#link "tileset1.c"

extern unsigned char palSprites[16];
extern unsigned char TILESET[8*256];

#define COLS 32
#define ROWS 28

#define NTADR(x,y) ((0x2000|((y)<<5)|(x)))

typedef unsigned char byte;
typedef signed char sbyte;
typedef unsigned short word;

// read a character from VRAM.
// this is tricky because we have to wait
// for VSYNC to start, then set the VRAM
// address to read, then set the VRAM address
// back to the start of the frame.
byte getchar(byte x, byte y) {
  // compute VRAM read address
  word addr = NTADR(x,y);
  byte rd;
  // wait for VBLANK to start
  waitvsync();
  vram_adr(addr);
  vram_read(&rd, 1);
  vram_adr(0x0);
  return rd + 0x20;
}

// VRAM UPDATE BUFFER

byte updbuf[64];
byte updptr = 0;

void cendbuf() {
  updbuf[updptr] = NT_UPD_EOF;
}

void cflushnow() {
  cendbuf();
  waitvsync();
  flush_vram_update(updbuf);
  updptr = 0;
  cendbuf();
  vram_adr(0x0);
}

void vdelay(byte count) {
  while (count--) cflushnow();
}

void cputcxy(byte x, byte y, char ch) {
  word addr = NTADR(x,y);
  if (updptr >= 60) cflushnow();
  updbuf[updptr++] = addr >> 8;
  updbuf[updptr++] = addr & 0xff;
  updbuf[updptr++] = ch - 0x20;
  cendbuf();
}

void cputsxy(byte x, byte y, char* str) {
  word addr = NTADR(x,y);
  byte len = strlen(str);
  if (updptr >= 60 - len) cflushnow();
  updbuf[updptr++] = (addr >> 8) | NT_UPD_HORZ;
  updbuf[updptr++] = addr & 0xff;
  updbuf[updptr++] = len;
  while (len--) {
    	updbuf[updptr++] = *str++ - 0x20;
  }
  cendbuf();
}

void clrscr() {
  updptr = 0;
  cendbuf();
  ppu_off();
  vram_adr(0x2000);
  vram_fill(0, 32*28);
  vram_adr(0x0);
  ppu_on_bg();
}

////////// GAME DATA

typedef struct {
  byte x;
  byte y;
  byte dir;
  word score;
  char head_attr;
  char tail_attr;
  int collided:1;
  int human:1;
} Player;

Player players[2];

byte attract;
byte gameover;
byte frames_per_move;

#define START_SPEED 12
#define MAX_SPEED 5
#define MAX_SCORE 7

///////////

const char BOX_CHARS[8] = { '+','+','+','+','-','-','!','!' };

void draw_box(byte x, byte y, byte x2, byte y2, const char* chars) {
  byte x1 = x;
  cputcxy(x, y, chars[2]);
  cputcxy(x2, y, chars[3]);
  cputcxy(x, y2, chars[0]);
  cputcxy(x2, y2, chars[1]);
  while (++x < x2) {
    cputcxy(x, y, chars[5]);
    cputcxy(x, y2, chars[4]);
  }
  while (++y < y2) {
    cputcxy(x1, y, chars[6]);
    cputcxy(x2, y, chars[7]);
  }
}

void draw_playfield() {
  draw_box(1,2,COLS-2,ROWS-1,BOX_CHARS);
  cputcxy(9,1,players[0].score+'0');
  cputcxy(28,1,players[1].score+'0');
  if (attract) {
    cputsxy(5,ROWS-1,"ATTRACT MODE - PRESS 1");
  } else {
    cputsxy(1,1,"PLYR1:");
    cputsxy(20,1,"PLYR2:");
  }
}

typedef enum { D_RIGHT, D_DOWN, D_LEFT, D_UP } dir_t;
const char DIR_X[4] = { 1, 0, -1, 0 };
const char DIR_Y[4] = { 0, 1, 0, -1 };

void init_game() {
  memset(players, 0, sizeof(players));
  players[0].head_attr = '1';
  players[1].head_attr = '2';
  players[0].tail_attr = '#';
  players[1].tail_attr = '*';
  frames_per_move = START_SPEED;
}

void reset_players() {
  players[0].x = players[0].y = 5;
  players[0].dir = D_RIGHT;
  players[1].x = COLS-6;
  players[1].y = ROWS-6;
  players[1].dir = D_LEFT;
  players[0].collided = players[1].collided = 0;
}

void draw_player(Player* p) {
  cputcxy(p->x, p->y, p->head_attr);
}

void move_player(Player* p) {
  cputcxy(p->x, p->y, p->tail_attr);
  p->x += DIR_X[p->dir];
  p->y += DIR_Y[p->dir];
  if (getchar(p->x, p->y) != ' ')
    p->collided = 1;
  draw_player(p);
}

void human_control(Player* p) {
  byte dir = 0xff;
  byte joy;
  joy = joy_read (JOY_1);
  // start game if attract mode
  if (attract && (joy & JOY_START_MASK))
    gameover = 1;
  // do not allow movement unless human player
  if (!p->human) return;
  if (joy & JOY_LEFT_MASK) dir = D_LEFT;
  if (joy & JOY_RIGHT_MASK) dir = D_RIGHT;
  if (joy & JOY_UP_MASK) dir = D_UP;
  if (joy & JOY_DOWN_MASK) dir = D_DOWN;
  // don't let the player reverse
  if (dir < 0x80 && dir != (p->dir ^ 2)) {
    p->dir = dir;
  }
}

byte ai_try_dir(Player* p, dir_t dir, byte shift) {
  byte x,y;
  dir &= 3;
  x = p->x + (DIR_X[dir] << shift);
  y = p->y + (DIR_Y[dir] << shift);
  if (x < COLS && y < ROWS && getchar(x, y) == ' ') {
    p->dir = dir;
    return 1;
  } else {
    return 0;
  }
}

void ai_control(Player* p) {
  dir_t dir;
  if (p->human) return;
  dir = p->dir;
  if (!ai_try_dir(p, dir, 0)) {
    ai_try_dir(p, dir+1, 0);
    ai_try_dir(p, dir-1, 0);
  } else {
    ai_try_dir(p, dir-1, 0) && ai_try_dir(p, dir-1, 1+(rand() & 3));
    ai_try_dir(p, dir+1, 0) && ai_try_dir(p, dir+1, 1+(rand() & 3));
    ai_try_dir(p, dir, rand() & 3);
  }
}

void flash_colliders() {
  byte i;
  // flash players that collided
  for (i=0; i<56; i++) {
    //cv_set_frequency(CV_SOUNDCHANNEL_0, 1000+i*8);
    //cv_set_attenuation(CV_SOUNDCHANNEL_0, i/2);
    if (players[0].collided) players[0].head_attr ^= 0x80;
    if (players[1].collided) players[1].head_attr ^= 0x80;
    vdelay(2);
    draw_player(&players[0]);
    draw_player(&players[1]);
  }
  //cv_set_attenuation(CV_SOUNDCHANNEL_0, 28);
}

void make_move() {
  byte i;
  for (i=0; i<frames_per_move; i++) {
    human_control(&players[0]);
    vdelay(1);
  }
  ai_control(&players[0]);
  ai_control(&players[1]);
  // if players collide, 2nd player gets the point
  move_player(&players[1]);
  move_player(&players[0]);
}

void declare_winner(byte winner) {
  byte i;
  clrscr();
  for (i=0; i<ROWS/2-3; i++) {
    draw_box(i,i,COLS-1-i,ROWS-1-i,BOX_CHARS);
    vdelay(1);
  }
  cputsxy(12,10,"WINNER:");
  cputsxy(12,13,"PLAYER ");
  cputcxy(12+7, 13, '1'+winner);
  vdelay(75);
  gameover = 1;
}

#define AE(tl,tr,bl,br) (((tl)<<0)|((tr)<<2)|((bl)<<4)|((br)<<6))

// this is attribute table data, 
// each 2 bits defines a color palette
// for a 16x16 box
const unsigned char Attrib_Table[0x40]={
AE(3,3,1,0),AE(3,3,0,0),AE(3,3,0,0),AE(3,3,0,0), AE(2,2,0,0),AE(2,2,0,0),AE(2,2,0,0),AE(2,2,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,0,1,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0), AE(0,0,0,0),AE(0,0,0,0),AE(0,0,0,0),AE(0,1,0,1),
AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1), AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),AE(1,1,1,1),
};

// this is palette data
const unsigned char Palette_Table[16]={
  0x02,
  0x31,0x31,0x31,0x00,
  0x34,0x34,0x34,0x00,
  0x39,0x39,0x39,0x00,
};

// put 8x8 grid of palette entries into the PPU
void setup_attrib_table() {
  vram_adr(0x23c0);
  vram_write(Attrib_Table, 0x40);
}

void setup_palette() {
  int i;
  // only set palette entries 0-15 (background only)
  for (i=0; i<15; i++)
    pal_col(i, Palette_Table[i] ^ attract);
}

void play_round() {
  ppu_off();
  setup_attrib_table();
  setup_palette();
  clrscr();
  draw_playfield();
  reset_players();
  while (1) {
    make_move();
    if (gameover) return; // attract mode -> start
    if (players[0].collided || players[1].collided) break;
  }
  flash_colliders();
  // add scores to players that didn't collide
  if (players[0].collided) players[1].score++;
  if (players[1].collided) players[0].score++;
  // increase speed
  if (frames_per_move > MAX_SPEED) frames_per_move--;
  // game over?
  if (players[0].score != players[1].score) {
    if (players[0].score >= MAX_SCORE)
      declare_winner(0);
    else if (players[1].score >= MAX_SCORE)
      declare_winner(1);
  }
}

void play_game() {
  gameover = 0;
  init_game();
  if (!attract)
    players[0].human = 1;
  while (!gameover) {
    play_round();
  }
}

void main() {
  vram_adr(0x0);
  vram_write((unsigned char*)TILESET, sizeof(TILESET));
  joy_install (joy_static_stddrv);
  while (1) {
    attract = 1;
    play_game();
    attract = 0;
    play_game();
  }
}
