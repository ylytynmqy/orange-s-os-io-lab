
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               tty.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#include "type.h"
#include "const.h"
#include "protect.h"
#include "proto.h"
#include "string.h"
#include "proc.h"
#include "global.h"
#include "keyboard.h"

#define SCREEN_SCROLL_DOWN -1
#define SCREEN_SCROLL_UP 1
#define SCREEN_SIZE (80 * 25)
#define SCREEN_WIDTH 80

#define TAB_WIDTH 4//tab width
#define DEFAULT_COLOR 0x07//white
#define SPECIAL_COLOR 0x09//blue

#define INTERVAL (20*10000)//20sec
/*
0 input
1 esc input
2 esc search
*/
int state = 0;
PRIVATE u32 base_cursor=0; 

PRIVATE u32 cursor = 0;
PRIVATE u32 current_start_addr = 0;
PRIVATE v_mem_limit = 0;

PUBLIC u8* get_pointer_at_cursor() {
    return (u8 *)(V_MEM_BASE + cursor * 2);
}

PUBLIC void set_char_at_cursor(u8 ch, u8 color) {
    u8 *p_vmem = get_pointer_at_cursor();
    p_vmem[0]=ch;
    p_vmem[1]=color;
}



/* console part */
PUBLIC void set_cursor(u32 pos)
{
    disable_int();
    out_byte(CRTC_ADDR_REG, CURSOR_H);
    out_byte(CRTC_DATA_REG, (pos >> 8) & 0xFF);
    out_byte(CRTC_ADDR_REG, CURSOR_L);
    out_byte(CRTC_DATA_REG, pos & 0xFF);
    enable_int();
}

PRIVATE void set_video_start_addr(u32 addr)
{
    disable_int();
    out_byte(CRTC_ADDR_REG, START_ADDR_H);
    out_byte(CRTC_DATA_REG, (addr >> 8) && 0xFF);
    out_byte(CRTC_ADDR_REG, START_ADDR_L);
    out_byte(CRTC_DATA_REG, addr & 0xFF);
    enable_int();
}

PRIVATE void sync_video()
{
    set_cursor(cursor);
    set_video_start_addr(current_start_addr);
}

PUBLIC void scroll_screen(int direction)
{
    if (direction == SCREEN_SCROLL_UP)
    {
        if (current_start_addr > 0)
        {
            current_start_addr -= SCREEN_WIDTH;
        }
    }
    else if (direction == SCREEN_SCROLL_DOWN)
    {
        if (current_start_addr + SCREEN_SIZE < V_MEM_SIZE)
        {
            current_start_addr += SCREEN_WIDTH;
        }
    }
    sync_video();
}

PUBLIC void output(char ch, u8 default_color)
{
    switch (ch)
    {
    case '\t':
        set_char_at_cursor('\t', 0x0);
        cursor = TAB_WIDTH * (cursor/TAB_WIDTH + 1);
        break;
    case '\n':
        set_char_at_cursor('\n', 0x0);
        cursor = SCREEN_WIDTH * (cursor / SCREEN_WIDTH + 1);
        break;
    case '\b': {//backspace
        u32 row = cursor/SCREEN_WIDTH;
        int found = 0;
        if (cursor % SCREEN_WIDTH==0) {
       
            u32 previous_start= (row == 0) ? 0 :SCREEN_WIDTH * (row-1); // the start of the previous row. if it's in row 1, assign 0
            u32 previous_cursor=cursor;

            u32 move_count = 0;
            while (cursor>0 && move_count<=SCREEN_WIDTH) {
                cursor--;
                move_count++;
                u8 *p = get_pointer_at_cursor();
                if (p[0]== '\n') {
                    found = 1;

                    p[0]= ' ';
                    p[1] = DEFAULT_COLOR;
                    break;
                }
            }
            if (!found) {
                cursor = previous_cursor;
            }
        } 
        if (!found && cursor % TAB_WIDTH==0) {
            u32 previous_start = TAB_WIDTH * (cursor/TAB_WIDTH - 1); 
 
            u32 previous_cursor=cursor;
            u32 move_count = 0;

            while (cursor>0 && move_count<=TAB_WIDTH) {
                cursor--;
                move_count++;
                u8 *p = get_pointer_at_cursor();
                if (p[0]== '\t') {
                    found = 1;

                    p[0]= ' ';
                    p[1] = DEFAULT_COLOR;
                    break;
                }
            }
            if (!found) {
                cursor = previous_cursor;
            }
        }
        if (!found && cursor>0) {
            cursor--;
            set_char_at_cursor(' ', DEFAULT_COLOR);
        }
        break;
    }
    default:
        set_char_at_cursor(ch, default_color);
        cursor++;
    }
    while (cursor >= current_start_addr + SCREEN_SIZE)
    {
        scroll_screen(SCREEN_SCROLL_DOWN);
    }
    sync_video();
}

