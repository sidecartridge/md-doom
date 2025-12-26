/**
 * File: term.c
 * Author: Diego Parrilla Santamaría
 * Date: January 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Online terminal
 */

#include "term.h"

// Command handlers
static void cmdClear(const char *arg);
static void cmdExit(const char *arg);
static void cmdHelp(const char *arg);
static void cmdUnknown(const char *arg);

// Command table
static const Command *commands;

// Number of commands in the table
static size_t numCommands = 0;

// Setter for commands and numCommands
void term_setCommands(const Command *cmds, size_t count) {
  commands = cmds;
  numCommands = count;
}

// Interrupt handler for DMA completion
void __not_in_flash_func(term_dma_irq_handler_lookup)(void) {
  // Read the rom3 signal and if so then process the command
  dma_hw->ints1 = 1U << 2;
}

static char screen[TERM_SCREEN_SIZE];
static uint8_t cursorX = 0;
static uint8_t cursorY = 0;

// Store previous cursor position for block removal
static uint8_t prevCursorX = 0;
static uint8_t prevCursorY = 0;

// Buffer to keep track of chars entered between newlines
static char inputBuffer[TERM_INPUT_BUFFER_SIZE];
static size_t inputLength = 0;

// Getter method for inputBuffer
char *term_getInputBuffer(void) { return inputBuffer; }

// Clears entire screen buffer and resets cursor
void term_clearScreen(void) {
  memset(screen, 0, TERM_SCREEN_SIZE);
  cursorX = 0;
  cursorY = 0;
  display_termClear();
}

// Clears the input buffer
void term_clearInputBuffer(void) {
  memset(inputBuffer, 0, TERM_INPUT_BUFFER_SIZE);
  inputLength = 0;
}

// Custom scrollup function to scroll except for the last row
void termScrollupBuffer(uint16_t blankBytes) {
  // blank bytes is the number of bytes to blank out at the bottom of the
  // screen

  unsigned char *u8g2Buffer = u8g2_GetBufferPtr(display_getU8g2Ref());
  memmove(u8g2Buffer, u8g2Buffer + blankBytes,
          DISPLAY_BUFFER_SIZE - blankBytes -
              TERM_SCREEN_SIZE_X * DISPLAY_TERM_CHAR_HEIGHT);
  memset(u8g2Buffer + DISPLAY_BUFFER_SIZE - blankBytes -
             TERM_SCREEN_SIZE_X * DISPLAY_TERM_CHAR_HEIGHT,
         0, blankBytes);
}

// Scrolls the screen up by one row
static void termScrollUp(void) {
  memmove(screen, screen + TERM_SCREEN_SIZE_X,
          TERM_SCREEN_SIZE - TERM_SCREEN_SIZE_X);
  memset(screen + TERM_SCREEN_SIZE - TERM_SCREEN_SIZE_X, 0, TERM_SCREEN_SIZE_X);
  termScrollupBuffer(TERM_DISPLAY_ROW_BYTES);
}

// Prints a character to the screen, handles scrolling
static void termPutChar(char chr) {
  screen[cursorY * TERM_SCREEN_SIZE_X + cursorX] = chr;
  display_termChar(cursorX, cursorY, chr);
  cursorX++;
  if (cursorX >= TERM_SCREEN_SIZE_X) {
    cursorX = 0;
    cursorY++;
    if (cursorY >= TERM_SCREEN_SIZE_Y) {
      termScrollUp();
      cursorY = TERM_SCREEN_SIZE_Y - 1;
    }
  }
}

// Renders a single character, with special handling for newline and carriage
// return
static void termRenderChar(char chr) {
  // First, remove the old block by restoring the character
  display_termChar(prevCursorX, prevCursorY, ' ');
  if (chr == '\n' || chr == '\r') {
    // Move to new line
    cursorX = 0;
    cursorY++;
    if (cursorY >= TERM_SCREEN_SIZE_Y) {
      termScrollUp();
      cursorY = TERM_SCREEN_SIZE_Y - 1;
    }
  } else if (chr != '\0') {
    termPutChar(chr);
  }

  // Draw a block at the new cursor position
  display_termCursor(cursorX, cursorY);
  // Update previous cursor coords
  prevCursorX = cursorX;
  prevCursorY = cursorY;
}

// Prints entire screen to stdout
static void termPrintScreen(void) {
  for (int posY = 0; posY < TERM_SCREEN_SIZE_Y; posY++) {
    for (int posX = 0; posX < TERM_SCREEN_SIZE_X; posX++) {
      char chr = screen[posY * TERM_SCREEN_SIZE_X + posX];
      putchar(chr ? chr : ' ');
    }
    putchar('\n');
  }
}

