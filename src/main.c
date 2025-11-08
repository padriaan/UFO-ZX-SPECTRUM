/*******************************************************************************************
ufo_zx 

Remake of Philips Videopac    34 Satellite Attack
          Magnavox Osyssey 2  34 UFO
          Originally released in 1981, programmed by Ed Averett.

Developed  with Z88DK in C for the Sinclair ZX Spectrum 48K.          
Created by Peter Adriaanse October 2025.

Version 1.0 (nr also in constant below)

Compile and link in Linux:
$ ./build.sh  (1 warning during compile)

Generates ufo_z80.tap (standalone for use in fuse or real heardware)

***********************************************************************************************/
#include <arch/zx.h>
#include <stdlib.h>
#include <string.h>
#include <arch/zx/sp1.h>
#include <alloc/balloc.h>
#include <input.h>
#include <intrinsic.h>
#include <compress/zx7.h>

#include "gfx.h"
#include "int.h"
#include "playfx.h"
#include "sound.h"

// scratch ram at 0x5e24 before program
//extern unsigned char TEMPMEM[1024];   // address placement defined in main.asm

#define VERSION "1.0"


// list of all beepfx sound effects
typedef struct effects_s
{
   void *effect;
   char *name;
} effects_t;
 
const effects_t beepfx[] = {
    {BEEPFX_SHOT_2,            "BEEPFX_EAT"},
    {BEEPFX_HIT_2,             "BEEPFX_HIT_ASTEROID"},
    {BEEPFX_DROP_2,            "BEEPFX_DYING"},
    {BEEPFX_SHOT_1,            "BEEPFX_FIRE_LASER"},
    {BEEPFX_POWER_OFF,         "BEEPFX_DYING"},
    {BEEPFX_FAT_BEEP_1,        "BEEPFX_DYING_SHORT"},
    {BEEPFX_JUMP_2,            "BEEPFX_MOVE"},
    {BEEPFX_ITEM_3,            "BEEPFX_ADD_UFO"}
};

struct sp1_Rect cr = { 0, 0, 32, 24 };

// sp1 print string context
struct sp1_pss ps0;

// control keys
unsigned int keys[5] = { 'o', 'p', 'q', 'a', 'z' };  //default left, right, up, down, fire
//unsigned int keys[5] = { 'j', 'l', 'i', 'k', 'z' };  //default left, right, up, down, fire

JOYFUNC joyfunc;
udk_t   joy_k;

char *redefine_texts[6] = {
   "\x14\x45" " LEFT",
   "RIGHT",
   "   UP",
   " DOWN",
   " FIRE"
};

#define LEFT                 1
#define RIGHT                2
#define UP                   3
#define DOWN                 4

#define FALSE                0
#define TRUE                 1
#define ASTEROID_SHAPE_TIMER 4    // must be an even integer for displaying image  
                                  // during multiple frames
#define NUM_ASTEROIDS       10    // max 15
#define NUM_BULLETS          3    // max 3
#define NUM_UFOS             2    // more makes game too hard
#define NUM_LASERS           2    // equals NUM_UFOS


// global variables for use in function rotate_maze_center()
// (needed this way, because compiler does not handle these as locals very well)

unsigned char udg_square_block[8]         = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

unsigned int key;
unsigned char i, j, k, found;
uint8_t ink_colour;
unsigned int  score, high_score;
unsigned char player_x;
unsigned char player_y;
unsigned char player_auto_direction;     // if <> 0, then 1,2,3 or 4 for auto movement to cell
                                         //          1=left, 2=right, 3=up, 4-down
unsigned char player_last_direction;     // (0=stopped, 1 left, 2 right, 3 up, 4 down)
unsigned char speed;                     // player speed in pixels per frame
unsigned char player_animation_frame;    // to display animations during movement
unsigned char player_dying;              // TRUE/FALSE
unsigned char player_destroyed;          // TRUE/FALSE
unsigned char player_dying_animation;    // to display dying animations
unsigned char player_explosion_nr;       // to display dying animations bullets 
unsigned char player_dying_countdown;    // countdown to 0 after player has been shot
unsigned char rotate_gun;                // TRUE/FALSE
unsigned char shield_timer;              // 0...x, 0 means shield is active
unsigned char gun_bit_position;          // 0..8
int bullet_frame;                        // frame no at wich last bullet was fired
int ufo_start_delay;                     // used to delay first ufo on screen
int frame = 1;


struct sp1_ss  *player_sprite;

// asteroids
typedef struct 
{
  struct sp1_ss *sprite;
  unsigned char status,          // 0 = not active, 1 = normal, 2 = magnetic, 3 = exploding
                                 //          3 = eaten/dead, 4 = recharging in center
                colour,          //colour  1=yellow, 2=green, 3=red, 4=cyan (normal)
                                 //        5=magenta                  (can be eaten)
                                 //        7=white                    (dead)
                speed,
                magnetic_timer,  // for displaying alternate + en x in magnetic asteroid
                shape_timer,     // timer for explosion when asteroid is hit
                recharge_timer;  // timer for asteroid being recharged  >> not used?
  signed char   xm, ym;          // delta x and y (can be negative)
  int           x,y;
} asteroid_sprite;

asteroid_sprite asteroids[15];   // max 15

// structure for asteroid animations

struct {  unsigned char *graphic; }     // sprites in gfx.h  // wordt niet opgepakt
asteroid_sprite_graphic[] = {
  {asteroid_cross},   
  {asteroid_plus},
  {asteroid_ball},   
  {asteroid_cross2},    // workaround for bug?
  {asteroid_explosion1} 
};

// bullets
typedef struct 
{
  struct sp1_ss *sprite;
  unsigned char x, y, 
                alive,                 // TRUE/FALSE 
                timer;                 // nr of frames alive
                signed char xm, ym;    // direction 
} bullet_sprite;

bullet_sprite bullets[3];    // max 3


// typedef voor ufo (8w x 2h pixel) 
typedef struct 
{
  struct sp1_ss *sprite;  
  unsigned char colour,            // 2 tm 6
                shape_timer,       // timer in frames for duration of explosion
                status;            // 0 = not active, 1 = active, 3 is exploding
  signed char   xm, ym;    // coordinates x,y ; speed is included in xm,ym
  int           x,y;
} ufo_sprite;

ufo_sprite ufos[2];    // max 2

struct {  unsigned char *graphic; }     // sprites in gfx.h  // wordt niet opgepakt
ufo_sprite_graphic[] = {
  {ufo},   
  {ufo2},    // workaround for bug?
  {ufo_explosion1} 
};


// typedef for laser for ufo (7w x 8h pixels) 
typedef struct 
{
  struct sp1_ss *sprite;  
  unsigned char alive,               // FALSE/TRUE
                fired_by_ufo;        // fired_by_ufo : ufo id 
  signed char   xm, ym;
  int           x, y;  
} laser_sprite;

laser_sprite lasers[2];   // max 2 (1 per ufo)

// structure for lasers   // 8 x 7 pixels
struct {  unsigned char *graphic; }     // sprites in gfx.h  // wordt niet opgepakt
laser_sprite_graphic[] = {
  {laser_left},   
  {laser_right},
  {laser_left2}
};


#define _________________________a
#define ___FORWARD_DECLARATIONS__b
#define _________________________c

void add_colour_to_sprite(unsigned int count, struct sp1_cs *c);
void pad_numbers(unsigned char *s, unsigned int limit, long number);
void get_ink_colour(unsigned char a_colour);
void run_redefine_keys(void);
void draw_menu(void);
void run_intro(void);
void display_score(void);
void handle_player(void);
void get_user_input(void);
void run_play(void);
void draw_player(void);
void check_player_collision(void);
void add_asteroid(void);
void draw_asteroids(void);
void handle_asteroids(void);
void check_colliding_asteroids(void);
void add_bullet(unsigned char xx, unsigned char yy);
void handle_bullets(void);
void check_bullet_hit(void);
void add_laser(unsigned char xx, unsigned char yy, 
               signed char xxm,  signed char yym, unsigned char ufo);
void handle_lasers(void);
void draw_lasers(void);
void check_laser_hit(void);
void draw_bullets(void);
void add_ufo(void);
void handle_ufos(void);
void draw_ufos(void);
void setup(void);
void start_new_game(void);
void hide_sprites(void);
void delete_sprites(void);
int main(void);

