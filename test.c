#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define MAX_PATH_DEPTH 4                 //A branch can contain up to four player choices.
#define MAX_TEXT_LENGTH 256              //Maximum characters in one line of dialogue.
#define MAX_CHOICES 2                    //Each dialogue line offers two player answers.
#define MAX_DIALOGUE_NODES 256           //Maximum lines loaded from one character file.
#define MAX_CHARACTERS 5                 //Number of selectable characters below.
#define MAX_EVIDENCE 32                  //Maximum evidence items kept in the inventory.
#define MAX_EVIDENCE_ID_LENGTH 64        //Maximum length of one evidence identifier.
#define INVENTORY_VISIBLE_ITEMS 12       //Number of evidence entries shown at once.
#define INVENTORY_SAVE_PATH "Inventory.txt" //Save file, relative to the game's working folder.

//Holds one line from a character's dialogue file.
typedef struct {
    int branch[MAX_PATH_DEPTH];                   //Choice sequence that leads to this line.
    char text[MAX_CHOICES + 1][MAX_TEXT_LENGTH];  //[0] is spoken text, [1] and [2] are answers.
    char evidenceId[MAX_EVIDENCE_ID_LENGTH];      //Optional ID to add to inventory at this line.
} DialogueNode;

//Remembers where the player is in the current character's conversation.
typedef struct {
    int path[MAX_PATH_DEPTH];  //Answers selected so far, 0 means no answer at that depth.
    int depth;                 //Number of answers selected so far.
    int currentNodeIndex;      //Index in the loaded node array, or -1 if there is no match.
    bool ended;                //True when no next dialogue node was found.
} DialogueState;

//Information needed to show a character on the selection screen.
typedef struct {
    char name[64];       //Name drawn on the character's card.
    char filePath[256];  //Text file containing this character's dialogue.
    Color color;         //Background color of the character's card.
    Rectangle box;       //Position and size of the clickable card.
} NPC;

//The screen currently being shown.
typedef enum {
    SCREEN_CHARACTER_SELECT,
    SCREEN_DIALOGUE,
    SCREEN_INVENTORY
} GameScreen;

static void trimInPlace(char *text) {
    if (!text) {
        return;
    }

    //Move the first non-space character to the start of the same string.
    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    } //test

    //Remove whitespace at the end too, including Windows line endings.
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

//Find the dialogue line whose branch numbers match the player's choices.
static int findNodeByBranch(const DialogueNode *dialogue, int nodeCount, const int path[MAX_PATH_DEPTH]) {
    //Each loaded line stores its own branch path. Compare every part to the
    //player's current path and return the matching line's array index.
    for (int i = 0; i < nodeCount; i++) {
        bool match = true;
        for (int j = 0; j < MAX_PATH_DEPTH; j++) {
            if (dialogue[i].branch[j] != path[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            return i;
        }
    }

    return -1; //-1 means that the file has no line for this set of choices.
}

//Read dialogue lines and their optional EVIDENCE:id tags from a character file.
static int loadDialogueFile(const char *filePath, DialogueNode *out, int maxNodes) {
    FILE *file = fopen(filePath, "r");
    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open dialogue file: %s", filePath);
        return 0;
    }

    int nodeCount = 0; //Number of valid dialogue lines loaded so far.
    char line[512];

    while (fgets(line, sizeof(line), file) != NULL) {
        trimInPlace(line);

        //Empty lines and lines starting with # are for spacing/comments.
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *cursor = line;
        //Start with empty text/evidence fields so an untagged line has no evidence.
        DialogueNode node = {0};
        bool valid = true;

        //Read the four numbers that identify the dialogue branch.
        for (int i = 0; i < MAX_PATH_DEPTH; i++) {
            char *end = NULL;
            long value = strtol(cursor, &end, 10);
            if (end == cursor) {
                valid = false;
                break;
            }

            node.branch[i] = (int)value;
            cursor = end;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
        }

        if (!valid) {
            continue;
        }

        //The file format has another number after the four branch values.
        //Keep accepting it for compatibility with the existing dialogue files.
        char *end = NULL;
        long extra = strtol(cursor, &end, 10);
        (void)extra;
        if (end == cursor) {
            continue;
        }

        cursor = end;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }

        //Read three quoted strings: what the character says, then two answers.
        for (int i = 0; i < MAX_CHOICES + 1; i++) {
            char *start = strchr(cursor, '"');
            if (!start) {
                valid = false;
                break;
            }
            start++;

            char *close = strchr(start, '"');
            if (!close) {
                valid = false;
                break;
            }

            //Copy the quoted text, shortening it if it is too long for the field.
            int length = (int)(close - start);
            if (length >= MAX_TEXT_LENGTH) {
                length = MAX_TEXT_LENGTH - 1;
            }

            memcpy(node.text[i], start, length);
            node.text[i][length] = '\0';

            cursor = close + 1;
            //An optional tag after the dialogue text gives this node an evidence ID.
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            if (*cursor == '>') {
                cursor++;
                while (*cursor == ' ' || *cursor == '\t') {
                    cursor++;
                }
            }
        }

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (strncmp(cursor, "EVIDENCE:", 9) == 0) {
            char *identifier = cursor + 9;
            char *end = identifier;
            while (*end != '\0' && !isspace((unsigned char)*end)) {
                end++;
            }

            size_t length = (size_t)(end - identifier);
            if (length == 0 || length >= sizeof(node.evidenceId)) {
                continue;
            }
            memcpy(node.evidenceId, identifier, length);
            node.evidenceId[length] = '\0';
        }

        if (!valid) {
            continue;
        }

        //Keep valid nodes in order so they can be searched by branch later.
        out[nodeCount++] = node;
        if (nodeCount >= maxNodes) {
            break;
        }
    }

    fclose(file);
    return nodeCount;
}