/**
 * @brief Processes a complete VT52 escape sequence.
 *
 * This function interprets the VT52 sequence stored in `seq` (with given
 * length) and performs the corresponding cursor movements. Modify the TODO
 * sections to implement additional features as needed.
 *
 * @param seq Pointer to the escape sequence buffer.
 * @param length The length of the escape sequence.
 */
static void vt52ProcessSequence(const char *seq, size_t length) {
  // Ensure we have at least an ESC and a command character.
  if (length < 2) return;

  char command = seq[1];
  switch (command) {
    case 'A':  // Move cursor up
      // TODO(diego): Improve behavior if needed.
      if (cursorY > 0) {
        cursorY--;
      }
      termRenderChar('\0');
      break;
    case 'B':  // Move cursor down
      if (cursorY < TERM_SCREEN_SIZE_Y - 1) {
        cursorY++;
      }
      termRenderChar('\0');
      break;
    case 'C':  // Move cursor right
      if (cursorX < TERM_SCREEN_SIZE_X - 1) {
        cursorX++;
      }
      termRenderChar('\0');
      break;
    case 'D':  // Move cursor left
      if (cursorX > 0) {
        cursorX--;
      }
      termRenderChar('\0');
      break;
    case 'E':  // Clear screen and place cursor at top left corner
      cursorX = 0;
      cursorY = 0;
      termRenderChar('\0');
      for (int posY = 0; posY < TERM_SCREEN_SIZE_Y; posY++) {
        for (int posX = 0; posX < TERM_SCREEN_SIZE_X; posX++) {
          screen[posY * TERM_SCREEN_SIZE_X + posX] = 0;
          display_termChar(posX, posY, ' ');
        }
      }
      break;
    case 'H':  // Cursor home
      cursorX = 0;
      cursorY = 0;
      termRenderChar('\0');
      break;
    case 'J':  // Erases from the current cursor position to the end of the
               // screen
      for (int posY = cursorY; posY < TERM_SCREEN_SIZE_Y; posY++) {
        for (int posX = cursorX; posX < TERM_SCREEN_SIZE_X; posX++) {
          screen[posY * TERM_SCREEN_SIZE_X + posX] = 0;
          display_termChar(posX, posY, ' ');
        }
      }
      break;
    case 'K':  // Clear to end of line
      for (int posX = cursorX; posX < TERM_SCREEN_SIZE_X; posX++) {
        screen[cursorY * TERM_SCREEN_SIZE_X + posX] = 0;
        display_termChar(posX, cursorY, ' ');
      }
      break;
    case 'Y':  // Direct cursor addressing: ESC Y <row> <col>
      if (length == 4) {
        int row = seq[2] - TERM_POS_Y;
        int col = seq[3] - TERM_POS_X;
        if (row >= 0 && row < TERM_SCREEN_SIZE_Y && col >= 0 &&
            col < TERM_SCREEN_SIZE_X) {
          cursorY = row;
          cursorX = col;
        }
        termRenderChar('\0');
      }
      break;
    default:
      // Unrecognized sequence. Optionally, print or ignore.
      // For now, we'll ignore it.
      break;
  }
}

void term_printString(const char *str) {
  enum { STATE_NORMAL, STATE_ESC } state = STATE_NORMAL;
  char escBuffer[TERM_ESC_BUFFLINE_SIZE];
  size_t escLen = 0;

  while (*str) {
    char chr = *str;
    if (state == STATE_NORMAL) {
      if (chr == TERM_ESC_CHAR) {  // ESC character detected
        state = STATE_ESC;
        escLen = 0;
        escBuffer[escLen++] = chr;
      } else {
        termRenderChar(chr);
      }
    } else {  // STATE_ESC: we're accumulating an escape sequence
      escBuffer[escLen++] = chr;
      // Check for sequence completion:
      // Most VT52 sequences are two characters (ESC + command)...
      if (escLen == 2) {
        if (escBuffer[1] == 'Y') {
          // ESC Y requires two more characters (for row and col)
          // Do nothing now—wait until esc_len == 4.
        } else {
          // Sequence complete (ESC + single command)
          vt52ProcessSequence(escBuffer, escLen);
          state = STATE_NORMAL;
        }
      } else if (escBuffer[1] == 'Y' && escLen == 4) {
        // ESC Y <row> <col> sequence complete
        vt52ProcessSequence(escBuffer, escLen);
        state = STATE_NORMAL;
      }
      // In case the buffer gets too long, flush it as normal text
      if (escLen >= sizeof(escBuffer)) {
        for (size_t i = 0; i < escLen; i++) {
          termRenderChar(escBuffer[i]);
        }
        state = STATE_NORMAL;
      }
    }
    str++;
  }
  // If the string ends while still in ESC state, flush the buffered
  // characters as normal text.
  if (state == STATE_ESC) {
    for (size_t i = 0; i < escLen; i++) {
      termRenderChar(escBuffer[i]);
    }
  }
  display_termRefresh();
}

