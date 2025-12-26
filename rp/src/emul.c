/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Template code for the core emulation
 */

#include "emul.h"

// inclusw in the C file to avoid multiple definitions
#include "target_firmware.h"  // Include the target firmware binary

// Command handlers
static void cmdMenu(const char *arg);
static void cmdClear(const char *arg);
static void cmdExit(const char *arg);
static void cmdHelp(const char *arg);
static void cmdBooster(const char *arg);

// Command table
static const Command commands[] = {
    {"m", cmdMenu},
    {"h", cmdHelp},
    {"e", cmdExit},
    {"x", cmdBooster},
    {"?", cmdHelp},
    {"s", term_cmdSettings},
    {"settings", term_cmdSettings},
    {"print", term_cmdPrint},
    {"save", term_cmdSave},
    {"erase", term_cmdErase},
    {"get", term_cmdGet},
    {"put_int", term_cmdPutInt},
    {"put_bool", term_cmdPutBool},
    {"put_str", term_cmdPutString},
};

// Number of commands in the table
static const size_t numCommands = sizeof(commands) / sizeof(commands[0]);

// Keep active loop or exit
static bool keepActive = true;

// Should we reset the device, or jump to the booster app?
// By default, we reset the device.
static bool resetDeviceAtBoot = true;

static void showTitle() {
  term_printString(
      "\x1B"
      "E"
      "Microfirmware test app - " RELEASE_VERSION "\n");
}

static void menu(void) {
  showTitle();
  term_printString("\n");
}

// Command handlers
void cmdMenu(const char *arg) { menu(); }

void cmdHelp(const char *arg) {
  term_printString("No commands available.\n");
}

void cmdClear(const char *arg) { term_clearScreen(); }

void cmdExit(const char *arg) {
  term_printString("Exiting terminal...\n");
  // Send continue to desktop command
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_CONTINUE);
}

void cmdBooster(const char *arg) {
  term_printString("Launching Booster app...\n");
  term_printString("The computer will boot shortly...\n\n");
  term_printString("If it doesn't boot, power it on and off.\n");
  resetDeviceAtBoot = false;  // Jump to the booster app
  keepActive = false;         // Exit the active loop
}

// This section contains the functions that are called from the main loop

static bool getKeepActive() { return keepActive; }

static bool getResetDevice() { return resetDeviceAtBoot; }

static void preinit() {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString("Press SHIFT to boot to desktop.\n");

  display_refresh();
}

void failure(const char *message) {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString(message);

  display_refresh();
}

static void init(const char *folder) {
  // Set the command table
  term_setCommands(commands, numCommands);

  // Clear the screen
  term_clearScreen();

  // Display the menu
  menu();

  // Example 1: Move the cursor up one line.
  // VT52 sequence: ESC A (moves cursor up)
  // The escape sequence "\x1BA" will move the cursor up one line.
  // term_printString("\x1B" "A");
  // After moving up, print text that overwrites part of the previous line.
  // term_printString("Line 2 (modified by ESC A)\n");

  // Example 2: Move the cursor right one character.
  // VT52 sequence: ESC C (moves cursor right)
  // term_printString("\x1B" "C");
  // term_printString(" <-- Moved right with ESC C\n");

  // Example 3: Direct cursor addressing.
  // VT52 direct addressing uses ESC Y <row> <col>, where:
  //   row_char = row + 0x20, col_char = col + 0x20.
  // For instance, to move the cursor to row 0, column 10:
  //   row: 0 -> 0x20 (' ')
  //   col: 10 -> 0x20 + 10 = 0x2A ('*')
  // term_printString("\x1B" "Y" "\x20" "\x2A");
  // term_printString("Text at row 0, column 10 via ESC Y\n");

  // term_printString("\x1B" "Y" "\x2A" "\x20");

  display_refresh();
}