#define __________a
#define ___MAIN___b
#define __________c

  
void add_colour_to_sprite(unsigned int count, struct sp1_cs *c)
{
    (void)count;    // Suppress compiler warning about unused parameter 
    c->attr_mask = SP1_AMASK_INK;
    c->attr      = ink_colour;
}


void pad_numbers(unsigned char *s, unsigned int limit, long number)
{
   s += limit;
   *s = 0;

   // not a fast method since there are two 32-bit divisions in the loop
   // better would be ultoa or ldivu which would do one division or if
   // the library is so configured, they would do special case base 10 code.
   
   while (limit--)
   {
      *--s = (number % 10) + '0';
      number /= 10;
   }
}



void get_ink_colour(unsigned char a_colour)
{
   // ink_colour is a global variable
   switch (a_colour) {
   case 1:
          ink_colour = INK_YELLOW | PAPER_BLACK;      
          break;
   case 2:
          ink_colour = INK_GREEN | PAPER_BLACK;      
          break;
   case 3:
          ink_colour = INK_RED | PAPER_BLACK;      
          break;
   case 4:
          ink_colour = INK_CYAN | PAPER_BLACK;      
          break;
   case 5:
          ink_colour = INK_MAGENTA | PAPER_BLACK;      
          break;
   case 6:
          ink_colour = INK_BLUE | PAPER_BLACK;      
          break;
   case 7:
          ink_colour = INK_WHITE | PAPER_BLACK;      
          break;
   } 
}


void run_redefine_keys(void)
{
   struct sp1_Rect r = { 10, 2, 30, 10 };

   sp1_ClearRectInv(&r, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);

   sp1_SetPrintPos(&ps0, 10, 10);
   sp1_PrintString(&ps0, "\x14\x47" "REDEFINE KEYS");

   for (i = 0; i < 5; ++i)
   {
      sp1_SetPrintPos(&ps0, 12 + i, 11);
      sp1_PrintString(&ps0, redefine_texts[i]);  
      sp1_UpdateNow();

      in_wait_key();
      keys[i] = in_inkey();
      in_wait_nokey();

      // nope!
      if (keys[i] < 32) {
         --i;
         continue;
      }

      // space is not visible, make it so
      if (keys[i] == 32) {
         sp1_SetPrintPos(&ps0, 12 + i, 18);
         sp1_PrintString(&ps0, "\x14\x46" "SPACE");
      }
      else sp1_PrintAtInv(12 + i, 18, INK_YELLOW, keys[i]);
      
      sp1_UpdateNow();
      playfx(FX_SELECT);
   }

   // some delay so the player can see last pressed key
   for (i = 0; i < 16; ++i)
      wait();
}


void draw_menu(void)
{
   unsigned char buffer[16];
   struct sp1_Rect r = { 9, 0, 32, 14 };

   // clear the screen first to avoid attribute artifacts
   sp1_ClearRectInv(&cr, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);
   sp1_UpdateNow();
   wait();

   // clear the screen first to avoid attribute artifacts
   dzx7_standard(menu, (void *)0x4000);

   sp1_SetPrintPos(&ps0, 9, 0);
   sp1_PrintString(&ps0, "\x14\x47" "BASED ON UFO / SATELLITE ATTACK");
   sp1_SetPrintPos(&ps0, 11, 8);
   sp1_PrintString(&ps0, "\x14\x46" "MAGNAVOX ODESSEY 2");
   sp1_SetPrintPos(&ps0, 12, 8 );
   sp1_PrintString(&ps0, "\x14\x46" "PHILIPS VIDEOPAC");


   sp1_SetPrintPos(&ps0, 14, 2);
   sp1_PrintString(&ps0, "\x14\x45" "Original by Ed Averett 1981");
   sp1_SetPrintPos(&ps0, 16, 1);
   sp1_PrintString(&ps0, "\x14\x43" "Remake by Peter Adriaanse 2025");


 
   sp1_SetPrintPos(&ps0, 20, 9);
   sp1_PrintString(&ps0, "\x14\x47" "PRESS ANY KEY");

   sp1_SetPrintPos(&ps0, 23, 10);
   sp1_PrintString(&ps0, "\x14\x01" "version");
   sp1_SetPrintPos(&ps0, 23, 19);
   sp1_PrintString(&ps0, VERSION);
   sp1_UpdateNow();

   in_wait_key();
   // avoid the player pressing one of the options and triggering the associated code
   in_wait_nokey();

   sp1_ClearRectInv(&r, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);

   sp1_SetPrintPos(&ps0, 11, 11);
   sp1_PrintString(&ps0, "\x14\x47" "1 KEYBOARD"
                 "\x0b\x0b\x06\x0b" "2 KEMPSTON"
                 "\x0b\x0b\x06\x0b" "3 SINCLAIR"
                 "\x0b\x0b\x06\x0b" "4 REDEFINE KEYS"
               );

   // the hiscore and score
   sp1_SetPrintPos(&ps0, 8, 11);
   sp1_PrintString(&ps0, "\x14\x47" "HIGH");

   buffer[0] = 0x14;
   buffer[1] = INK_YELLOW | PAPER_BLACK;
   pad_numbers(buffer + 2, 4, high_score);

   sp1_SetPrintPos(&ps0, 8, 17);
   sp1_PrintString(&ps0, buffer);

   sp1_SetPrintPos(&ps0, 9, 0);
   sp1_PrintString(&ps0, "\x14\x47" " ");

   sp1_SetPrintPos(&ps0, 14, 0);
   sp1_PrintString(&ps0, "\x14\x47" " ");
}


void run_intro(void)
{
   sp1_ClearRectInv(&cr, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);
   sp1_UpdateNow();

   // the menu key may be is still pressed
   in_wait_nokey();
   wait();

   // copy attributes to our temp memory area
   //memcpy(TEMPMEM, (void *)0x5800, 768);     // TO DO can be deleted??!!

   sp1_ClearRectInv(&cr, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);
   sp1_UpdateNow();

   // in case a key was pressed to skip the intro
   in_wait_nokey();
}


void display_score(void)
{
   unsigned char buffer[16];

   sp1_SetPrintPos(&ps0, 23, 3);
   sp1_PrintString(&ps0, "\x14\x47" "HIGH");

   buffer[0] = 0x14;
   buffer[1] = INK_YELLOW | PAPER_BLACK;
   pad_numbers(buffer + 2, 4, high_score);

   sp1_SetPrintPos(&ps0, 23, 8);
   sp1_PrintString(&ps0, buffer);

   
   sp1_SetPrintPos(&ps0, 23, 13);
   sp1_PrintString(&ps0, "\x14\x47" "-> UFO SCORE");

   buffer[0] = 0x14;
   buffer[1] = INK_YELLOW | PAPER_BLACK;
   pad_numbers(buffer + 2, 4, score);

   sp1_SetPrintPos(&ps0, 23, 26);
   sp1_PrintString(&ps0, buffer);
   
}



void handle_player(void)
{
  ; // place holder for player (not used, in get_user_input)
}


void get_user_input(void)
{
    rotate_gun = FALSE;

    key = (joyfunc)(&joy_k);

   // Check continuous-response keys / joystick
   
   if (player_dying == FALSE) {

       if (key & IN_STICK_LEFT && !(key & IN_STICK_RIGHT) && player_x > 3)
          player_x = player_x - speed;
       if (key & IN_STICK_RIGHT && !(key & IN_STICK_LEFT)  && player_x < 239)
           player_x = player_x + speed;  
       if (key & IN_STICK_UP && !(key & IN_STICK_DOWN) && player_y > 1)
           player_y = player_y - speed;   
       if (key & IN_STICK_DOWN && !(key & IN_STICK_UP) && player_y < 166)
           player_y = player_y + speed;  

       // rotate gun also when at end of screen
       if ( (key & IN_STICK_DOWN)  || (key & IN_STICK_UP) ||
            (key & IN_STICK_RIGHT) || (key & IN_STICK_LEFT) )
                rotate_gun = TRUE;

       if ((rotate_gun == TRUE) && (frame % 3 == 0)) {     //slow down a bit
          gun_bit_position++;
          if (gun_bit_position == 9) gun_bit_position = 1;  // back to 1: workaround bug!
       }    

       if (key & IN_STICK_FIRE) {
          // prevent bullets fired to soon after each other
          if (frame - bullet_frame >= 5) {
              add_bullet(player_x, player_y);
             bullet_frame = frame;
          }
        } 

    } else {    // dying == TRUE
        player_dying_countdown--;
        if (player_dying_countdown == 0) player_destroyed = TRUE;       

        player_dying_animation++;
        if (player_dying_animation == 6) {
              player_dying_animation = 1;
         }      

    } // player_dying == FALSE 
}


