/* battleshipGui.c */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>

#define GRID_SIZE 8          // Size of the game grid (8x8)
#define GAME_CONTINUE 0      // Game is ongoing
#define GAME_OVER 1          // Game has ended
#define PARENT_TURN 0        // Parent's turn
#define CHILD_TURN 1         // Child's turn

#define SAVE_FILE "gamestate.bin"  // File to save the game state
#define MOVE_INTERVAL 250         // Interval between moves in milliseconds
#define SHM_KEY 1234                // Shared memory key for IPC

// Structure to define ship types
typedef struct {
    int length;         // Length of the ship
    char name[20];      // Name of the ship
} Ship;

// Define ships and their lengths
Ship ships[] = {
    {4, "Battleship"},
    {3, "Cruiser"},
    {3, "Cruiser"},
    {2, "Destroyer"},
    {2, "Destroyer"}
};

int shipCount = sizeof(ships) / sizeof(ships[0]); // Total number of ships

// Structure to represent the game state
typedef struct {
    int parentGrid[GRID_SIZE][GRID_SIZE];          // Parent's game grid
    int childGrid[GRID_SIZE][GRID_SIZE];           // Child's game grid
    int parentAttackedCells[GRID_SIZE][GRID_SIZE]; // Cells attacked by parent
    int childAttackedCells[GRID_SIZE][GRID_SIZE];  // Cells attacked by child
    int gameStatus[2];  // [0]: GAME_CONTINUE or GAME_OVER, [1]: PARENT_TURN or CHILD_TURN
} GameState;

// Global variables
GameState *gameState;                      // Game state pointer in shared memory
GtkWidget *playerGridWidget;               // Player's grid widget
GtkWidget *opponentGridWidget;             // Opponent's grid widget
GtkWidget *statusLabel;                    // Label to display game status
GtkWidget *turnLabel;                      // Label to display current turn
GtkWidget *movesTextView;                  // TextView to display moves history
GtkTextBuffer *movesBuffer;                // TextBuffer for the moves TextView
gboolean shipsPlaced = FALSE;              // Flag to check if ships have been placed
gboolean gameStarted = FALSE;              // Flag to check if the game has started

GtkWidget *playerButtons[GRID_SIZE][GRID_SIZE];    // Array to store player's buttons
GtkWidget *opponentButtons[GRID_SIZE][GRID_SIZE];  // Array to store opponent's buttons

// Function prototypes
void initializeGrid(int grid[GRID_SIZE][GRID_SIZE]);
int isValidPlacement(int grid[GRID_SIZE][GRID_SIZE], int x, int y, int length, int horizontal);
void placeShip(int grid[GRID_SIZE][GRID_SIZE], int length);
void placeAllShips(int grid[GRID_SIZE][GRID_SIZE]);
int isValidAttack(int attackedCells[GRID_SIZE][GRID_SIZE], int x, int y);
int childAttack(GameState *gameState, int *hitX, int *hitY);
int parentAttack(GameState *gameState, int *hitX, int *hitY);
int checkGameOver(int grid[GRID_SIZE][GRID_SIZE]);
void startGame(GameState *gameState);
void saveGameState(GameState *gameState);
int loadGameState(GameState *gameState);
void onStartGame(GtkWidget *widget, gpointer data);
void onPlaceShips(GtkWidget *widget, gpointer data);
void onSaveGame(GtkWidget *widget, gpointer data);
void onLoadGame(GtkWidget *widget, gpointer data);
void refreshGrid(int grid[GRID_SIZE][GRID_SIZE], gboolean isPlayer, GtkWidget *buttons[GRID_SIZE][GRID_SIZE]);
gboolean playGame(gpointer data);
void displayMessage(const char *message);
GtkWidget* createGameGrid(int grid[GRID_SIZE][GRID_SIZE], gboolean isPlayer, GtkWidget *buttons[GRID_SIZE][GRID_SIZE]);

/* Function Implementations */

