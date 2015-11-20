/*******************************************************************
    Copyright (C) 2009 FreakLabs
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. Neither the name of the the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.

    Originally written by Christopher Wang aka Akiba.
    Please post support questions to the FreakLabs forum.

*******************************************************************/
/*!
    \file Cmd.c

    This implements a simple command line interface for the Arduino so that
    its possible to execute individual functions within the sketch.
*/
/**************************************************************************/
#include <avr/pgmspace.h>
#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif
#include "HardwareSerial.h"
#include "Cmd.h"


#define CMD_HISTORY_LEN 11
#define CMD_HISTORY_WRAP(x)   ((x) < 0) ? (CMD_HISTORY_LEN - 1) : (x >= CMD_HISTORY_LEN) ? 0 : (x)
#define ARGC_MAX 12

static uint8_t cmd_history[CMD_HISTORY_LEN][MAX_MSG_SIZE] = {{0}};
static int8_t history_ptr = 0;            // Stores the current place in the history buffer.
static int8_t history_temp_index = 0;     // Stores where the current command is looking.


// command line message buffer and pointer
static uint8_t *msg;
static uint8_t *msg_ptr;
static int8_t cursor_pos = 0;

// linked list for command table
static cmd_t *cmd_tbl_list, *cmd_tbl;

// text strings for command prompt (stored in flash)
#if CMD_LEGACY
#define BANNER_LEN 40
#define PROMPT_LEN 7
const char cmd_banner[BANNER_LEN] PROGMEM = "*************** CMD *******************";
const char cmd_prompt[PROMPT_LEN] PROGMEM = "CMD >> ";

#else
#define PROMPT_LEN 3
#define BANNER_LEN PROMPT_LEN
const char cmd_prompt[PROMPT_LEN] PROGMEM = "$ ";
#endif

#define UNREC_LEN 30
char cmd_unrecog[UNREC_LEN] PROGMEM = "CMD: Command not recognized.";
#define PRINTER_LEN (UNREC_LEN > BANNER_LEN) ? UNREC_LEN : BANNER_LEN


static Stream* stream;

/**************************************************************************/
/*!
    Generate the main command prompt
*/
/**************************************************************************/
void cmd_display()
{

    char buf[PRINTER_LEN];
    //stream->println();

#if CMD_LEGACY 
    strcpy_P(buf, cmd_banner);
    stream->println(buf);
#else
    strcpy_P(buf, cmd_prompt);
    stream->print(buf);
#endif

    return;
}

/**************************************************************************/
/*!
    Parse the command line. This function tokenizes the command input, then
    searches for the command table entry associated with the commmand. Once found,
    it will jump to the corresponding function.
*/
/**************************************************************************/
void cmd_parse(char *cmd)
{
    uint8_t argc, i = 0;
    char *argv[30];
    char buf[PRINTER_LEN];
    cmd_t *cmd_entry;

#ifdef CMD_LEGACPRINTER_LEN
    fflush(stdout);
#endif

    // parse the command line statement and break it up into space-delimited
    // strings. the array of strings will be saved in the argv array.
    argv[i] = strtok(cmd, " ");
    do
    {
        argv[++i] = strtok(NULL, " ");
    } while ((i < ARGC_MAX) && (argv[i] != NULL));

    // save off the number of arguments for the particular command.
    argc = i;

    // parse the command table for valid command. used argv[0] which is the
    // actual command name typed in at the prompt
    for (cmd_entry = cmd_tbl; cmd_entry != NULL; cmd_entry = cmd_entry->next)
    {
        if (!strcmp(argv[0], cmd_entry->cmd))
        {
            cmd_entry->func(argc, argv);
            cmd_display();
            return;
        }
    }

    // command not recognized. print message and re-generate prompt.
    strcpy_P(buf, cmd_unrecog);
    stream->println(buf);

    cmd_display();
}