void run_play(void)
{
   sp1_ClearRectInv(&cr, INK_WHITE | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);
   sp1_UpdateNow();

   start_new_game();

   // setup asteroids sprites
   for (i = 0; i < NUM_ASTEROIDS; i++)  {
       asteroids[i].sprite = sp1_CreateSpr(SP1_DRAW_MASK2LB, SP1_TYPE_2BYTE, 2, (int)asteroid_cross, 0);
       sp1_AddColSpr(asteroids[i].sprite, SP1_DRAW_MASK2RB, SP1_TYPE_2BYTE, 0, 0);
       asteroids[i].colour =  (i + 1) % 8;  //(rand() % 7) + 1;       // 1-7
       get_ink_colour(asteroids[i].colour);
       sp1_IterateSprChar(asteroids[i].sprite, add_colour_to_sprite);
   } 

   // setup bullet sprites
   for (i = 0; i < NUM_BULLETS; i++)  {
       bullets[i].sprite = sp1_CreateSpr(SP1_DRAW_MASK2LB, SP1_TYPE_2BYTE, 2, (int)bullet, 0);
       sp1_AddColSpr(bullets[i].sprite, SP1_DRAW_MASK2RB, SP1_TYPE_2BYTE, 0, 0);
       ink_colour = INK_WHITE | PAPER_BLACK;
       sp1_IterateSprChar(bullets[i].sprite, add_colour_to_sprite);
   } 

   // setup ufo sprites
   for (i = 0; i < NUM_UFOS; i++)  {
       ufos[i].sprite = sp1_CreateSpr(SP1_DRAW_MASK2LB, SP1_TYPE_2BYTE, 2, (int)ufo, 0);
       sp1_AddColSpr(ufos[i].sprite, SP1_DRAW_MASK2RB, SP1_TYPE_2BYTE, 0, 0);

       if (i%2 == 0) ink_colour = BRIGHT | INK_CYAN   | PAPER_BLACK;  
       else          ink_colour = BRIGHT | INK_YELLOW | PAPER_BLACK;  
       sp1_IterateSprChar(ufos[i].sprite, add_colour_to_sprite);
   } 

   // setup laser sprites
   for (i = 0; i < NUM_LASERS; i++)  {
       lasers[i].sprite = sp1_CreateSpr(SP1_DRAW_MASK2LB, SP1_TYPE_2BYTE, 2, (int)laser_left, 0);
       sp1_AddColSpr(lasers[i].sprite, SP1_DRAW_MASK2RB, SP1_TYPE_2BYTE, 0, 0);
       ink_colour = INK_WHITE | PAPER_BLACK;  
       sp1_IterateSprChar(lasers[i].sprite, add_colour_to_sprite);
   } 


   // set up player sprite (keeps living in the game forever)
   player_x = 124;
   player_y = 88;
   player_sprite = sp1_CreateSpr(SP1_DRAW_MASK2LB, SP1_TYPE_2BYTE, 3, (int)player1_col1, 0);
   sp1_AddColSpr(player_sprite, SP1_DRAW_MASK2,    SP1_TYPE_2BYTE,    (int)player1_col2, 0);
   sp1_AddColSpr(player_sprite, SP1_DRAW_MASK2RB,  SP1_TYPE_2BYTE,    0, 0);
   ink_colour = BRIGHT | INK_RED | PAPER_BLACK;
   sp1_IterateSprChar(player_sprite, add_colour_to_sprite);


   // ------------------
   // - Main game loop -
   // ------------------ 
   while(1)
   {
      if (in_inkey() == 12) {   // backspace on PC keyboard
         hide_sprites();        // to clear all sprites
         delete_sprites();
         break;                 // exit current game
      }   
      
      // restart_game after death 
      if (player_destroyed == TRUE) {
         ink_colour = BRIGHT | INK_RED | PAPER_BLACK;  // change colour player to red
         sp1_IterateSprChar(player_sprite, add_colour_to_sprite);
         hide_sprites();    // to clear all sprites
         start_new_game();
         // dummy move player after restart (to prevent bug of invalidated sprite)
         sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)((gun_bit_position + 6) * 96), player_x + 10, player_y); 
         sp1_UpdateNow();
         sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)(gun_bit_position * 96), player_x, player_y); 
         gun_bit_position = 1;
         sp1_UpdateNow();

      }  

      get_user_input();  // also calls handle_player();
      draw_player();

      handle_asteroids();
      if (rand() % 5 == 1 ) add_asteroid(); 
      // add_asteroid(); 
      draw_asteroids();

      if (frame % 2 == 0 && player_dying == FALSE) check_colliding_asteroids();

      handle_ufos();
      draw_ufos();

      if (rand() % 50 == 1 && player_dying == FALSE && (frame - ufo_start_delay > 150) )
        add_ufo();

      handle_bullets();
      if (player_dying == FALSE) check_bullet_hit();
      draw_bullets();

      handle_lasers();
      draw_lasers();

      if (player_dying == FALSE) check_player_collision();
      if (player_dying == FALSE) check_laser_hit();

      frame++;

      intrinsic_halt();   // inline halt without impeding optimizer  
      sp1_UpdateNow();

   }  // main loop

   sp1_ClearRectInv(&cr, INK_BLACK | PAPER_BLACK, 32, SP1_RFLAG_TILE | SP1_RFLAG_COLOUR);
   sp1_UpdateNow();
}


void draw_player(void)
{
   if (player_dying == FALSE) { 
      switch (shield_timer) {
      case 51:
          ink_colour = INK_BLUE | PAPER_BLACK;      
          break;
      case 41:
          ink_colour = BRIGHT | INK_BLUE | PAPER_BLACK;      
          break;
      case 21:
          ink_colour = INK_MAGENTA | PAPER_BLACK;      
          break;
      case 11:
          ink_colour = BRIGHT | INK_MAGENTA | PAPER_BLACK;      
          break;
      case 1:
          ink_colour = BRIGHT | INK_RED | PAPER_BLACK;      
          break;
      }

      if (shield_timer > 0) {   // change colour
         sp1_IterateSprChar(player_sprite, add_colour_to_sprite);
         shield_timer--;
      } 

      sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)(gun_bit_position * 96), player_x, player_y); 
            // (void *) (size_t) to prevent compiler warning
   
      if (shield_timer == 1) {  
         sp1_SetPrintPos(&ps0, 22, 15);
         sp1_PrintString(&ps0, "\x14\x47" "         ");
      }

      if (shield_timer > 0) shield_timer--;


   } else {    // dying == TRUE
        if (player_dying_countdown > 20) {    // show dying animations

            if (player_dying_animation == 1|| player_dying_animation == 2) 
                 sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)( 9 * 96), 
                               player_x, player_y);            // explosion 1
            else if (player_dying_animation == 4 || player_dying_animation == 5)
                sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)((gun_bit_position * 96)), 
                               player_x, player_y);            // explosion 2
            else sp1_MoveSprPix(player_sprite, &cr, (void *) (size_t)((10 * 96)), 
                               player_x, player_y);            // normal

            // hide all existing bullets and create 3 new ones
            if (frame % 7 == 0) {
               for (k = 0; k < NUM_BULLETS; k++) {
                      sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                      bullets[k].alive = TRUE;
                      bullets[k].timer = 5;   // smaller reach than normal bullet
                      bullets[k].x = player_x + 7;  // halfway ship
                      bullets[k].y = player_y + 6;  // halfway ship
               }     
               if (player_explosion_nr == 0) {  // alternate bullet directions
                 bullets[0].xm = 0;
                 bullets[0].ym = 8;
                 bullets[1].xm = 6;
                 bullets[1].ym = -6;
                 bullets[2].xm = -6;
                 bullets[2].ym = -6;
                 player_explosion_nr = 1;
               } else {
                 bullets[0].xm = 0;
                 bullets[0].ym = -8;
                 bullets[1].xm = -6;
                 bullets[1].ym = 6;
                 bullets[2].xm = 6;
                 bullets[2].ym = 6;
                 player_explosion_nr = 0;
               } 
            }     // frame % 10 == 0
            if (player_dying_countdown % 5 == 0) bit_beepfx_di(beepfx[2].effect); // BOOM

        } else {

            if (player_dying_countdown == 20) {   // hide explosions
                 for (k = 0; k < NUM_BULLETS; k++)  sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); 
                 sp1_MoveSprAbs(player_sprite, &cr, NULL, 0, 34, 0, 0);    
                 sp1_UpdateNow();
            }  
            

        }        //  player_dying_countdown > 3
   }

   /* 
   unsigned char buffer[16];
  
   buffer[0] = 0x14;
   buffer[1] = BRIGHT | INK_YELLOW | PAPER_BLACK;
   pad_numbers(buffer + 2, 4, gun_bit_position);
   //pad_numbers(buffer + 2, 4, shield_timer);
  
   sp1_SetPrintPos(&ps0, 22, 6);
   sp1_PrintString(&ps0, buffer);
   */
}