static void resetDialogueState(DialogueState *state, const DialogueNode *dialogue, int nodeCount) {
    //Clear previous choices, then find the opening line (the all-zero path).
    memset(state, 0, sizeof(*state));
    state->currentNodeIndex = findNodeByBranch(dialogue, nodeCount, state->path);
    if (state->currentNodeIndex == -1) {
        state->ended = true;
    }
}

//Record the selected answer and look for the next dialogue line.
static void advanceDialogue(DialogueState *state, const DialogueNode *dialogue, int nodeCount, int choiceIndex) {
    //There is no more room to record another answer.
    if (state->ended || state->depth >= MAX_PATH_DEPTH) {
        state->ended = true;
        return;
    }

    //Store answer 1 or 2 in the next open branch slot. Paths are written
    //from right to left to match the branch numbering in the dialogue files.
    state->path[MAX_PATH_DEPTH - 1 - state->depth] = choiceIndex + 1;
    state->depth += 1;
    //The next node is whichever file line matches the updated choice path.
    state->currentNodeIndex = findNodeByBranch(dialogue, nodeCount, state->path);

    if (state->currentNodeIndex == -1) {
        state->ended = true;
    }
}

//Write each collected evidence ID on its own line in the save file.
static bool saveInventory(const char inventory[MAX_EVIDENCE][MAX_EVIDENCE_ID_LENGTH], int inventoryCount) {
    //"w" creates the file or replaces its old contents with the current list.
    FILE *file = fopen(INVENTORY_SAVE_PATH, "w");
    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open inventory save file '%s': %s", INVENTORY_SAVE_PATH, strerror(errno));
        return false;
    }

    bool success = true;
    //Store only IDs (not display names), one per line, so they can be read back.
    for (int i = 0; i < inventoryCount; i++) {
        if (fprintf(file, "%s\n", inventory[i]) < 0) {
            success = false;
            break;
        }
    }

    if (fclose(file) != 0) {
        success = false;
    }
    if (!success) {
        TraceLog(LOG_ERROR, "Failed to write inventory save file '%s'.", INVENTORY_SAVE_PATH);
    }
    return success;
}