/**************************************************************************/
/*!
    This function processes the individual characters typed into the command
    prompt. It saves them off into the message buffer unless its a "backspace"
    or "enter" key.
*/
/**************************************************************************/
void cmd_handler()
{


   /* Used to copy actual command to so that history isn't corrupted */
   char temp_cmd[MAX_MSG_SIZE];

   /* buffer for checking vt_commands */
   char vt_cmd[VT100_CMD_LEN];

   char c = stream->peek();

   uint8_t *tmp_msg_ptr = msg_ptr;

   /* Deal With a control command */
   if(c == VT100_ESC)
   {

      /* wait for the whole command to come through */
      while(stream->available() < VT100_CMD_LEN)
         ;

      /* Read the command into a buffer */
      for(uint8_t i = 0; i < VT100_CMD_LEN; i++)
      {
         vt_cmd[i] = stream->read();
      }

      if(strncmp(vt_cmd, VT100_CURSOR_UP, VT100_CMD_LEN) == 0)
      {

         stream->print(VT100_ERASE_TO_START_LINE);
         stream->print(cmd_prompt);

         /* Go back one message relative to the current pos (Handle Wrap)*/
         history_temp_index = CMD_HISTORY_WRAP(history_temp_index - 1);

         /* Copy history temp index into current command buffer */
         msg_ptr = msg + snprintf((char*)msg, MAX_MSG_SIZE, "%s", (char*)cmd_history[history_temp_index]);
         cursor_pos = msg_ptr - msg;
         stream->print((char*)msg);

      }

      else if(strncmp(vt_cmd, VT100_CURSOR_DOWN, VT100_CMD_LEN) == 0)
      {

         stream->print(VT100_ERASE_TO_START_LINE);
         stream->print(cmd_prompt);

         /* Go back one message relative to the current pos (Handle Wrap)*/
         history_temp_index = CMD_HISTORY_WRAP(history_temp_index + 1);

         /* Copy history temp index into current command buffer */
         msg_ptr = msg + snprintf((char*)msg, MAX_MSG_SIZE, "%s", (char*)cmd_history[history_temp_index]);
         cursor_pos = msg - msg_ptr;
         stream->print((char*)msg);

      }

      else if(strncmp(vt_cmd, VT100_CURSOR_RIGHT, VT100_CMD_LEN) == 0)
      {
         if(cursor_pos < (msg_ptr - msg))
         {
            stream->print(VT100_CURSOR_RIGHT);
            cursor_pos++; 
         }
      }

      else if(strncmp(vt_cmd, VT100_CURSOR_LEFT, VT100_CMD_LEN) == 0)
      { 
         /* If the cursor pos is not at the beginning */

         if(cursor_pos > 0)
         {
            stream->print(VT100_CURSOR_LEFT);
            cursor_pos--;
         }
      }

   } //if(c == VT100_ESC)

   else
   {

      c = stream->read();
      switch (c)
      {
         case '.':
         case '\r':
            // terminate the msg and reset the msg ptr. then send
            // it to the handler for processing.
            *msg_ptr = '\0';         // Null Terminate the message 

            stream->print("\r\n");   // Print the return characters

            /* CMD parse changes the string, so we need to pass only a copy */
            snprintf(temp_cmd, MAX_MSG_SIZE, "%s", msg);
            cmd_parse((char *)temp_cmd);

            /* Wrap the history pointer */
            history_ptr = CMD_HISTORY_WRAP(history_ptr + 1);

            /* Reset the history_temp_index for this command */
            history_temp_index = history_ptr;

            /* get the latest message buffer ptr (From next history) */
            msg = cmd_history[history_ptr];           // Reset The message pointer to the next history location.

            /* Reset the message */
            memset(msg, 0, MAX_MSG_SIZE);

            msg_ptr = msg;
            cursor_pos = 0;

         break;

         /* Send Help / Usage */
#ifdef USE_HELP
         case '?':

            stream->println();
            stream->println("Printing Help: ");

            /* Loop through the command table and print the commands */
            for (cmd_t* cmd_entry = cmd_tbl; cmd_entry != NULL; cmd_entry = cmd_entry->next)
            {
               stream->println(cmd_entry->cmd);
            }

            cmd_display();
            stream->print((char*)msg);

         break;
#endif

         case 127:
         case '\b':

            /* If you won't end up before the message buffer by deleting a character */
            if ((msg_ptr > msg) && (cursor_pos > 0))
            {
               tmp_msg_ptr = msg + (cursor_pos - 1);
               while(tmp_msg_ptr <= msg_ptr){
                  *tmp_msg_ptr = *(tmp_msg_ptr + 1);
                  tmp_msg_ptr++;
               }

               tmp_msg_ptr = msg_ptr;
               /* reset the rest of the message */
               while(tmp_msg_ptr < msg + MAX_MSG_SIZE)
               {
                  *(tmp_msg_ptr++) = 0;
               }

               msg_ptr--;
               cursor_pos--;

               stream->print(c);
               stream->print(VT100_ERASE_TO_END_LINE);
               stream->print((char*)(msg + cursor_pos));
         
               tmp_msg_ptr = msg + cursor_pos;
               
               /* move the cursor back to where it came */
               while(tmp_msg_ptr < msg_ptr)
               {
                  stream->print(VT100_CURSOR_LEFT);
                  tmp_msg_ptr++;
               }

            }


         break;

      default:

         if((msg - msg_ptr) < MAX_MSG_SIZE){
            /* ================= ADD Character ================== */

            /* Copy all chars forward */
            tmp_msg_ptr = msg_ptr;

            while(tmp_msg_ptr >= (msg + cursor_pos))
            {
               /* Copy the data forward */
               *(tmp_msg_ptr + 1) = *tmp_msg_ptr;
               tmp_msg_ptr--;
            }

            /* Copy in new message pointer */
            *(tmp_msg_ptr + 1) = c;

            msg_ptr++;

            stream->print(VT100_ERASE_TO_END_LINE);
            stream->print((char*)(msg + cursor_pos));

            cursor_pos++;

            /* move the cursor back to where it came */
            tmp_msg_ptr = msg + cursor_pos;
            while(tmp_msg_ptr < msg_ptr)
            {
               stream->print(VT100_CURSOR_LEFT);
               tmp_msg_ptr++;
            }

            /* ============== END ADD CHARACTER ================== */
         }
         break;
      } // switch(c)

    }//else
}