void check_player_collision(void)
{
  int a_x, a_y, a_xr, a_yb;       // top-left and bottom-right coordinates of asteroid
  
    // check if ship collides with an asteroid/player 
    for (i = 0; i < NUM_ASTEROIDS; i++)
    {
      if (asteroids[i].status == 1 || asteroids[i].status == 2) {
         a_xr = (asteroids[i].x + 6) ;   // width 
         a_yb = (asteroids[i].y + 5) ;   // height
         a_x  = asteroids[i].x;
         a_y  = asteroids[i].y;

         // check overlap of astroid and ship 
         if ( (player_x + 15)        > a_x   &&
               player_x              < a_xr  &&
              (player_y + 10)        > a_y   &&
               player_y              < a_yb) {

             if (shield_timer > 0 && player_dying == FALSE) { // shield is down
                  //printf("DEADLY COLLISION WITH ASTEROID!\n");
                  player_dying = TRUE;
                  player_explosion_nr = 0;
                  player_dying_animation = 1;
                  player_dying_countdown = 45;

                  ink_colour = BRIGHT | INK_WHITE | PAPER_BLACK;  // change colour player to white    
                  sp1_IterateSprChar(player_sprite, add_colour_to_sprite);

                  asteroids[i].status = 0;    // hide sprite which hit player
                  sp1_MoveSprAbs(asteroids[i].sprite, &cr, NULL, 0, 34, 0, 0);

             } else {      // shield is active, destroy asteroid

                if (asteroids[i].status == 1) score++;
                else if (asteroids[i].status == 2) score = score + 3;

                if (score > high_score)  high_score = score;
                display_score();

                // set status 3 : exploding
                asteroids[i].status = 3;
                asteroids[i].shape_timer = 0;   // explosion takes x frames
                                                // NB already set in add_bullet

                // hide all existing bullets and create 3 new ones
                for (k = 0; k < NUM_BULLETS; k++) {
                       sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                       bullets[k].alive = TRUE;
                       bullets[k].timer = 12;   // smaller reach than normal bullet
                       bullets[k].x = asteroids[i].x + 3;  // halfway asteroid
                       bullets[k].y = asteroids[i].y + 2;  // halfway asteroid
                }     
                bullets[0].xm = 0;
                bullets[0].ym = 4;
                bullets[1].xm = 3;
                bullets[1].ym = -3;
                bullets[2].xm = -3;
                bullets[2].ym = -3;

                // disable shield
                shield_timer = 51;

             }  // shield_timer > 0
                  
         }     // check overlap
      }        // status 1 or 2      
    } // end loop active asteroids


    // check if player collides with active ufo (ship can hit asteroid too)
    for (i = 0; i < NUM_UFOS && player_dying == FALSE; i++) {
        if (ufos[i].status == 1) {
             a_xr = (ufos[i].x + 8) ;   // width
             a_yb = (ufos[i].y + 2) ;   // height
             a_x  = ufos[i].x;
             a_y  = ufos[i].y;
           
           /* check overlap of ufo and ship */
           /* (ship size is larger when shield is active) */
           if (  (player_x - 3) + 15        > a_x   &&
                 (player_x - 3)             < a_xr  &&
                 (player_y - 3) + 10        > a_y   &&
                 (player_y - 3)            < a_yb) {
               //printf("hit ufo with ship\n");

             if (shield_timer > 0 && player_dying == FALSE) { // shield is down
                  //printf("DEADLY COLLISION WITH UFO!\n");
                  player_dying = TRUE;
                  player_explosion_nr = 0;
                  player_dying_animation = 1;
                  player_dying_countdown = 45;

                  ink_colour = BRIGHT | INK_WHITE | PAPER_BLACK;  // change colour player to white    
                  sp1_IterateSprChar(player_sprite, add_colour_to_sprite);

                  ufos[i].status = 0;    // hide ufo which hit player
                  sp1_MoveSprAbs(ufos[i].sprite, &cr, NULL, 0, 34, 0, 0);

             } else {      // shield is active, destroy ufo

                score = score + 10;
                if (score > high_score) high_score = score;
                display_score();

                // set status 3 : ufo exploding
                ufos[i].status = 3;
                ufos[i].shape_timer = 0;   // explosion takes x frames
                                           // NB already set in add_bullet

                // hide all existing bullets and create 3 new ones
                for (k = 0; k < NUM_BULLETS; k++) {
                       sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                       bullets[k].alive = TRUE;
                       bullets[k].timer = 12;   // smaller reach than normal bullet
                       bullets[k].x = ufos[i].x + 4;  // halfway ufo
                       bullets[k].y = ufos[i].y + 1;  // halfway ufo
                }     
                bullets[0].xm = 0;
                bullets[0].ym = 4;
                bullets[1].xm = 3;
                bullets[1].ym = -3;
                bullets[2].xm = -3;
                bullets[2].ym = -3;

                // disable shield
                shield_timer = 51;

             }  // shield_timer > 0
           }   // player collides iwth ufo
         }      // ufo status == 1
     }          // for loop ufos    

}


void add_asteroid(void)
{
  unsigned char  direction;
  
  // Find a slot
  found = 99;
  for (i = 0; i < NUM_ASTEROIDS && found == 99; i++) {
     if (asteroids[i].status == 0)
        found = i;
  }
  
  // Turn the asteroid on: 
  // (size is 6 pixels wide and 5 pixels tall) 
  if (found != 99) {
      asteroids[found].status      = 1;                      // normal, not magnetic
      asteroids[found].shape_timer = ASTEROID_SHAPE_TIMER;   // timer for explosion shape

      // random 1 of 4 starting positions 
      direction = rand() %4 + 1; 
        switch (direction) {
        case (1):
          // spawn from top 
          asteroids[found].x  = (rand() % (255 - 20)) + 10;
          asteroids[found].y  = 0;
          asteroids[found].xm = ( rand() % 5) - 2;             // values -2, -1, 0, 1 or 2) 
          asteroids[found].ym = ( rand() % 2) + 1;             // values 1 or 2) 
          break;
        case (2):
          // spawn from bottom 
          asteroids[found].x  = (rand() % (255 - 20)) + 10;
          asteroids[found].y  = 175;  // height of asteroid out of screen
          asteroids[found].xm = ( rand() % 5) - 2;             // values -2, -1, 0, 1 or 2) 
          asteroids[found].ym = ( rand() % 2) - 2;             // values -1 or -2) 
          break;
        case (3):
          // spawn from left 
          asteroids[found].x  = 0;                     // width of asteroid out of screen
          asteroids[found].y  = (rand() % (176 - 20)) + 10;
          asteroids[found].xm = ( rand() % 2) + 1;               // values 1 or 2) 
          asteroids[found].ym = ( rand() % 5) - 2;               // values -2, -1, 0, 1 or 2) 
          break;
        case (4):
          // spawn from right 
          asteroids[found].x  = 255 - (6);      // width of asteroid out of screen
          asteroids[found].y  = (rand() % (176 - 20)) + 10;
          asteroids[found].xm = ( rand() % 2) - 2;                // values -1 or -2) 
          asteroids[found].ym = ( rand() % 5) - 2;                // values -2, -1, 0, 1 or 2) 
          break;
        }
    } // found != 99    
}