void emul_start() {
  // The anatomy of an app or microfirmware is as follows:
  // - The driver code running in the remote device (the computer)
  // - the driver code running in the host device (the rp2040/rp2350)
  //
  // The driver code running in the remote device is responsible for:
  // 1. Perform the emulation of the device (ex: a ROM cartridge)
  // 2. Handle the communication with the host device
  // 3. Handle the configuration of the driver (ex: the ROM file to load)
  // 4. Handle the communication with the user (ex: the terminal)
  //
  // The driver code running in the host device is responsible for:
  // 1. Handle the communication with the remote device
  // 2. Handle the configuration of the driver (ex: the ROM file to load)
  // 3. Handle the communication with the user (ex: the terminal)
  //
  // Hence, we effectively have two drivers running in two different devices
  // with different architectures and capabilities.
  //
  //

  // 1. Check if the host device must be initialized to perform the emulation
  //    of the device, or start in setup/configuration mode
  SettingsConfigEntry *appMode =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_MODE);
  int appModeValue = APP_MODE_SETUP;  // Setup menu
  if (appMode == NULL) {
    DPRINTF(
        "APP_MODE_SETUP not found in the configuration. Using default value\n");
  } else {
    appModeValue = atoi(appMode->value);
    DPRINTF("Start emulation in mode: %i\n", appModeValue);
  }

  // 2. Initialiaze the normal operation of the app, unless the configuration
  // option says to start the config app Or a SELECT button is (or was) pressed
  // to start the configuration section of the app

  // In this example, the flow will always start the configuration app first
  // The ROM Emulator app for example will check here if the start directly
  // in emulation mode is needed or not

  // 3. If we are here, it means the app is not in emulation mode, but in
  // setup/configuration mode

  // As a rule of thumb, the remote device (the computer) driver code must
  // be copied to the RAM of the host device where the emulation will take
  // place.
  // The code is stored as an array in the target_firmware.h file
  //
  // Copy the terminal firmware to RAM
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length * 2);

  // Initialize the terminal emulator PIO programs
  // The communication between the remote (target) computer and the RP2040 is
  // done using a command protocol over the cartridge bus
  // term_dma_irq_handler_lookup is the implementation of the terminal emulator
  // using the command protocol.
  // Hence, if you want to implement your own app or microfirmware, you should
  // implement your own command handler using this protocol.
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // After this point, the remote computer can execute the code

  // 4. During the setup/configuration mode, the driver code must interact
  // with the user to configure the device. To simplify the process, the
  // terminal emulator is used to interact with the user.
  // The terminal emulator is a simple text-based interface that allows the
  // user to configure the device using text commands.
  // If you want to use a custom app in the remote computer, you can do it.
  // But it's easier to debug and code in the rp2040

  // Initialize the display
  display_setupU8g2();

  // 5. Init the sd card
  // Most of the apps or microfirmwares will need to read and write files
  // to the SD card. The SD card is used to store the ROM, floppies, even
  // full hard disk files, configuration files, and other data.
  // The SD card is initialized here. If the SD card is not present, the
  // app will show an error message and wait for the user to insert the SD card.
  // The app will not start until the SD card is inserted correctly.
  // Each app or microfirmware must have a folder in the SD card where the
  // files are stored. The folder name is defined in the configuration.
  // If there is no folder in the micro SD card, the app will create it.

  FATFS fsys;
  SettingsConfigEntry *folder =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_FOLDER);
  char *folderName = "/test";  // MODIFY THIS TO YOUR FOLDER NAME
  if (folder == NULL) {
    DPRINTF("FOLDER not found in the configuration. Using default value\n");
  } else {
    DPRINTF("FOLDER: %s\n", folder->value);
    folderName = folder->value;
  }
  int sdcardErr = sdcard_initFilesystem(&fsys, folderName);
  if (sdcardErr != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card: %i\n", sdcardErr);
    failure(
        "SD card error.\nCheck the card is inserted correctly.\nInsert card "
        "and restart the computer.");
    while (1) {
      // Wait forever
      term_loop();
    }
  } else {
    DPRINTF("SD card found & initialized\n");
  }

  // Initialize the display again (in case the terminal emulator changed it)
  display_setupU8g2();

  // Pre-init the stuff
  // In this example it only prints the initial message, but can be used as
  // a place to put other code that needs to be run before the app starts.
  preinit();

  // 6. Now complete the terminal emulator initialization
  // The terminal emulator is used to interact with the user to configure the
  // device.
  init(folderName);

  // 7. Configure the SELECT button
  // Short press: reset the device and restart the app
  // Long press: reset the device and erase the flash.
  select_configure();
  select_coreWaitPush(
      reset_device,
      reset_deviceAndEraseFlash);  // Wait for the SELECT button to be pushed

  // 8. Start the main loop
  // The main loop is the core of the app. It is responsible for running the
  // app, handling the user input, and performing the tasks of the app.
  // The main loop runs until the user decides to exit.
  // For testing purposes, this app only shows commands to manage the settings
  DPRINTF("Start the app loop here\n");

  while (getKeepActive()) {
    sleep_ms(SLEEP_LOOP_MS);
    // Check remote commands
    term_loop();
  }

  // 9. Send RESET computer command
  // Ok, so we are done with the setup but we want to reset the computer to
  // reboot in the same microfirmware app or start the booster app

  select_coreWaitPushDisable();  // Disable the SELECT button
  sleep_ms(SLEEP_LOOP_MS);
  // We must reset the computer
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
  sleep_ms(SLEEP_LOOP_MS);
  if (getResetDevice()) {
    // Reset the device
    reset_device();
  } else {
    // Before jumping to the booster app, let's clean the settings
    // Set emulation mode to 255 (setup menu)
    settings_put_integer(aconfig_getContext(), ACONFIG_PARAM_MODE,
                         APP_MODE_SETUP);
    settings_save(aconfig_getContext(), true);

    // Jump to the booster app
    DPRINTF("Jumping to the booster app...\n");
    reset_jump_to_booster();
  }
}