void cmd_add_character(uint8_t * buff, uint8_t * msg_ptr, uint8_t cursor_pos, char c)
{

}

/**************************************************************************/
/*!
    This function should be set inside the main loop. It needs to be called
    constantly to check if there is any available input at the command prompt.
*/
/**************************************************************************/
void cmdPoll()
{
    while (stream->available())
    {
        cmd_handler();
    }
}

/**************************************************************************/
/*!
    Initialize the command line interface. This sets the terminal speed and
    and initializes things.
*/
/**************************************************************************/
void cmdInit(Stream *str)
{
    stream = str;
    // init the msg ptr
    msg_ptr = cmd_history[history_ptr];

    // init the command table
    cmd_tbl_list = NULL;

    for(int a = 0; a < CMD_HISTORY_LEN; a++){
       for(int b = 0; b < MAX_MSG_SIZE; b++){
         cmd_history[a][b] = 0;
       }
    }

}

/**************************************************************************/
/*!
    Add a command to the command table. The commands should be added in
    at the setup() portion of the sketch.
*/
/**************************************************************************/
void cmdAdd(char *name, void (*func)(int argc, char **argv))
{
    // alloc memory for command struct
    cmd_tbl = (cmd_t *)malloc(sizeof(cmd_t));

    // alloc memory for command name
    char *cmd_name = (char *)malloc(strlen(name)+1);

    // copy command name
    strcpy(cmd_name, name);

    // terminate the command name
    cmd_name[strlen(name)] = '\0';

    // fill out structure
    cmd_tbl->cmd = cmd_name;
    cmd_tbl->func = func;
    cmd_tbl->next = cmd_tbl_list;
    cmd_tbl_list = cmd_tbl;
}

/**************************************************************************/
/*!
    Convert a string to a number. The base must be specified, ie: "32" is a
    different value in base 10 (decimal) and base 16 (hexadecimal).
*/
/**************************************************************************/
uint32_t cmdStr2Num(char *str, uint8_t base)
{
    return strtol(str, NULL, base);
}