// Initializes the game grid by setting all cells to 0 (empty)
void initializeGrid(int grid[GRID_SIZE][GRID_SIZE]) {
    memset(grid, 0, sizeof(int) * GRID_SIZE * GRID_SIZE);
}

// Checks if placing a ship at the specified position is valid
int isValidPlacement(int grid[GRID_SIZE][GRID_SIZE], int x, int y, int length, int horizontal) {
    for (int i = 0; i < length; i++) {
        int placeX = x + (horizontal ? i : 0);
        int placeY = y + (horizontal ? 0 : i);

        // Invalid if the ship goes outside the grid
        if (placeX < 0 || placeX >= GRID_SIZE || placeY < 0 || placeY >= GRID_SIZE) {
            return 0;
        }

        // Invalid if the cell is already occupied
        if (grid[placeY][placeX] != 0) {
            return 0;
        }

        // Check surrounding cells to prevent ships from touching each other
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int checkX = placeX + dx;
                int checkY = placeY + dy;

                // Check boundaries
                if (checkX >= 0 && checkX < GRID_SIZE && checkY >= 0 && checkY < GRID_SIZE) {
                    // Invalid if adjacent cell contains a ship (except the current cell)
                    if (grid[checkY][checkX] != 0 && !(checkX == placeX && checkY == placeY)) {
                        return 0;
                    }
                }
            }
        }
    }
    return 1; // Valid placement
}

// Places a ship randomly on the grid
void placeShip(int grid[GRID_SIZE][GRID_SIZE], int length) {
    int placed = 0;
    int attempts = 0;
    while (!placed && attempts < 1000) {
        int x = rand() % GRID_SIZE;
        int y = rand() % GRID_SIZE;
        int horizontal = rand() % 2;

        // If the placement is valid, place the ship
        if (isValidPlacement(grid, x, y, length, horizontal)) {
            for (int i = 0; i < length; i++) {
                int placeX = x + (horizontal ? i : 0);
                int placeY = y + (horizontal ? 0 : i);
                grid[placeY][placeX] = 1;  // 1 represents a ship
            }
            placed = 1;
        }
        attempts++;
    }
    if (!placed) {
        printf("Failed to place the ship.\n");
        exit(1);
    }
}

// Places all ships on the grid
void placeAllShips(int grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < shipCount; i++) {
        placeShip(grid, ships[i].length);
    }
}

// Checks if an attack at the specified position is valid
int isValidAttack(int attackedCells[GRID_SIZE][GRID_SIZE], int x, int y) {
    if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE && attackedCells[y][x] == 0) {
        return 1; // Valid attack
    } else {
        return 0; // Invalid attack
    }
}

// Parent's attack function
int parentAttack(GameState *gameState, int *hitX, int *hitY) {
    static int lastHitX = -1, lastHitY = -1; // Coordinates of the last hit
    int x, y;

    // If the last attack was a hit, try attacking adjacent cells
    if (lastHitX != -1 && lastHitY != -1) {
        int directions[4][2] = {
            {-1, 0}, // Left
            {1, 0},  // Right
            {0, -1}, // Up
            {0, 1}   // Down
        };
        for (int i = 0; i < 4; i++) {
            x = lastHitX + directions[i][0];
            y = lastHitY + directions[i][1];
            if (isValidAttack(gameState->parentAttackedCells, x, y)) {
                goto attack;
            }
        }
        // Reset if no valid adjacent cells
        lastHitX = -1;
        lastHitY = -1;
    }

    // Random attack
    do {
        x = rand() % GRID_SIZE;
        y = rand() % GRID_SIZE;
    } while (!isValidAttack(gameState->parentAttackedCells, x, y));

attack:
    *hitX = x;
    *hitY = y;
    gameState->parentAttackedCells[y][x] = 1; // Mark the cell as attacked

    if (gameState->childGrid[y][x] == 1) {
        gameState->childGrid[y][x] = 2; // Mark as hit
        lastHitX = x;
        lastHitY = y;
        printf("Parent hit at (%d, %d)\n", x, y);
        return 1; // Hit
    } else {
        gameState->childGrid[y][x] = -1; // Mark as miss
        printf("Parent missed at (%d, %d)\n", x, y);
        return 0; // Miss
    }
}

