#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define MAX_PATH_DEPTH 4
#define MAX_TEXT_LENGTH 256
#define MAX_CHOICES 2
#define MAX_DIALOGUE_NODES 256
#define MAX_CHARACTERS 5

typedef struct {
    int branch[MAX_PATH_DEPTH];
    char text[MAX_CHOICES + 1][MAX_TEXT_LENGTH];
} DialogueNode;

typedef struct {
    int path[MAX_PATH_DEPTH];
    int depth;
    int currentNodeIndex;
    bool ended;
} DialogueState;

typedef struct {
    char name[64];
    char filePath[256];
    Color color;
    Rectangle box;
} NPC;

typedef enum {
    SCREEN_CHARACTER_SELECT,
    SCREEN_DIALOGUE
} GameScreen;

static void trimInPlace(char *text) {
    if (!text) {
        return;
    }

    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

static int findNodeByBranch(const DialogueNode *dialogue, int nodeCount, const int path[MAX_PATH_DEPTH]) {
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

    return -1;
}

static int loadDialogueFile(const char *filePath, DialogueNode *out, int maxNodes) {
    FILE *file = fopen(filePath, "r");
    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open dialogue file: %s", filePath);
        return 0;
    }

    int nodeCount = 0;
    char line[512];

    while (fgets(line, sizeof(line), file) != NULL) {
        trimInPlace(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *cursor = line;
        DialogueNode node = {0};
        bool valid = true;

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

            int length = (int)(close - start);
            if (length >= MAX_TEXT_LENGTH) {
                length = MAX_TEXT_LENGTH - 1;
            }

            memcpy(node.text[i], start, length);
            node.text[i][length] = '\0';

            cursor = close + 1;
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

        if (!valid) {
            continue;
        }

        out[nodeCount++] = node;
        if (nodeCount >= maxNodes) {
            break;
        }
    }

    fclose(file);
    return nodeCount;
}

static void resetDialogueState(DialogueState *state, const DialogueNode *dialogue, int nodeCount) {
    memset(state, 0, sizeof(*state));
    state->currentNodeIndex = findNodeByBranch(dialogue, nodeCount, state->path);
    if (state->currentNodeIndex == -1) {
        state->ended = true;
    }
}

static void advanceDialogue(DialogueState *state, const DialogueNode *dialogue, int nodeCount, int choiceIndex) {
    if (state->ended || state->depth >= MAX_PATH_DEPTH) {
        state->ended = true;
        return;
    }

    state->path[MAX_PATH_DEPTH - 1 - state->depth] = choiceIndex + 1;
    state->depth += 1;
    state->currentNodeIndex = findNodeByBranch(dialogue, nodeCount, state->path);

    if (state->currentNodeIndex == -1) {
        state->ended = true;
    }
}

static void drawChoiceButton(Rectangle box, const char *text, bool hover) {
    Color bg = hover ? SKYBLUE : LIGHTGRAY;
    Color border = hover ? BLUE : DARKGRAY;

    DrawRectangleRounded(box, 0.12f, 0, bg);
    DrawRectangleRoundedLines(box, 0.12f, 0, border);
    DrawText(text, (int)box.x + 16, (int)box.y + 12, 20, BLACK);
}

int main(void) {
    NPC characters[MAX_CHARACTERS] = {
        {"Sara", "DialogueText1.txt", RED, {60, 80, 220, 170}},
        {"Miranda", "DialogueText2.txt", BLUE, {330, 80, 220, 170}},
        {"Ash", "DialogueText3.txt", GREEN, {60, 300, 220, 170}},
        {"Asper", "DialogueText4.txt", PURPLE, {330, 300, 220, 170}},
        {"Fila", "DialogueText5.txt", PURPLE, {600, 80, 220, 170}}
    };

    DialogueNode nodes[MAX_DIALOGUE_NODES];
    DialogueState state = {0};
    int activeCharacter = -1;
    int nodeCount = 0;
    GameScreen screen = SCREEN_CHARACTER_SELECT;

    InitWindow(960, 540, "Character Dialogue Select");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        Vector2 mousePos = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (screen == SCREEN_CHARACTER_SELECT) {
                for (int i = 0; i < MAX_CHARACTERS; i++) {
                    if (CheckCollisionPointRec(mousePos, characters[i].box)) {
                        activeCharacter = i;
                        nodeCount = loadDialogueFile(characters[i].filePath, nodes, MAX_DIALOGUE_NODES);
                        if (nodeCount > 0) {
                            resetDialogueState(&state, nodes, nodeCount);
                            screen = SCREEN_DIALOGUE;
                        }
                        break;
                    }
                }
            } else if (screen == SCREEN_DIALOGUE) {
                Rectangle backButton = { 720, 20, 180, 40 };
                if (CheckCollisionPointRec(mousePos, backButton)) {
                    screen = SCREEN_CHARACTER_SELECT;
                    activeCharacter = -1;
                    nodeCount = 0;
                    memset(&state, 0, sizeof(state));
                } else if (!state.ended && state.currentNodeIndex >= 0) {
                    Rectangle choiceBoxes[MAX_CHOICES];
                    for (int i = 0; i < MAX_CHOICES; i++) {
                        choiceBoxes[i] = (Rectangle){ 80, 260 + i * 90, 800, 54 };
                        if (CheckCollisionPointRec(mousePos, choiceBoxes[i])) {
                            advanceDialogue(&state, nodes, nodeCount, i);
                            break;
                        }
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (screen == SCREEN_CHARACTER_SELECT) {
            DrawText("Select a character", 60, 20, 32, DARKGRAY);
            for (int i = 0; i < MAX_CHARACTERS; i++) {
                Rectangle box = characters[i].box;
                bool hover = CheckCollisionPointRec(mousePos, box);

                DrawRectangleRounded(box, 0.16f, 0, characters[i].color);
                DrawRectangleRoundedLines(box, 0.16f, 0, hover ? BLACK : WHITE);
                DrawText(characters[i].name, (int)box.x + 20, (int)box.y + 65, 28, WHITE);
            }
        } else {
            DrawText(TextFormat("Character: %s", characters[activeCharacter].name), 60, 22, 28, DARKGRAY);
            DrawRectangleRounded((Rectangle){ 720, 20, 180, 40 }, 0.12f, 0, LIGHTGRAY);
            DrawText("Back", 790, 28, 24, BLACK);

            if (!state.ended && state.currentNodeIndex >= 0) {
                DialogueNode *current = &nodes[state.currentNodeIndex];

                DrawRectangleRounded((Rectangle){ 60, 80, 840, 140 }, 0.18f, 0, LIGHTGRAY);
                DrawText(current->text[0], 90, 110, 22, BLACK);

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

        EndDrawing();
    }

    CloseWindow();
    return 0;
}