//Load saved IDs, skipping blank, invalid, or repeated entries.
static int loadInventory(char inventory[MAX_EVIDENCE][MAX_EVIDENCE_ID_LENGTH]) {
    //A missing file means this is the first playthrough, so start empty.
    FILE *file = fopen(INVENTORY_SAVE_PATH, "r");
    if (!file) {
        if (errno != ENOENT) {
            TraceLog(LOG_ERROR, "Failed to open inventory save file '%s': %s", INVENTORY_SAVE_PATH, strerror(errno));
        }
        return 0;
    }

    int inventoryCount = 0;
    //The extra two bytes give room for a maximum-length ID and line ending.
    char line[MAX_EVIDENCE_ID_LENGTH + 2];
    while (fgets(line, sizeof(line), file) != NULL) {
        trimInPlace(line);
        if (line[0] == '\0') {
            continue;
        }
        size_t identifierLength = strlen(line);
        if (identifierLength >= MAX_EVIDENCE_ID_LENGTH) {
            TraceLog(LOG_WARNING, "Ignoring overlong evidence identifier in '%s'.", INVENTORY_SAVE_PATH);
            continue;
        }

        //IDs may contain only letters, numbers, underscores, and hyphens.
        bool valid = true;
        for (int i = 0; line[i] != '\0'; i++) {
            if (!isalnum((unsigned char)line[i]) && line[i] != '_' && line[i] != '-') {
                valid = false;
                break;
            }
        }
        if (!valid) {
            TraceLog(LOG_WARNING, "Ignoring invalid evidence identifier in '%s'.", INVENTORY_SAVE_PATH);
            continue;
        }

        //Avoid adding the same evidence twice, even if the save file repeats it.
        bool duplicate = false;
        for (int i = 0; i < inventoryCount; i++) {
            if (strcmp(inventory[i], line) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (inventoryCount >= MAX_EVIDENCE) {
            TraceLog(LOG_WARNING, "Inventory save contains more than %d items, extra items were ignored.", MAX_EVIDENCE);
            break;
        }

        memcpy(inventory[inventoryCount], line, identifierLength + 1);
        inventoryCount++;
    }

    if (ferror(file)) {
        TraceLog(LOG_ERROR, "Failed while reading inventory save file '%s'.", INVENTORY_SAVE_PATH);
    }
    fclose(file);
    return inventoryCount;
}

//Add evidence from this dialogue line if it has not already been collected.
static void collectEvidence(const DialogueNode *node, char inventory[MAX_EVIDENCE][MAX_EVIDENCE_ID_LENGTH], int *inventoryCount) {
    //Lines without an EVIDENCE: tag have nothing to add.
    if (node->evidenceId[0] == '\0') {
        return;
    }

    //Evidence IDs are unique: do nothing if this ID is already collected.
    for (int i = 0; i < *inventoryCount; i++) {
        if (strcmp(inventory[i], node->evidenceId) == 0) {
            return;
        }
    }

    if (*inventoryCount >= MAX_EVIDENCE) {
        TraceLog(LOG_WARNING, "Inventory is full, could not add evidence: %s", node->evidenceId);
        return;
    }

    //Append the ID, update the count, then immediately persist the new list.
    snprintf(inventory[*inventoryCount], MAX_EVIDENCE_ID_LENGTH, "%s", node->evidenceId);
    (*inventoryCount)++;
    if (!saveInventory(inventory, *inventoryCount)) {
        TraceLog(LOG_ERROR, "Evidence was collected but could not be saved.");
    }
}

//Draw a dialogue choice, using a different color when the mouse is over it.
static void drawChoiceButton(Rectangle box, const char *text, bool hover) {
    Color bg = hover ? SKYBLUE : LIGHTGRAY;
    Color border = hover ? BLUE : DARKGRAY;

    DrawRectangleRounded(box, 0.12f, 0, bg);
    DrawRectangleRoundedLines(box, 0.12f, 0, border);
    DrawText(text, (int)box.x + 16, (int)box.y + 12, 20, BLACK);
}

//Draw a simple button used near the top of the screen.
static void drawTopButton(Rectangle box, const char *text) {
    DrawRectangleRounded(box, 0.12f, 0, LIGHTGRAY);
    DrawText(text, (int)box.x + 14, (int)box.y + 8, 20, BLACK);
}

//Draw dialogue on multiple lines so long sentences stay inside their box.
static void drawWrappedText(const char *text, int x, int y, int fontSize, int maxWidth, int maxHeight, Color color) {
    char line[MAX_TEXT_LENGTH] = {0};
    int lineCount = 0;
    int lineHeight = fontSize + 5;
    int maxLines = maxHeight / lineHeight;

    while (*text != '\0' && lineCount < maxLines) {
        if (*text == '\n') {
            DrawText(line, x, y + lineCount * lineHeight, fontSize, color);
            lineCount++;
            line[0] = '\0';
            text++;
            continue;
        }

        size_t lineLength = strlen(line);
        if (lineLength >= sizeof(line) - 1) {
            DrawText(line, x, y + lineCount * lineHeight, fontSize, color);
            lineCount++;
            line[0] = '\0';
            continue;
        }

        line[lineLength] = *text++;
        line[lineLength + 1] = '\0';

        if (MeasureText(line, fontSize) > maxWidth) {
            //Prefer splitting at a space so words remain intact.
            char *lastSpace = strrchr(line, ' ');
            if (lastSpace && lastSpace != line) {
                char carry[MAX_TEXT_LENGTH];
                snprintf(carry, sizeof(carry), "%s", lastSpace + 1);
                *lastSpace = '\0';
                DrawText(line, x, y + lineCount * lineHeight, fontSize, color);
                lineCount++;
                snprintf(line, sizeof(line), "%s", carry);
            } else if (lineLength > 0) {
                //A single word wider than the box must be split by character.
                char carry[2] = { line[lineLength], '\0' };
                line[lineLength] = '\0';
                DrawText(line, x, y + lineCount * lineHeight, fontSize, color);
                lineCount++;
                snprintf(line, sizeof(line), "%s", carry);
            }
        }
    }

    if (line[0] != '\0' && lineCount < maxLines) {
        DrawText(line, x, y + lineCount * lineHeight, fontSize, color);
    }
}

int main(void) {
    //This table connects each character card to its dialogue text file and appearance.
    NPC characters[MAX_CHARACTERS] = {
        {"Sara", "DialogueText1.txt", RED, {60, 80, 220, 170}},
        {"Miranda", "DialogueText2.txt", BLUE, {330, 80, 220, 170}},
        {"Ash", "DialogueText3.txt", GREEN, {60, 300, 220, 170}},
        {"Asper", "DialogueText4.txt", PURPLE, {330, 300, 220, 170}},
        {"Fila", "DialogueText5.txt", PURPLE, {600, 80, 220, 170}}
    };

    DialogueNode nodes[MAX_DIALOGUE_NODES]; //Loaded dialogue for the selected character.
    DialogueState state = {0};               //Current position in that dialogue.
    char inventory[MAX_EVIDENCE][MAX_EVIDENCE_ID_LENGTH] = {0}; //Collected evidence IDs.
    int inventoryCount = 0;                  //Number of occupied inventory slots.
    int inventoryScroll = 0;                 //First item currently shown in the inventory.
    bool confirmInventoryReset = false;      //Makes the player click reset twice.
    int activeCharacter = -1;                //Selected character's index in characters[].
    int nodeCount = 0;                       //Number of loaded dialogue nodes.
    GameScreen screen = SCREEN_CHARACTER_SELECT; //Screen currently being displayed.
    GameScreen previousScreen = SCREEN_CHARACTER_SELECT; //Return destination from inventory.

    //Start raylib's window and set how often the game loop should run.
    InitWindow(960, 540, "Character Dialogue Select");
    SetTargetFPS(60);
    //Keep evidence from earlier sessions in the inventory.
    inventoryCount = loadInventory(inventory);

    //The game loop checks for input, updates state, and draws one frame.
    while (!WindowShouldClose()) {
        //Mouse coordinates are used to tell which button/card was clicked.
        Vector2 mousePos = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (screen == SCREEN_CHARACTER_SELECT) {
                //Check each character card to see if the player chose it.
                for (int i = 0; i < MAX_CHARACTERS; i++) {
                    if (CheckCollisionPointRec(mousePos, characters[i].box)) {
                        activeCharacter = i;
                        //Load this character's conversation from their text file.
                        nodeCount = loadDialogueFile(characters[i].filePath, nodes, MAX_DIALOGUE_NODES);
                        if (nodeCount > 0) {
                            //Start from the opening line, then award any evidence on it.
                            resetDialogueState(&state, nodes, nodeCount);
                            if (!state.ended && state.currentNodeIndex >= 0) {
                                collectEvidence(&nodes[state.currentNodeIndex], inventory, &inventoryCount);
                            }
                            screen = SCREEN_DIALOGUE;
                        }
                        break;
                    }
                }
                //The inventory button is also available before choosing a character.
                if (CheckCollisionPointRec(mousePos, (Rectangle){ 720, 20, 180, 40 })) {
                    previousScreen = screen;
                    screen = SCREEN_INVENTORY;
                }
            } else if (screen == SCREEN_DIALOGUE) {
                //These rectangles must match the positions used when drawing buttons.
                Rectangle backButton = { 720, 20, 180, 40 };
                Rectangle inventoryButton = { 520, 20, 180, 40 };
                if (CheckCollisionPointRec(mousePos, backButton)) {
                    //Return to character selection and clear the active conversation.
                    screen = SCREEN_CHARACTER_SELECT;
                    activeCharacter = -1;
                    nodeCount = 0;
                    memset(&state, 0, sizeof(state));
                } else if (CheckCollisionPointRec(mousePos, inventoryButton)) {
                    //Remember to return to this conversation when inventory closes.
                    previousScreen = screen;
                    screen = SCREEN_INVENTORY;
                } else if (!state.ended && state.currentNodeIndex >= 0) {
                    //See if the click landed on either of the two answer buttons.
                    Rectangle choiceBoxes[MAX_CHOICES];
                    for (int i = 0; i < MAX_CHOICES; i++) {
                        choiceBoxes[i] = (Rectangle){ 80, 260 + i * 90, 800, 54 };
                        if (CheckCollisionPointRec(mousePos, choiceBoxes[i])) {
                            advanceDialogue(&state, nodes, nodeCount, i);
                            //Evidence is awarded when its tagged line is reached.
                            if (!state.ended && state.currentNodeIndex >= 0) {
                                collectEvidence(&nodes[state.currentNodeIndex], inventory, &inventoryCount);
                            }
                            break;
                        }
                    }
                }
            } else if (screen == SCREEN_INVENTORY) {
                //Inventory buttons: return, or ask twice before deleting the save.
                Rectangle backButton = { 720, 20, 180, 40 };
                Rectangle resetButton = { 500, 20, 180, 40 };
                if (CheckCollisionPointRec(mousePos, backButton)) {
                    screen = previousScreen;
                    confirmInventoryReset = false;
                } else if (CheckCollisionPointRec(mousePos, resetButton)) {
                    if (confirmInventoryReset) {
                        //Clear the in-memory list only after the empty save succeeds.
                        char emptyInventory[MAX_EVIDENCE][MAX_EVIDENCE_ID_LENGTH] = {0};
                        if (saveInventory(emptyInventory, 0)) {
                            memset(inventory, 0, sizeof(inventory));
                            inventoryCount = 0;
                            inventoryScroll = 0;
                        }
                        confirmInventoryReset = false;
                    } else {
                        confirmInventoryReset = true;
                    }
                } else {
                    confirmInventoryReset = false;
                }
            }
        }

        if (screen == SCREEN_INVENTORY) {
            //Let the mouse wheel move through items when the list is longer than the screen.
            float wheelMove = GetMouseWheelMove();
            if (wheelMove > 0.0f && inventoryScroll > 0) {
                inventoryScroll--;
            } else if (wheelMove < 0.0f && inventoryScroll + INVENTORY_VISIBLE_ITEMS < inventoryCount) {
                inventoryScroll++;
            }
        }

        //Start drawing this frame and clear anything left from the previous frame.
        BeginDrawing();
        ClearBackground(RAYWHITE);

        //Draw the character selection, inventory, or active dialogue screen.
        if (screen == SCREEN_CHARACTER_SELECT) {
            DrawText("Select a character", 60, 20, 32, DARKGRAY);
            drawTopButton((Rectangle){ 720, 20, 180, 40 }, TextFormat("Inventory (%d)", inventoryCount));
            for (int i = 0; i < MAX_CHARACTERS; i++) {
                //Highlight the card when the pointer is over it.
                Rectangle box = characters[i].box;
                bool hover = CheckCollisionPointRec(mousePos, box);

                DrawRectangleRounded(box, 0.16f, 0, characters[i].color);
                DrawRectangleRoundedLines(box, 0.16f, 0, hover ? BLACK : WHITE);
                DrawText(characters[i].name, (int)box.x + 20, (int)box.y + 65, 28, WHITE);
            }
        } else {
            if (screen == SCREEN_INVENTORY) {
                DrawText("Inventory", 60, 22, 32, DARKGRAY);
                drawTopButton((Rectangle){ 500, 20, 180, 40 }, confirmInventoryReset ? "Confirm Reset" : "Reset Save");
                drawTopButton((Rectangle){ 720, 20, 180, 40 }, "Back");
                if (confirmInventoryReset) {
                    DrawText("Click Confirm Reset to erase saved evidence.", 80, 490, 20, MAROON);
                }
                if (inventoryCount == 0) {
                    DrawText("No evidence collected yet.", 80, 100, 24, DARKGRAY);
                } else {
                    //Do not draw more than the visible limit and scrolling changes the offset.
                    int visibleCount = inventoryCount - inventoryScroll;
                    if (visibleCount > INVENTORY_VISIBLE_ITEMS) {
                        visibleCount = INVENTORY_VISIBLE_ITEMS;
                    }
                    for (int i = 0; i < visibleCount; i++) {
                        char displayName[MAX_EVIDENCE_ID_LENGTH];
                        snprintf(displayName, sizeof(displayName), "%s", inventory[inventoryScroll + i]);
                        //Show readable names without changing the saved evidence IDs.
                        for (int j = 0; displayName[j] != '\0'; j++) {
                            if (displayName[j] == '_') {
                                displayName[j] = ' ';
                            }
                        }
                        DrawText(TextFormat("- %s", displayName), 80, 90 + i * 34, 22, DARKGRAY);
                    }
                    if (inventoryCount > INVENTORY_VISIBLE_ITEMS) {
                        DrawText(TextFormat("%d-%d of %d (scroll to view more)",
                            inventoryScroll + 1,
                            inventoryScroll + visibleCount,
                            inventoryCount), 80, 510, 18, GRAY);
                    }
                }
            } else {
                DrawText(TextFormat("Character: %s", characters[activeCharacter].name), 60, 22, 28, DARKGRAY);
                drawTopButton((Rectangle){ 520, 20, 180, 40 }, TextFormat("Inventory (%d)", inventoryCount));
                drawTopButton((Rectangle){ 720, 20, 180, 40 }, "Back");

                if (!state.ended && state.currentNodeIndex >= 0) {
                    //Draw the character's words and both available answers.
                    DialogueNode *current = &nodes[state.currentNodeIndex];

                    DrawRectangleRounded((Rectangle){ 60, 80, 840, 170 }, 0.18f, 0, LIGHTGRAY);
                    drawWrappedText(current->text[0], 90, 100, 22, 780, 145, BLACK);

                    for (int i = 0; i < MAX_CHOICES; i++) {
                        Rectangle box = (Rectangle){ 80, 260 + i * 90, 800, 54 };
                        bool hover = CheckCollisionPointRec(mousePos, box);
                        char label[280];
                        snprintf(label, sizeof(label), "%d. %s", i + 1, current->text[i + 1]);
                        drawChoiceButton(box, label, hover);
                    }
                } else {
                    DrawRectangleRounded((Rectangle){ 60, 120, 840, 200 }, 0.18f, 0, LIGHTGRAY);
                    DrawText("Dialogue complete.", 120, 175, 32, DARKGRAY);
                    DrawText("Click Back to choose another character.", 120, 220, 22, DARKGRAY);
                }
            }
        }

        //Finish drawing the frame before the next loop iteration begins.
        EndDrawing();
    }

    //Release the window resources before the program exits.
    CloseWindow();
    return 0;
}