// Called whenever a character is entered by the user
// This is the single point of entry for user input
static void termInputChar(char chr) {
  // Check for backspace
  if (chr == '\b') {
    display_termChar(prevCursorX, prevCursorY, ' ');

    // If we have chars in input_buffer, remove last char
    if (inputLength > 0) {
      inputLength--;
      inputBuffer[inputLength] = '\0';  // Null-terminate the string
      // Also reflect on screen
      // same as term_backspace
      if (cursorX == 0) {
        if (cursorY > 0) {
          cursorY--;
          cursorX = TERM_SCREEN_SIZE_X - 1;
        } else {
          // already top-left corner
          return;
        }
      } else {
        cursorX--;
      }
      screen[cursorY * TERM_SCREEN_SIZE_X + cursorX] = 0;
      display_termChar(cursorX, cursorY, ' ');
    }

    display_termCursor(cursorX, cursorY);
    prevCursorX = cursorX;
    prevCursorY = cursorY;
    display_termRefresh();
    return;
  }

  // If it's newline or carriage return, finalize the line
  if (chr == '\n' || chr == '\r') {
    // Render newline on screen
    termRenderChar('\n');

    // Process input_buffer
    // Split the input into command and argument
    char command[TERM_INPUT_BUFFER_SIZE] = {0};
    char arg[TERM_INPUT_BUFFER_SIZE] = {0};
    sscanf(inputBuffer, "%63s %63[^\n]", command,
           arg);  // Split at the first space

    bool commandFound = false;
    for (size_t i = 0; i < numCommands; i++) {
      if (strcmp(command, commands[i].command) == 0) {
        commands[i].handler(arg);  // Pass the argument to the handler
        commandFound = true;
      }
    }
    if ((!commandFound) && (strlen(command) > 0)) {
      // The custom unknown command manager is called when the command is empty
      // in the command table. This is useful to manage custom entries.
      for (size_t i = 0; i < numCommands; i++) {
        if (strlen(commands[i].command) == 0) {
          commands[i].handler(inputBuffer);  // Pass the argument to the handler
        }
      }
    }

    // Reset input buffer
    memset(inputBuffer, 0, TERM_INPUT_BUFFER_SIZE);
    inputLength = 0;

    term_printString("> ");
    display_termRefresh();
    return;
  }

  // If it's a normal character
  // Add it to input_buffer if there's space
  if (inputLength < TERM_INPUT_BUFFER_SIZE - 1) {
    inputBuffer[inputLength++] = chr;
    // Render char on screen
    termRenderChar(chr);

    // show block cursor

    display_termRefresh();
  } else {
    // Buffer full, ignore or beep?
  }
}

// For convenience, we can also have a helper function that "types" a string
// as if typed by user
static void termTypeString(const char *str) {
  while (*str) {
    termInputChar(*str);
    str++;
  }
}

void term_init(void) {
  // Initialize the random seed
  srand(time(NULL));

  // Initialize the welcome messages
  term_clearScreen();
  display_refresh();
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(term_loop)() {
  return;
}

// Command handlers
void term_cmdSettings(const char *arg) {
  term_printString(
      "\x1B"
      "E"
      "Available settings commands:\n");
  term_printString("  print   - Show settings\n");
  term_printString("  save    - Save settings\n");
  term_printString("  erase   - Erase settings\n");
  term_printString("  get     - Get setting (requires key)\n");
  term_printString("  put_int - Set integer (key and value)\n");
  term_printString("  put_bool- Set boolean (key and value)\n");
  term_printString("  put_str - Set string (key and value)\n");
  term_printString("\n");
}

void term_cmdPrint(const char *arg) {
  char *buffer = (char *)malloc(TERM_PRINT_SETTINGS_BUFFER_SIZE);
  if (buffer == NULL) {
    term_printString("Error: Out of memory.\n");
    return;
  }
  settings_print(aconfig_getContext(), buffer);
  term_printString(buffer);
  free(buffer);
}

void term_cmdClear(const char *arg) { term_clearScreen(); }

void term_cmdExit(const char *arg) {
  term_printString("Exiting terminal...\n");
  // Send continue to desktop command
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_CONTINUE);
}