// Child's attack function
int childAttack(GameState *gameState, int *hitX, int *hitY) {
    static int lastHitX = -1, lastHitY = -1; // Coordinates of the last hit
    int x, y;

    // If the last attack was a hit, try attacking adjacent cells
    if (lastHitX != -1 && lastHitY != -1) {
        int directions[4][2] = {
            {-1, 0}, // Left
            {1, 0},  // Right
            {0, -1}, // Up
            {0, 1}   // Down
        };
        for (int i = 0; i < 4; i++) {
            x = lastHitX + directions[i][0];
            y = lastHitY + directions[i][1];
            if (isValidAttack(gameState->childAttackedCells, x, y)) {
                goto attack;
            }
        }
        // Reset if no valid adjacent cells
        lastHitX = -1;
        lastHitY = -1;
    }

    // Random attack
    do {
        x = rand() % GRID_SIZE;
        y = rand() % GRID_SIZE;
    } while (!isValidAttack(gameState->childAttackedCells, x, y));

attack:
    *hitX = x;
    *hitY = y;
    gameState->childAttackedCells[y][x] = 1; // Mark the cell as attacked

    if (gameState->parentGrid[y][x] == 1) {
        gameState->parentGrid[y][x] = 2;  // Mark as hit
        lastHitX = x;
        lastHitY = y;
        printf("Child hit at (%d, %d)\n", x, y);
        return 1; // Hit
    } else {
        gameState->parentGrid[y][x] = -1;  // Mark as miss
        printf("Child missed at (%d, %d)\n", x, y);
        return 0; // Miss
    }
}

// Displays a message in the status label
void displayMessage(const char *message) {
    gtk_label_set_text(GTK_LABEL(statusLabel), message);
}

// Refreshes the grid display
void refreshGrid(int grid[GRID_SIZE][GRID_SIZE], gboolean isPlayer, GtkWidget *buttons[GRID_SIZE][GRID_SIZE]) {
    GtkWidget *button;
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            button = buttons[y][x];

            // Remove previous CSS classes
            GtkStyleContext *context = gtk_widget_get_style_context(button);
            gtk_style_context_remove_class(context, "ship-cell");
            gtk_style_context_remove_class(context, "hit-cell");

            // Reset button content
            gtk_button_set_label(GTK_BUTTON(button), NULL);

            // Add CSS classes based on cell status
            if (grid[y][x] == 2) { // Hit
                gtk_style_context_add_class(context, "hit-cell");
                gtk_button_set_label(GTK_BUTTON(button), "X");
            } else if (grid[y][x] == -1) { // Miss
                gtk_button_set_label(GTK_BUTTON(button), "O");
            } else if (grid[y][x] == 1 && isPlayer) { // Ship
                gtk_style_context_add_class(context, "ship-cell");
            } else {
                // Empty water cell
                gtk_button_set_label(GTK_BUTTON(button), "~");
            }
        }
    }
}

// Creates a game grid
GtkWidget* createGameGrid(int grid[GRID_SIZE][GRID_SIZE], gboolean isPlayer, GtkWidget *buttons[GRID_SIZE][GRID_SIZE]) {
    GtkWidget *gridWidget = gtk_grid_new();
    GtkWidget *button;

    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            // Create a button for each cell
            button = gtk_button_new();
            // Set button size
            gtk_widget_set_size_request(button, 40, 40);

            // Remove button border
            gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

            // Disable button clicks
            gtk_widget_set_sensitive(button, FALSE);

            // Store the button in the array
            buttons[y][x] = button;

            // Attach button to the grid
            gtk_grid_attach(GTK_GRID(gridWidget), button, x, y, 1, 1);
        }
    }

    return gridWidget;
}