void draw_asteroids(void)
{
  unsigned char image_num;

  for (i = 0; i < NUM_ASTEROIDS; i++)  {
    //image_num = 0;

    switch (asteroids[i].status) {
    case 1:  // normal
             // display alternating + en x for normal asteroid, 
             //  2 frames per image, example ++xx++xx++xx
       
             if (asteroids[i].shape_timer >= 1 && asteroids[i].shape_timer <= 2) 
                  image_num = 96;   // cross
             else image_num = 32;   // plus
             break;
    case 2:  // magnetic
             if (asteroids[i].shape_timer >= 1 && asteroids[i].shape_timer <= 2) {
                  image_num = 64;  // ball
             } else {   
                  if (asteroids[i].magnetic_timer%2 == 0) 
                        image_num = 32; // plus
                  else  image_num = 96; // cross
                        
             }   
             asteroids[i].magnetic_timer++; 
             if (asteroids[i].magnetic_timer > 5) asteroids[i].magnetic_timer = 0; 
             break;
     case 3:  // exploding
              if (asteroids[i].shape_timer >= 1 && asteroids[i].shape_timer <= 2) 
                   image_num = 128;   // explosion 1
              else image_num = 160;   // explosion 2
              break;
     }          
 
     if (asteroids[i].x >= 0 && asteroids[i].y >= 0 && asteroids[i].status > 0) {  
        sp1_MoveSprPix(asteroids[i].sprite, &cr, 
                       (void *) (size_t) image_num,
                       //0,       // cross  do not use
                       //32,      // plus 
                       //64,      // ball 
                       //96       // cross  < used for workaround bug
                       //128      // explosion1
                       //160      // explosion2
                       //asteroid_sprite_graphic[0].graphic,   // werkt niet??
                       asteroids[i].x, asteroids[i].y);
      }
       
   }    //for loop    
}