PUBLIC void clear_screen()
{
    if (state != 0) {
        return;
    }
    u8 *base = (u8 *)V_MEM_BASE;
    for (int i = 0; i < V_MEM_SIZE; i += 2)
    {
        base[i] = ' ';
        base[i + 1] = DEFAULT_COLOR;
    }
    cursor = 0;
    sync_video();
}

PUBLIC void init_screen()
{
    int v_mem_size = V_MEM_SIZE >> 1;
    v_mem_limit = v_mem_size;
    current_start_addr = 0;
    cursor = 0;
    disp_pos = 0;

    state = 0;

    set_cursor(cursor);
}

/*======================================================================*
                           task_tty
 *======================================================================*/
PUBLIC void task_tty()
{
    init_screen();

    while (1)
    {
        keyboard_read();//in keyboard.c
    }
}

PUBLIC void task_timely_clear_screen() {
    while (1) {
        clear_screen();
		
        milli_delay(INTERVAL);
    }
}

/*======================================================================*
				in_process used in keyboard.c
 *======================================================================*/
PUBLIC void in_process(u32 key)
{
    char output[2] = {'\0', '\0'};

    if (state == 2) {
        if ((key & MASK_RAW) == ESC) {
            while(cursor>=base_cursor) {
                set_char_at_cursor(' ', DEFAULT_COLOR);
                cursor--;
            }

            for(cursor=0;cursor<=base_cursor;cursor++) {
                u8* p = get_pointer_at_cursor();
                if (p[1] == SPECIAL_COLOR) {
                    p[1] = DEFAULT_COLOR;
                }
            }
            cursor--;
            sync_video();
            state = 0;

        }
        return;
    }


    if (!(key & FLAG_EXT))
    {
		u8 color;
		if(state==1){
			color=SPECIAL_COLOR;
		}
		else{
			color=DEFAULT_COLOR;
		}
		output(key,color);
    }
    else
    {
        int raw_code = key & MASK_RAW;
        switch (raw_code)
        {

        case ENTER:
            if (state == 1) {
                state =2;
                u32 length = cursor-base_cursor;
                u32 cursor_start=0, cursor_end=length;

                u8* content = V_MEM_BASE;

                while(cursor_end<=base_cursor) {
                    int match = 1;
                    for (u32 i=0;i<length;i++){
                        if (content[(cursor_start+i)*2] != content[(base_cursor+i)*2]) {
                            match = 0;;
                            break;
                        }
                    }
                    if (match ==0) {
                        cursor_start++;
                        cursor_end++;
                    } else {
                        for (u32 i=cursor_start;i<cursor_end;i++) {
                            content[i*2+1] = SPECIAL_COLOR;
                        }
                        cursor_start+=length;
                        cursor_end+=length;
                    }
                }

            } else {
                output('\n', DEFAULT_COLOR);
            }

            break;
        case BACKSPACE:
            output('\b',DEFAULT_COLOR);
            break;
        case TAB:
            output('\t',DEFAULT_COLOR);
            break;
        case ESC:
            if (state == 0) {
                state = 1;
                base_cursor = cursor;
            }
            break;
        case PAGEUP:
            scroll_screen(SCREEN_SCROLL_UP);
            break;
        case PAGEDOWN:
            scroll_screen(SCREEN_SCROLL_DOWN);
            break;
        default:
            break;
        }
    }
}