// Checks if the game is over (no ships left)
int checkGameOver(int grid[GRID_SIZE][GRID_SIZE]) {
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            if (grid[y][x] == 1) {
                return 0; // Game is not over
            }
        }
    }
    return 1; // Game is over
}

// Callback for "Start Game" menu item
void onStartGame(GtkWidget *widget, gpointer data) {
    startGame(gameState);
}

// Callback for "Place Ships" menu item
void onPlaceShips(GtkWidget *widget, gpointer data) {
    initializeGrid(gameState->parentGrid);
    initializeGrid(gameState->childGrid);
    initializeGrid(gameState->parentAttackedCells);
    initializeGrid(gameState->childAttackedCells);
    placeAllShips(gameState->parentGrid);
    placeAllShips(gameState->childGrid);
    shipsPlaced = TRUE;
    gameStarted = FALSE;
    gameState->gameStatus[0] = GAME_CONTINUE;
    gameState->gameStatus[1] = PARENT_TURN;
    refreshGrid(gameState->parentGrid, TRUE, playerButtons);
    refreshGrid(gameState->childGrid, TRUE, opponentButtons); // Now shows child's ships
    displayMessage("Ships have been placed.");
    // Reset turn label
    gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: None");
    // Clear moves history
    gtk_text_buffer_set_text(movesBuffer, "", -1);
}

// Callback for "Save Game" menu item
void onSaveGame(GtkWidget *widget, gpointer data) {
    if (shipsPlaced) {
        saveGameState(gameState);
        gtk_main_quit(); // Exit the game after saving
    } else {
        displayMessage("No game to save.");
    }
}

// Saves the game state to a file
void saveGameState(GameState *gameState) {
    FILE *fp = fopen(SAVE_FILE, "wb");
    if (fp == NULL) {
        perror("Failed to save game state");
        return;
    }
    fwrite(gameState, sizeof(GameState), 1, fp);
    fclose(fp);
    displayMessage("Game state saved.");
}

// Loads the game state from a file
int loadGameState(GameState *gameState) {
    FILE *fp = fopen(SAVE_FILE, "rb");
    if (fp == NULL) {
        // No file to load, start a new game
        return 0;
    }
    fread(gameState, sizeof(GameState), 1, fp);
    fclose(fp);
    displayMessage("Game state loaded.");
    return 1;
}

// Callback for "Load Game" menu item
void onLoadGame(GtkWidget *widget, gpointer data) {
    if (loadGameState(gameState)) {
        shipsPlaced = TRUE;
        gameStarted = FALSE;
        refreshGrid(gameState->parentGrid, TRUE, playerButtons);
        refreshGrid(gameState->childGrid, TRUE, opponentButtons); // Show opponent's ships
        displayMessage("Game state loaded.");
        // Update turn label based on loaded game state
        if (gameState->gameStatus[0] == GAME_OVER) {
            gtk_label_set_text(GTK_LABEL(turnLabel), "Game Over");
        } else if (gameState->gameStatus[1] == PARENT_TURN) {
            gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Parent");
        } else {
            gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Child");
        }
    } else {
        displayMessage("No game to load or the game file is corrupted.");
    }
}

// Function to start the game
void startGame(GameState *gameState) {
    if (!shipsPlaced) {
        displayMessage("You need to place ships first.");
        return;
    }
    if (gameStarted) {
        displayMessage("Game is already in progress.");
        return;
    }
    gameStarted = TRUE;
    displayMessage("Game started.");

    // Continue the game if it was previously in progress
    if (gameState->gameStatus[0] == GAME_CONTINUE) {
        // Update turn label based on whose turn it is
        if (gameState->gameStatus[1] == PARENT_TURN) {
            gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Parent");
        } else {
            gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Child");
        }
    } else {
        // If the game was over, reset the game status
        gameState->gameStatus[0] = GAME_CONTINUE;
        gameState->gameStatus[1] = PARENT_TURN;
        gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Parent");
    }

    // Start the game loop
    g_timeout_add(MOVE_INTERVAL, playGame, NULL);
}