void handle_asteroids(void)
{
    for (i = 0; i < NUM_ASTEROIDS; i++) {
      if (asteroids[i].status >= 1 && asteroids[i].status <= 2) {
          // Move
          if (asteroids[i].status == 1) {     // normal asteroid
             asteroids[i].x = asteroids[i].x + asteroids[i].xm;
             asteroids[i].y = asteroids[i].y + asteroids[i].ym;

          } else {  // magnetic, move towards ship

              if (player_dying == FALSE) {   // only when ship is still alive
                 if (asteroids[i].x > player_x + 5)
                   asteroids[i].x = asteroids[i].x - 1;
                 else if (asteroids[i].x < player_x + 5)
                   asteroids[i].x = asteroids[i].x + 1;

                 if (asteroids[i].y > player_y + 2)
                   asteroids[i].y = asteroids[i].y - 1 ;
                 else if (asteroids[i].y < player_y + 2)
                   asteroids[i].y = asteroids[i].y + 1;
              } 

          }

          asteroids[i].shape_timer--;;          
          if (asteroids[i].shape_timer == 0)
              asteroids[i].shape_timer = ASTEROID_SHAPE_TIMER ;
          
          // Off screen? 
          if (asteroids[i].x < 0 || asteroids[i].x >= 255 + 6 ||
              asteroids[i].y < 0 || asteroids[i].y >= 170 + 5) {

              asteroids[i].status = 0;
              sp1_MoveSprAbs(asteroids[i].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
          }

      }  // if status 1 or 2


      if (asteroids[i].status == 3) {   // exploding
          asteroids[i].shape_timer++;
          if (asteroids[i].shape_timer == 5) {
              //  after explosion disable asteroid completely
                asteroids[i].status = 0;
                sp1_MoveSprAbs(asteroids[i].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
          } else {
              if (asteroids[i].shape_timer == 2) bit_beepfx_di(beepfx[1].effect); // HIT
          }      
      }

    }    // for loop
}


void check_colliding_asteroids(void)
{
  unsigned char a_x, a_y, a_xr, a_yb;       // top-left and bottom-right coordinates of asteroid
  unsigned char b_x, b_y, b_xr, b_yb;       // top-left and bottom-right coordinates of asteroid

  // handle colliding asteroid with other asteroid and
  //   colliding asteroid with players (player alway loses) 

  // loop active astroids (magnetic and non-magnetic) 
  for (i = 0; i < NUM_ASTEROIDS; i++)
  {
    if (asteroids[i].status == 1 || asteroids[i].status == 2) {
       a_xr = (asteroids[i].x + 6);   // width 
       a_yb = (asteroids[i].y + 5);   // height
       a_x  = asteroids[i].x;
       a_y  = asteroids[i].y;

       // loop active astroids (magnetic and non-magnetic) 
       for (j = 0; j < NUM_ASTEROIDS; j++)  
       {
          if ((asteroids[j].status == 1 || asteroids[i].status == 2) && i < j) {  
                                                               // not itself or double checks
             b_xr = (asteroids[j].x + 6);    // width  
             b_yb = (asteroids[j].y + 5);    // height 
             b_x  = asteroids[j].x;
             b_y  = asteroids[j].y;

           // check overlap of asteroids 
           if (b_xr  > a_x   &&
               b_x   < a_xr  &&
               b_yb  > a_y   &&
               b_y   < a_yb) {
                  // create magnetic 1 out of 3 
                  if (rand() % 3 == 0 && asteroids[i].status != 2) {

                       // determine which asteroid is non-magnetic 
                       if (asteroids[i].status == 1) {
                          asteroids[i].status = 2;   // make magnetic, keep colour //
                          asteroids[i].magnetic_timer = 0;

                          // kill the other one (only if this one is non-magnetic)
                          if (asteroids[j].status == 1) {
                            asteroids[j].status = 0;
                            sp1_MoveSprAbs(asteroids[j].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
                          }                            
                       } else {
                          // asteroids[i] is magnetic, check if asteroids[j] is normal 
                          // if so, make kill j
                          if (asteroids[j].status == 1) {
                             //printf("Magnetic (j) created!\n");
                             // kill the other one
                             asteroids[j].status = 0;
                             sp1_MoveSprAbs(asteroids[j].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
                          } else {
                               // both asteroids are magnetic
                               // kill the second one
                               asteroids[j].status = 0;
                               sp1_MoveSprAbs(asteroids[j].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
                          }
                       }
                  }
            }
           } // end if asteroids[j].status == 1

        }  // end j-loop asteroids

      }    // if asteroids[i].status == 1
   }       // end i-loop asteroids
}   


void add_bullet(unsigned char xx, unsigned char yy)
{
  // Find a slot 
  found = 99;  // not found
  for (i = 0; i < NUM_BULLETS && found == 99; i++) {
     if (bullets[i].alive == FALSE)
        found = i;
  }

  // Turn the bullet on 
  if (found != 99)
    { 
      bullets[found].alive = TRUE;
      bullets[found].timer = 19;  // 19 frames

      // start point and direction of bullet 
      if (gun_bit_position == 0 || gun_bit_position == 8) {   // first sprite can be skipped: bug
          bullets[found].x = xx + (7);
          bullets[found].y = yy ;
          bullets[found].xm =  0;
          bullets[found].ym = -4;
      } else if (gun_bit_position == 1) {
          bullets[found].x = xx + (11);
          bullets[found].y = yy + 1;
          bullets[found].xm =  2;
          bullets[found].ym = -3;
      } else if (gun_bit_position == 2) { 
          bullets[found].x = xx + (14);
          bullets[found].y = yy + 4;
          bullets[found].xm = 4;
          bullets[found].ym = 0;
      } else if (gun_bit_position == 3) { 
          bullets[found].x = xx + (11);
          bullets[found].y = yy + 8;
          bullets[found].xm = 2;
          bullets[found].ym = 3;
      } else if (gun_bit_position == 4) {  
          bullets[found].x = xx + (7);
          bullets[found].y = yy + 9;
          bullets[found].xm = 0;
          bullets[found].ym = 4;  
      } else if (gun_bit_position == 5) { 
          bullets[found].x = xx + (2);
          bullets[found].y = yy + 8;
          bullets[found].xm = -2;
          bullets[found].ym = 3;
      } else if (gun_bit_position == 6) { 
          bullets[found].x = xx;
          bullets[found].y = yy + 4;
          bullets[found].xm = -4;
          bullets[found].ym = 0;
      } else if (gun_bit_position == 7) { 
          bullets[found].x = xx + 2;
          bullets[found].y = yy + 1;
          bullets[found].xm = -2;
          bullets[found].ym = -3;
      }
      // disable shield (set timer) 
      shield_timer = 51;

      playfx(FX_FIRE);
      //bit_beepfx_di(beepfx[0].effect); // SHOT

    }  // if (found != 99)

}


void handle_bullets(void)
{      
    for (i = 0; i < NUM_BULLETS; i++)
    {
      if (bullets[i].alive == TRUE)
        {
          bullets[i].x = bullets[i].x + bullets[i].xm;
          bullets[i].y = bullets[i].y + bullets[i].ym;
          
          // Count down
          bullets[i].timer--;

          // Die? 
          if (bullets[i].y < 1 || bullets[i].y > 176 ||    // unsigned char, so range 0-255 
              bullets[i].x < 3 || bullets[i].x > 252  ||
              bullets[i].timer <= 1) {
                  bullets[i].alive = FALSE;
                  sp1_MoveSprAbs(bullets[i].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite
           }
     }
   }
}     


void draw_bullets(void)
{

  for (i = 0; i < NUM_BULLETS; i++)
  {
    if (bullets[i].alive == TRUE) {
        sp1_MoveSprPix(bullets[i].sprite, &cr, 0,  bullets[i].x, bullets[i].y);
    }  // if bullet alive
  }    // for loop
}



void check_bullet_hit(void)
{
  unsigned char a_x, a_y, a_xr, a_yb;       // top-left and bottom-right coordinates of asteroid
  unsigned char b_x, b_y, b_xr, b_yb;       // top-left and bottom-right coordinates of bullet
  unsigned char bullet_hit;

  bullet_hit = FALSE;

  for (j = 0; j < NUM_BULLETS; j++) {

     if (bullets[j].alive == TRUE) {

         b_xr = bullets[j].x + 3;   // width  + 2
         b_yb = bullets[j].y + 3;   // height + 2 
         b_x  = bullets[j].x - 1;   // x - 1
         b_y  = bullets[j].y - 1;   // y - 1

         // check if active bullet has a hit 
         // loop active astroids 
         for (i = 0; i < NUM_ASTEROIDS; i++) {
           if (asteroids[i].status == 1 || asteroids[i].status == 2) {
              a_xr = (asteroids[i].x + 7);   // width  + 1
              a_yb = (asteroids[i].y + 6);   // height + 1
              a_x  = asteroids[i].x;
              a_y  = asteroids[i].y;

              // check overlap of bullet and asteroid
              if (b_xr  > a_x   &&
                  b_x   < a_xr  &&
                  b_yb  > a_y   &&
                  b_y   < a_yb) {

                    // hit! disable asteroid: set status = 3 exploding

                    if (asteroids[i].status == 1) score++;
                    else if (asteroids[i].status == 2) score = score + 3;
                    if (score > high_score)  high_score = score;
                    display_score();

                    // set status 3 : exploding
                    asteroids[i].status = 3;
                    asteroids[i].shape_timer = 0;   // explosion takes x frames
                                                    // NB already set in add_bullet

                    // hide all existing bullets and create 3 new ones
                    for (k = 0; k < NUM_BULLETS; k++) {
                           sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                           bullets[k].alive = TRUE;
                           bullets[k].timer = 12;   // smaller reach than normal bullet
                           bullets[k].x = asteroids[i].x + 3;  // halfway asteroid
                           bullets[k].y = asteroids[i].y + 2;  // halfway asteroid
                    }     
                    bullets[0].xm = 0;   bullets[0].ym = 4;
                    bullets[1].xm = 3;   bullets[1].ym = -3;
                    bullets[2].xm = -3;  bullets[2].ym = -3;

                    // disable shield
                    shield_timer = 51;

                    bullet_hit == TRUE;
                    break;                   // exit inner i-loop (asteroids)
              }
           }  // if status = 1 or 2
         }    // end i-loop active asteroids 


         /* loop active ufos */
         for (i = 0; i < NUM_UFOS; i++) {
           if (ufos[i].status == 1) {
              a_xr = (ufos[i].x + 9);   // width  + 1
              a_yb = (ufos[i].y + 3);   // height + 1
              a_x  = ufos[i].x;
              a_y  = ufos[i].y;

              /* check overlap of bullet and ufo */
              if (b_xr  > a_x   &&
                  b_x   < a_xr  &&
                  b_yb  > a_y   &&
                  b_y   < a_yb) {

                    // disable ufo: set status = 3 exploding
                    //play_sound(3, 3);

                    /* increase score */
                    score = score + 10;
                    if (score > high_score) high_score = score;
                    display_score();

                    ufos[i].status = 3;
                    ufos[i].shape_timer = 0;   // explosion takes x images

                    /* disable all bullets */
                    for (k = 0; k < NUM_BULLETS; k++)
                         bullets[k].alive = 0;

                    /* create 3 new bullets (=explosion bits) */
                    for (k = 0; k < NUM_BULLETS; k++) {
                           sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                           bullets[k].alive = 1;
                           bullets[k].timer = 15;  
                           bullets[k].x = ufos[i].x + 4;  // halfway ufo
                           bullets[k].y = ufos[i].y + 1;  // halfway ufo
                    }      
                    bullets[0].xm = 0;   bullets[0].ym = 4;
                    bullets[1].xm = 3;   bullets[1].ym = -3;
                    bullets[2].xm = -3;  bullets[2].ym = -3;

                    // disable shield
                    shield_timer = 51;

                    bullet_hit == TRUE;
                    break;                   // exit inner i-loop (asteroids)

              }  // check overlap

           }  // if status = 1 
         }    // end loop active ufos

         if (bullet_hit == TRUE) break;     // exit outer j-loop (bullets)

       }      // end if bullet alive

  } // end loop active bullets j-loop
}


void add_laser(unsigned char xx, unsigned char yy, 
               signed char xxm,  signed char yym, 
               unsigned char ufo)
{
  // Find a slot
  found = 99;
  for (i = 0; i < NUM_LASERS && found == 99; i++) {
     if (lasers[i].alive == FALSE)
        found = i;
  }
  
  // Turn the laser on
  if (found != 99)
    { 
      lasers[found].alive = TRUE;
      lasers[found].fired_by_ufo = ufo;
      /* start point and direction of laser */   
      lasers[found].x = xx;
      lasers[found].y = yy;
      lasers[found].xm = xxm * 4;  
      lasers[found].ym = yym * 4;  
      //playfx(FX_MOVE);  // fire sound (same as bullet fire)
      bit_beepfx_di(beepfx[0].effect);
      //play_sound(8, -1);   
    }  // if (found != 99)

}


void handle_lasers(void)
{
    for (i = 0; i < NUM_LASERS; i++)
    {
      if (lasers[i].alive == TRUE) {
          lasers[i].x = lasers[i].x + lasers[i].xm;
          lasers[i].y = lasers[i].y + lasers[i].ym;
          
          // Die?
          if (lasers[i].y < 0 || lasers[i].y >= 172 ||
              lasers[i].x < 0 || lasers[i].x >= 255  ) {
                  lasers[i].alive = FALSE;
                  lasers[i].fired_by_ufo = 99;
                  sp1_MoveSprAbs(lasers[i].sprite, &cr, NULL, 0, 34, 0, 0); // hide 
          }      

     }
   }     // for loop
}


void draw_lasers(void)
{
  unsigned char image_num;

  for (i = 0; i < NUM_LASERS; i++)
  {
    if (lasers[i].alive == TRUE) {

      // left or right laser
      
      if ( (lasers[i].xm < 0 && lasers[i].ym < 0) ||
           (lasers[i].xm > 0 && lasers[i].ym > 0) ) {
             image_num = 64;
             sp1_MoveSprPix(lasers[i].sprite, &cr, (void *) (size_t) image_num,  lasers[i].x, lasers[i].y);  // \ laser  
      } else { 
             image_num = 32;
             sp1_MoveSprPix(lasers[i].sprite, &cr, (void *) (size_t) image_num, lasers[i].x, lasers[i].y);  // / laser
      }
    }  // alive == TRUE  
  }    // for loop
}


void check_laser_hit(void)
{
  unsigned char a_x, a_y, a_xr, a_yb;       // top-left and bottom-right coordinates of asteroid
  unsigned char b_x, b_y, b_xr, b_yb;       // top-left and bottom-right coordinates of laser
  
  for (j = 0; j < NUM_LASERS; j++) {

     if (lasers[j].alive == TRUE) {

         b_xr = lasers[j].x + 8;   // width  
         b_yb = lasers[j].y + 7;   // height 
         b_x  = lasers[j].x;
         b_y  = lasers[j].y;

         // check if laser hits ship 
         // check overlap of laser and ship (center of ship)
         if (  player_x + 15        > b_x   &&
               player_x             < b_xr  &&
               player_y + 10        > b_y   &&
               player_y             < b_yb) {

             // is shield down?
             if (shield_timer > 0 && player_dying == FALSE) { // shield is down
                  //printf("HIT BY LASER, SHIELD WAS DOWN\n");
                  player_dying = TRUE;
                  player_explosion_nr = 0;
                  player_dying_animation = 1;
                  player_dying_countdown = 45;

                  ink_colour = BRIGHT | INK_WHITE | PAPER_BLACK;  // change colour player to white    
                  sp1_IterateSprChar(player_sprite, add_colour_to_sprite);

                  lasers[j].alive = FALSE;    // hide laser which hit player
                  lasers[j].fired_by_ufo = 99;
                  sp1_MoveSprAbs(lasers[j].sprite, &cr, NULL, 0, 34, 0, 0);

             } else {
                  //printf("HIT BY LASER, SHIELD WAS UP\n");
                  // sp1_SetPrintPos(&ps0, 22, 15);
                  // sp1_PrintString(&ps0, "\x14\x47" "LASER HIT");

                  // hide all existing bullets and create 3 new ones
                for (k = 0; k < NUM_BULLETS; k++) {
                       sp1_MoveSprAbs(bullets[k].sprite, &cr, NULL, 0, 34, 0, 0); // hide sprite

                       bullets[k].alive = TRUE;
                       bullets[k].timer = 12;   // smaller reach than normal bullet
                       bullets[k].x = player_x + 7;  // halfway 
                       bullets[k].y = player_y + 5;  // 
                }     
                bullets[0].xm = 0;
                bullets[0].ym = 4;
                bullets[1].xm = 3;
                bullets[1].ym = -3;
                bullets[2].xm = -3;
                bullets[2].ym = -3;


                lasers[j].alive = FALSE;    // hide laser which hit player
                lasers[j].fired_by_ufo = 99;
                sp1_MoveSprAbs(lasers[j].sprite, &cr, NULL, 0, 34, 0, 0);
                // disable shield
                shield_timer = 51;

                bit_beepfx_di(beepfx[1].effect); 

             } // end shield is up/down (found ==1)

             
         }  // end check overlap laser and ship

 
         // check if laser has a hit an asteroid

         /* loop active asteroids */
         for (i = 0; i < NUM_ASTEROIDS; i++) {
           if (asteroids[i].status == 1 || asteroids[i].status == 2) {
              a_xr = (asteroids[i].x + 6);   // width 
              a_yb = (asteroids[i].y + 5);   // height
              a_x  = asteroids[i].x;
              a_y  = asteroids[i].y;

              /* check overlap of laser and asteroid */
              if (b_xr  > a_x   &&
                  b_x   < a_xr  &&
                  b_yb  > a_y   &&
                  b_y   < a_yb) {

                    //printf("laser hit on astroid color %d\n", asteroids[i].colour);  
                    lasers[j].alive = FALSE;
                    lasers[j].fired_by_ufo = 99;
                    sp1_MoveSprAbs(lasers[j].sprite, &cr, NULL, 0, 34, 0, 0);
                    //add_mini_explosion(laser[j].x , laser[j].y);    
                    bit_beepfx_di(beepfx[1].effect);  

                    if (asteroids[i].status == 1) {
                      asteroids[i].status = 2;          // make magnetic if asteroid hit by laser
                      asteroids[i].magnetic_timer = 0;
                    }                    
                  
              }    // check overlap laser and asteroid

            }      // asteroid == 1 or 2
          }        // loop asteroids


      }     // laser alive

  } //end active lasers

}



void add_ufo(void)
{
 unsigned char direction;
  
  // Find a slot: 
  found = 99;
  for (i = 0; i < NUM_UFOS && found == 99; i++) {
     if (ufos[i].status == 0)
        found = i;
  }
  
  // Turn the ufo on: 
  // (size is 8 pixels wide and 2 pixels tall) 
  if (found != 99) {
      //printf("UFO created\n");  
      ufos[found].status      = 1;                      // active

      ufos[found].colour       = (rand() % 5) + 1;       // random 1-5
      get_ink_colour(ufos[found].colour);
      ink_colour = BRIGHT | ink_colour;
      sp1_IterateSprChar(ufos[found].sprite, add_colour_to_sprite);

      ufos[found].shape_timer = ASTEROID_SHAPE_TIMER;   // count_down timer for explosion

      // random 1 of 4 starting positions 
      //   but always moving diagonal and speed 2
      direction = rand() %4 + 1; 
        switch (direction) {
        case (1):
          /* spawn from top */
          ufos[found].x  = ( (rand() % (255 - 80)) + 40);
          ufos[found].y  = 0;                    // height of ufo out of screen
          ufos[found].xm = (-4 * (rand()%2))  + 2;         // values -2 or 2) 
          ufos[found].ym = 2;    
          break;
        case (2):
          /* spawn from bottom */
          ufos[found].x  = ( (rand() % (255 - 80)) + 40);
          ufos[found].y  = 176 - (2);   // height of ufo out of screen
          ufos[found].xm = (-4 * (rand()%2))  + 2;         // values -2 or 2) 
          ufos[found].ym = -2;
          break;
        case (3):
          /* spawn from left */
          ufos[found].x  = 0;                     // width of ufo out of screen
          ufos[found].y  = ( (rand() % (176 - 80)) + 20);
          ufos[found].xm = 2;
          ufos[found].ym = (-4 * (rand()%2))  + 2;          // values -2 or 2) 
          break;
        case (4):
          /* spawn from right */
          ufos[found].x  = 255 - (8);      // width of asteroid out of screen
          ufos[found].y  = ( (rand() % (176 - 80)) + 20);
          ufos[found].xm = -2;
          ufos[found].ym = (-4 * (rand()%2))  + 2;          // values -2 or 2) 
          break;
        }
        bit_beepfx_di(beepfx[7].effect);

  }  // if not found 
}


void handle_ufos(void)
{
   unsigned char lx, ly;
   signed char   xm, ym;
   unsigned char laser_for_ufo_added;
   unsigned char laser_type;    // 1 = backward down     3 = forward down
                                // 2 = backward up       4 = forward up

   for (i = 0; i < NUM_UFOS; i++)
   {
     if (ufos[i].status == 1)
     {
       // Move: 
        ufos[i].x = ufos[i].x + ufos[i].xm;
        ufos[i].y = ufos[i].y + ufos[i].ym;
        ufos[i].shape_timer--;;          
        
        // timer for ufo explosion
        if (ufos[i].shape_timer == 0) {
            ufos[i].shape_timer = ASTEROID_SHAPE_TIMER ;
        }

        // Is ufo off-screen? 
        if (ufos[i].x < -8 || ufos[i].x >= 255 + (8) ||
            ufos[i].y < -2 || ufos[i].y >= 176 + (2)) {
            ufos[i].status = 0;
            sp1_MoveSprAbs(ufos[i].sprite, &cr, NULL, 0, 34, 0, 0);

            //if (Mix_Playing(6)) Mix_HaltChannel(6);
            //printf("-- ufo off screen: removed...\n");
        }


        /* If possible fire a laser
           First check if UFO already has an active laser, if so, don't fire 
           a possible second laser for this ufo */
        
        laser_for_ufo_added = 99;
        for (k = 0; k < NUM_LASERS && laser_for_ufo_added == 99; k++) {
          if (lasers[k].fired_by_ufo == i) {
            laser_for_ufo_added = i;  // was = j ??????
          }
        } // end loop 

    
        for (j = 0; j < NUM_LASERS && laser_for_ufo_added == 99; j++)  
        {
          if ( lasers[j].alive == 1) {   // skip active lasers
              ; 
          } else {


              // fire laser if ship is in range 
              lx = ufos[i].x + 2;               // possible laser x starting point
              ly = ufos[i].y;                   // possible laser y starting point

              // check if backward or forward laser can hit ship 
              laser_type = 0;
                if ( abs( ((player_x + 2) - (lx-ly))  -  ((player_y + 2)) ) <= 2) {   // middle of ship +- 2        
                 if (player_y > ufos[i].y + 4) {
                   laser_type = 1;
                 } else {   
                   laser_type = 2;
                 }
              }
                if ( abs( (lx - (player_x - 2)) - ((player_y + 2) - ly) ) <= 2) {   // middle of ship +- 2
                 if (player_y > ufos[i].y + 4) {
                   laser_type = 3;
                 } else {   
                   laser_type = 4;
                 }
              }

              if (laser_type != 0) {
                  switch (laser_type) {
                    case 1:
                      xm = 1;
                      ym = 1;
                      break;
                    case 2:
                      xm = -1;
                      ym = -1;
                      break;
                    case 3:
                      xm = -1;
                      ym = 1;
                      break;
                    case 4:
                      xm = 1;
                      ym = -1;
                      break;
                  }   

               add_laser(ufos[i].x + (2), ufos[i].y, xm, ym, i );
               laser_for_ufo_added = i;
              }    
           }  // end laser[j].alive == 1 && laser[j].fired_by_ufo == i   
        }     // end j-loop
        

     } else {
        if (ufos[i].status == 3) {   // exploding
          ufos[i].shape_timer++;
          if (ufos[i].shape_timer == 5) {
                //  after explosion disable ufo completely
                ufos[i].status = 0;
                sp1_MoveSprAbs(ufos[i].sprite, &cr, NULL, 0, 34, 0, 0);  // hide
          } else {
              if (ufos[i].shape_timer == 2) bit_beepfx_di(beepfx[1].effect); // HIT
          }      
        }

     } // ufos[i].status == 1

   }  // end for i-loop
}


void draw_ufos(void)
{
  unsigned char image_num;

  for (i = 0; i < NUM_UFOS; i++)  {

    if (ufos[i].status == 1) {  
        image_num = 32;   // normal (2nd image because of bug?)
    } else {
       
       if (ufos[i].status == 3) { // exploding
              if (ufos[i].shape_timer >= 1 && ufos[i].shape_timer <= 2) 
                   image_num = 64;   // explosion 1
              else image_num = 96;   // explosion 2
        } 
    }   // status 1 or 3

    if (ufos[i].status >= 1) {
       sp1_MoveSprPix(ufos[i].sprite, &cr, 
                       (void *) (size_t) image_num,
                       ufos[i].x, ufos[i].y);
    }  

   }    // for loop  
}



unsigned char block_of_ram[5000];   // ?? TO DO zie hier direct onder


void setup(void)
{
   zx_border(INK_BLUE);

   // set up the block memory allocator with one queue
   // max size requested by sp1 will be 24 bytes or block size of 25 (+1 for overhead)
   balloc_reset(0);                                              // make queue 0 empty
   balloc_addmem(0, sizeof(block_of_ram)/25, 24, block_of_ram);  // add free memory from bss section
   balloc_addmem(0, 8, 24, (void *)0xd101);                      // another eight from an unused area

   // interrupt mode 2
   setup_int();

   sp1_Initialize(SP1_IFLAG_MAKE_ROTTBL | SP1_IFLAG_OVERWRITE_TILES | SP1_IFLAG_OVERWRITE_DFILE, INK_BLACK | PAPER_BLACK, ' ');
   
   ps0.bounds = &cr;
   ps0.flags = SP1_PSSFLAG_INVALIDATE;
   ps0.visit = 0;

   intrinsic_ei();
}


void start_new_game(void) 
{
  player_dying = FALSE;
  score = 0;
  speed = 3;             // speed of player 
  gun_bit_position = 0;
  bullet_frame = frame;    
  shield_timer = 0;
  player_dying = FALSE;
  player_destroyed = FALSE;
  player_dying_animation = 0;
  player_x = 124;
  player_y = 88;
  player_animation_frame = 0;
  ufo_start_delay = frame;     
  
  // init ufo  
  for (i = 0; i < NUM_UFOS; i++)
      ufos[i].status = 0;

  // init lasers 
  for (i = 0; i < NUM_LASERS; i++) {
      lasers[i].alive = FALSE;
      lasers[i].fired_by_ufo = 99;
  }

  // init bullets 
  for (i = 0; i < NUM_BULLETS; i++) 
       bullets[i].alive = FALSE;

  // init asteroids
  for (i = 0; i < NUM_ASTEROIDS; i++)
      asteroids[i].status = 0;

  display_score();
}



void hide_sprites(void)
{
  for (i = 0; i < NUM_ASTEROIDS; i++)  {
    sp1_MoveSprAbs(asteroids[i].sprite, &cr, NULL, 0, 34, 0, 0);  // remove from screen
                                                               // print at column 34
    get_ink_colour(asteroids[i].colour);  // set to original colour
    sp1_IterateSprChar(asteroids[i].sprite, add_colour_to_sprite);
  }  

  for (i = 0; i < NUM_BULLETS; i++) 
       sp1_MoveSprAbs(bullets[i].sprite, &cr, NULL, 0, 34, 0, 0); // hide bullets

  for (i = 0; i < NUM_UFOS; i++) 
       sp1_MoveSprAbs(ufos[i].sprite, &cr, NULL, 0, 34, 0, 0); // hide 

  for (i = 0; i < NUM_LASERS; i++) 
       sp1_MoveSprAbs(lasers[i].sprite, &cr, NULL, 0, 34, 0, 0); // hide 

  sp1_MoveSprAbs(player_sprite, &cr, NULL, 0, 34, 0, 0);  // remove player from screen

  sp1_UpdateNow();
}


void delete_sprites(void)
{
   for (i = 0; i < NUM_ASTEROIDS; i++)  sp1_DeleteSpr(asteroids[i].sprite); 
   for (i = 0; i < NUM_BULLETS; i++)    sp1_DeleteSpr(bullets[i].sprite); 
   for (i = 0; i < NUM_UFOS; i++)       sp1_DeleteSpr(ufos[i].sprite); 
   for (i = 0; i < NUM_LASERS; i++)     sp1_DeleteSpr(lasers[i].sprite); 
   sp1_DeleteSpr(player_sprite); 
}



int main(void)
{
   unsigned char idle = 0;

   setup();
   zx_border(INK_BLACK);

   // set up the block memory allocator with one queue
   // max size requested by sp1 will be 24 bytes or block size of 25 (+1 for overhead)
   balloc_reset(0);                                              // make queue 0 empty
   balloc_addmem(0, sizeof(block_of_ram)/25, 24, block_of_ram);  // add free memory from bss section
   balloc_addmem(0, 8, 24, (void *)0xd101);                      // another eight from an unused area

   // interrupt mode 2
   setup_int();

   sp1_Initialize(SP1_IFLAG_MAKE_ROTTBL | SP1_IFLAG_OVERWRITE_TILES | SP1_IFLAG_OVERWRITE_DFILE, INK_BLACK | PAPER_BLACK, ' ');
   
   ps0.bounds = &cr;
   ps0.flags = SP1_PSSFLAG_INVALIDATE;
   ps0.visit = 0;

   intrinsic_ei();

   draw_menu();

   srand(tick);  // 256 different games are possible
   high_score = 0;
   //active_pills = NUM_PILLS;

   while(1)
   {
      key = in_inkey();
      if (key) {
         if (key == '4') {
            playfx(FX_SELECT);

            in_wait_nokey();
            run_redefine_keys();
            idle = 0;
         }


         if (key == '1' || key == '2' || key == '3') {
            playfx(FX_SELECT);

            joy_k.left  = in_key_scancode(keys[0]);
            joy_k.right = in_key_scancode(keys[1]);
            joy_k.down  = in_key_scancode(keys[3]);
            joy_k.up    = in_key_scancode(keys[2]);
            joy_k.fire  = in_key_scancode(keys[4]);

            if (key == '1') joyfunc = (JOYFUNC)in_stick_keyboard;
            if (key == '2') joyfunc = (JOYFUNC)in_stick_kempston;
            if (key == '3') joyfunc = (JOYFUNC)in_stick_sinclair1;

            run_intro();

            run_play();
            idle = 0;
            draw_menu();
         }
      }

      if (idle++ == 255) {
         // go back to the welcome screen after a while
         // if the player doesn't do anything
         idle = 0;
         draw_menu();
      }

      wait();
      sp1_UpdateNow();
   }
}