void term_cmdUnknown(const char *arg) {
  TPRINTF("Unknown command. Type 'help' for a list of commands.\n");
}

void term_cmdSave(const char *arg) {
  settings_save(aconfig_getContext(), true);
  term_printString("Settings saved.\n");
}

void term_cmdErase(const char *arg) {
  settings_erase(aconfig_getContext());
  term_printString("Settings erased.\n");
}

void term_cmdGet(const char *arg) {
  if (arg && strlen(arg) > 0) {
    SettingsConfigEntry *entry =
        settings_find_entry(aconfig_getContext(), &arg[0]);
    if (entry != NULL) {
      TPRINTF("Key: %s\n", entry->key);
      TPRINTF("Type: ");
      switch (entry->dataType) {
        case SETTINGS_TYPE_INT:
          TPRINTF("INT");
          break;
        case SETTINGS_TYPE_STRING:
          TPRINTF("STRING");
          break;
        case SETTINGS_TYPE_BOOL:
          TPRINTF("BOOL");
          break;
        default:
          TPRINTF("UNKNOWN");
          break;
      }
      TPRINTF("\n");
      TPRINTF("Value: %s\n", entry->value);
    } else {
      TPRINTF("Key not found.\n");
    }
  } else {
    TPRINTF("No key provided for 'get' command.\n");
  }
}

void term_cmdPutInt(const char *arg) {
  char key[SETTINGS_MAX_KEY_LENGTH] = {0};
  int value = 0;
  if (sscanf(arg, "%s %d", key, &value) == 2) {
    int err = settings_put_integer(aconfig_getContext(), key, value);
    if (err == 0) {
      TPRINTF("Key: %s\n", key);
      TPRINTF("Value: %d\n", value);
    }
  } else {
    TPRINTF("Invalid arguments for 'put_int' command.\n");
  }
}

void term_cmdPutBool(const char *arg) {
  char key[SETTINGS_MAX_KEY_LENGTH] = {0};

  char valueStr[TERM_BOOL_INPUT_BUFF] = {
      0};  // Small buffer to store the value string (e.g., "true", "false")
  bool value;

  // Scan the key and the value string
  if (sscanf(arg, "%s %7s", key, valueStr) == 2) {
    // Convert the value_str to lowercase for easier comparison
    for (int i = 0; valueStr[i]; i++) {
      valueStr[i] = tolower(valueStr[i]);
    }

    // Check if the value is true or false
    if (strcmp(valueStr, "true") == 0 || strcmp(valueStr, "t") == 0 ||
        strcmp(valueStr, "1") == 0) {
      value = true;
    } else if (strcmp(valueStr, "false") == 0 || strcmp(valueStr, "f") == 0 ||
               strcmp(valueStr, "0") == 0) {
      value = false;
    } else {
      TPRINTF(
          "Invalid boolean value. Use 'true', 'false', 't', 'f', '1', or "
          "'0'.\n");
      return;
    }

    // Store the boolean value
    int err = settings_put_bool(aconfig_getContext(), key, value);
    if (err == 0) {
      TPRINTF("Key: %s\n", key);
      TPRINTF("Value: %s\n", value ? "true" : "false");
    }
  } else {
    TPRINTF(
        "Invalid arguments for 'put_bool' command. Usage: put_bool <key> "
        "<true/false>\n");
  }
}

void term_cmdPutString(const char *arg) {
  char key[SETTINGS_MAX_KEY_LENGTH] = {0};

  // Scan the first word (key)
  if (sscanf(arg, "%s", key) == 1) {
    // Find the position after the key in the argument string
    const char *value = strchr(arg, ' ');
    if (value) {
      // Move past the space to the start of the value
      value++;

      // Check if the value is non-empty
      if (strlen(value) > 0) {
        int err = settings_put_string(aconfig_getContext(), key, value);
        if (err == 0) {
          TPRINTF("Key: %s\n", key);
          TPRINTF("Value: %s\n", value);
        }
      } else {
        int err = settings_put_string(aconfig_getContext(), key, value);
        if (err == 0) {
          TPRINTF("Key: %s\n", key);
          TPRINTF("Value: <EMPTY>\n");
        }
      }
    } else {
      int err = settings_put_string(aconfig_getContext(), key, value);
      if (err == 0) {
        TPRINTF("Key: %s\n", key);
        TPRINTF("Value: <EMPTY>\n");
      }
    }
  } else {
    TPRINTF("Invalid arguments for 'put_string' command.\n");
  }
}