// Function called periodically to play the game
gboolean playGame(gpointer data) {
    int hitX, hitY;

    if (gameState->gameStatus[0] == GAME_OVER) {
        return FALSE; // Stop the timer
    }

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(movesBuffer, &iter);

    if (gameState->gameStatus[1] == PARENT_TURN) {
        // Update turn label
        gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Parent");
        // Parent's turn
        int result = parentAttack(gameState, &hitX, &hitY);
        refreshGrid(gameState->childGrid, TRUE, opponentButtons); // Show opponent's ships

        char moveMessage[256];
        if (result == 1) {
            sprintf(moveMessage, "Parent hit at (%d, %d)\n", hitX, hitY);
            displayMessage(moveMessage);
            if (checkGameOver(gameState->childGrid)) {
                displayMessage("Parent wins the game!");
                gameState->gameStatus[0] = GAME_OVER;
                gtk_label_set_text(GTK_LABEL(turnLabel), "Game Over");
                return FALSE;
            }
        } else {
            sprintf(moveMessage, "Parent missed at (%d, %d)\n", hitX, hitY);
            displayMessage(moveMessage);
        }
        // Append move to moves history
        gtk_text_buffer_insert(movesBuffer, &iter, moveMessage, -1);

        gameState->gameStatus[1] = CHILD_TURN;
    } else if (gameState->gameStatus[1] == CHILD_TURN) {
        // Update turn label
        gtk_label_set_text(GTK_LABEL(turnLabel), "Current Turn: Child");
        // Child's turn
        int result = childAttack(gameState, &hitX, &hitY);
        refreshGrid(gameState->parentGrid, TRUE, playerButtons);

        char moveMessage[256];
        if (result == 1) {
            sprintf(moveMessage, "Child hit at (%d, %d)\n", hitX, hitY);
            displayMessage(moveMessage);
            if (checkGameOver(gameState->parentGrid)) {
                displayMessage("Child wins the game!");
                gameState->gameStatus[0] = GAME_OVER;
                gtk_label_set_text(GTK_LABEL(turnLabel), "Game Over");
                return FALSE;
            }
        } else {
            sprintf(moveMessage, "Child missed at (%d, %d)\n", hitX, hitY);
            displayMessage(moveMessage);
        }
        // Append move to moves history
        gtk_text_buffer_insert(movesBuffer, &iter, moveMessage, -1);

        gameState->gameStatus[1] = PARENT_TURN;
    }

    return TRUE; // Continue the timer
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *mainGrid;
    GtkWidget *playerFrame, *opponentFrame;
    GtkWidget *menuBar, *gameMenu, *gameItem;
    GtkWidget *startGameItem, *placeShipsItem, *saveGameItem, *loadGameItem, *exitItem;
    GtkWidget *statusFrame;
    GtkWidget *turnFrame; // Added frame for turn label
    GtkWidget *movesFrame; // Added frame for moves history
    GtkWidget *movesScrolledWindow; // Added scrolled window for moves history
    GtkCssProvider *cssProvider;

    gtk_init(&argc, &argv);

    // Shared Memory Allocation
    int shmid = shmget(SHM_KEY, sizeof(GameState), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        exit(1);
    }
    gameState = (GameState *)shmat(shmid, NULL, 0);
    if (gameState == (GameState *)-1) {
        perror("shmat failed");
        exit(1);
    }

    // Initialize game state
    srand(time(NULL));
    initializeGrid(gameState->parentGrid);
    initializeGrid(gameState->childGrid);
    initializeGrid(gameState->parentAttackedCells);
    initializeGrid(gameState->childAttackedCells);
    gameState->gameStatus[0] = GAME_CONTINUE;
    gameState->gameStatus[1] = PARENT_TURN;

    // Create CSS provider for styling
    cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cssProvider,
        ".ship-cell { background-color: green; }"
        ".hit-cell { background-color: red; }",
        -1, NULL);

    // Apply the CSS provider to the default screen
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(cssProvider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    // Create main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Battleship Game");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create main grid
    mainGrid = gtk_grid_new();

    // Create player's grid
    playerFrame = gtk_frame_new("Parent's Board");
    playerGridWidget = createGameGrid(gameState->parentGrid, TRUE, playerButtons);
    gtk_container_add(GTK_CONTAINER(playerFrame), playerGridWidget);

    // Create opponent's grid
    opponentFrame = gtk_frame_new("Child's Board");
    opponentGridWidget = createGameGrid(gameState->childGrid, TRUE, opponentButtons); // Show opponent's ships
    gtk_container_add(GTK_CONTAINER(opponentFrame), opponentGridWidget);

    // Attach frames to the main grid
    gtk_grid_attach(GTK_GRID(mainGrid), playerFrame, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(mainGrid), opponentFrame, 1, 1, 1, 1);

    // Create a menu bar
    menuBar = gtk_menu_bar_new();
    gameMenu = gtk_menu_new();
    gameItem = gtk_menu_item_new_with_label("Game");
    startGameItem = gtk_menu_item_new_with_label("Start Game");
    placeShipsItem = gtk_menu_item_new_with_label("Place Ships");
    saveGameItem = gtk_menu_item_new_with_label("Save Game");
    loadGameItem = gtk_menu_item_new_with_label("Load Game");
    exitItem = gtk_menu_item_new_with_label("Exit Game");

    // Construct the menu
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), startGameItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), placeShipsItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), saveGameItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), loadGameItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), exitItem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameItem), gameMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), gameItem);

    // Connect signals
    g_signal_connect(startGameItem, "activate", G_CALLBACK(onStartGame), NULL);
    g_signal_connect(placeShipsItem, "activate", G_CALLBACK(onPlaceShips), NULL);
    g_signal_connect(saveGameItem, "activate", G_CALLBACK(onSaveGame), NULL);
    g_signal_connect(loadGameItem, "activate", G_CALLBACK(onLoadGame), NULL);
    g_signal_connect(exitItem, "activate", G_CALLBACK(gtk_main_quit), NULL);

    // Add menu bar to the main grid
    gtk_grid_attach(GTK_GRID(mainGrid), menuBar, 0, 0, 3, 1);

    // Create a status bar
    statusFrame = gtk_frame_new("Status");
    statusLabel = gtk_label_new("Welcome to Battleship Game!");
    gtk_container_add(GTK_CONTAINER(statusFrame), statusLabel);
    gtk_grid_attach(GTK_GRID(mainGrid), statusFrame, 0, 4, 3, 1);

    // Create a turn status frame
    turnFrame = gtk_frame_new("Turn");
    turnLabel = gtk_label_new("Current Turn: None");
    gtk_container_add(GTK_CONTAINER(turnFrame), turnLabel);
    gtk_grid_attach(GTK_GRID(mainGrid), turnFrame, 0, 2, 2, 1);

    // Create moves history frame
    movesFrame = gtk_frame_new("Moves History");
    movesTextView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(movesTextView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(movesTextView), FALSE);
    movesBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(movesTextView));

    // Add a scrolled window for the moves history
    movesScrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(movesScrolledWindow, 200, 200);
    gtk_container_add(GTK_CONTAINER(movesScrolledWindow), movesTextView);
    gtk_container_add(GTK_CONTAINER(movesFrame), movesScrolledWindow);
    gtk_grid_attach(GTK_GRID(mainGrid), movesFrame, 2, 1, 1, 3);

    // Add main grid to the window
    gtk_container_add(GTK_CONTAINER(window), mainGrid);

    gtk_widget_show_all(window);
    gtk_main();

    // Detach and remove shared memory
    shmdt(gameState);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}

